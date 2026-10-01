#include "Rack.h"
#include "engine/EditParamsIO.h"
#include <chrono>
#include <thread>
#include <cmath>
#include <sstream>

namespace arstro
{
namespace interstellar
{
    Rack::Rack(cosmo::ThreadBudget &budget) : mCosmo(budget) {}

    void Rack::setDecoderFactory(DecoderFactory f) { mCosmo.setDecoderFactory(std::move(f)); }
    void Rack::setWorkerInit(std::function<void()> f) { mCosmo.setWorkerInit(std::move(f)); }
    void Rack::subscribe(std::function<void(const cosmo::Event &)> sink)
    {
        mCosmo.subscribe(std::move(sink));
    }

    bool Rack::dispatch(const cosmo::Command &c, std::string &err)
    {
        if (mCosmo.dispatch(c)) return true;
        // Cosmo puts the reason in its own model, so the caller gets the real message rather than
        // "it failed" — which is the difference between a usable error and a shrug.
        err = mCosmo.model().lastError;
        if (err.empty()) err = "cosmo refused: " + cosmo::formatCommand(c);
        return false;
    }

    bool Rack::openProject(const std::string &path, std::string &err)
    {
        cosmo::Command c;
        c.kind = cosmo::Command::Kind::ProjectOpen;
        c.path = path;
        if (!dispatch(c, err)) return false;
        mPath = path;
        // The load runs on a pool and is delivered through `pump`; a caller that forgot to pump
        // would see an empty rack and conclude the file was empty.
        if (!pumpUntilLoaded())
        {
            err = "the rack is still loading after 30 s — " + path;
            return false;
        }
        return true;
    }

    bool Rack::newProject(const std::string &path, std::string &err)
    {
        cosmo::Command c;
        c.kind = cosmo::Command::Kind::ProjectNew;
        c.path = path;
        if (!dispatch(c, err)) return false;
        mPath = path;
        return true;
    }

    bool Rack::addSources(const std::vector<std::string> &paths, std::string &err)
    {
        if (paths.empty()) { err = "no sources given"; return false; }
        cosmo::Command c;
        c.kind = cosmo::Command::Kind::Import;
        c.paths = paths;
        if (!dispatch(c, err)) return false;
        if (!pumpUntilLoaded()) { err = "the import is still decoding after 30 s"; return false; }
        return true;
    }

    bool Rack::save(std::string &err)
    {
        if (mPath.empty()) { err = "no rack project is open"; return false; }
        cosmo::Command c;
        c.kind = cosmo::Command::Kind::ProjectSave;
        return dispatch(c, err);
    }

    void Rack::pump(double nowMs)
    {
        if (nowMs > mNow) mNow = nowMs;
        mCosmo.pump(mNow);
    }

    bool Rack::pumpUntilLoaded(int maxMs)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(maxMs);
        bool settled = false;
        while (std::chrono::steady_clock::now() < deadline)
        {
            mCosmo.pump(mNow);
            mNow += 16.0;                 // a fixed tick, so a scripted run is reproducible
            if (!mCosmo.loadActive() && !mCosmo.model().load.active) { settled = true; break; }
            // 1 ms of REAL time, because the decode is on a pool and a spin over simulated time
            // would run out of budget before a worker opened the first file.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        mCosmo.pump(mNow);
        return settled;
    }

    std::vector<Rack::Node> Rack::nodes() const
    {
        std::vector<Node> out;
        for (const auto &n : mCosmo.model().nodes)
        {
            Node r;
            r.id = n.node;
            r.parent = n.parent;
            r.depth = n.depth;
            r.group = n.group;
            r.bypass = n.bypass;
            r.pending = n.pending;
            r.failed = n.failed;
            r.cosmoName = n.name;
            out.push_back(r);
        }
        return out;
    }

    int Rack::imageCount() const { return mCosmo.model().imageCount; }

    int Rack::selectedNode() const { return mCosmo.model().selectedNode; }

    bool Rack::select(int node, std::string &err)
    {
        cosmo::Command c;
        c.kind = cosmo::Command::Kind::Select;
        c.index = node;
        if (!dispatch(c, err)) return false;
        mCosmo.pump(0.0);
        return true;
    }

    bool Rack::setParam(int node, const std::string &key, const std::string &value, std::string &err)
    {
        if (!isOpen()) { err = "no rack project is open"; return false; }
        if (!select(node, err)) return false;

        // The SAME pair `cosmo-cc` sends. Cosmo's `set` feeds its fields through the very codec
        // that reads a `.cmp` and a `.apf`, so a key that round-trips through a project file is
        // automatically settable — and a key that is NOT settable is a hole in persistence, not in
        // this call.
        cosmo::Command c;
        c.kind = cosmo::Command::Kind::Set;
        c.fields.emplace_back(key, value);
        if (!dispatch(c, err)) return false;
        mCosmo.pump(0.0);
        return true;
    }

    bool Rack::getParam(int node, const std::string &key, double &out) const
    {
        EditParams p;
        // `ownParams` is this node's own contribution; `params` is the stacked reach. A caller
        // asking "what did I set on this node" means the first, and the two differ on purpose.
        Rack &self = const_cast<Rack &>(*this);
        std::string err;
        if (!self.select(node, err)) return false;
        if (!mCosmo.model().hasEditTarget) return false;
        p = mCosmo.model().ownParams;

        // Read the scalar back out through Cosmo's own serializer rather than a switch over field
        // names here: a hand-written mapping would be a second list of what EditParams contains and
        // would go stale the first time a field was added.
        const std::string text = serializeParams(p);
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line))
        {
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            if (line.substr(0, eq) != key) continue;
            out = std::atof(line.c_str() + eq + 1);
            return std::isfinite(out);
        }
        return false;
    }

    bool Rack::effectiveParams(int node, EditParams &out) const
    {
        Rack &self = const_cast<Rack &>(*this);
        std::string err;
        if (!self.select(node, err)) return false;
        if (!mCosmo.model().hasEditTarget) return false;
        // `params` is `effectiveEditParams` — the node's own composed with its ancestors, which is
        // what a frame renders with (R-RACK-4's stacking, done by Cosmo's own composeParams).
        out = mCosmo.model().params;
        return true;
    }
}
}
