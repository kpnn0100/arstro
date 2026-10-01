#include "Rack.h"
#include "engine/EditParamsIO.h"
#include <algorithm>
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
        refreshParams();
        return true;
    }

    bool Rack::newProject(const std::string &path, std::string &err)
    {
        cosmo::Command c;
        c.kind = cosmo::Command::Kind::ProjectNew;
        c.path = path;
        if (!dispatch(c, err)) return false;
        mPath = path;
        mOwn.clear();
        ++mParamsRevision;
        return true;
    }

    bool Rack::addSources(const std::vector<std::string> &paths, std::string &err, std::vector<int> *added)
    {
        if (paths.empty()) { err = "no sources given"; return false; }
        std::vector<int> before;
        for (const auto &n : mCosmo.model().nodes) before.push_back(n.node);
        // `add`, not `import`: Cosmo's import REPLACES the workspace. P1 used import and passed its
        // gate only because the gate added to an empty project — a second `rack add` would have
        // wiped the rack.
        cosmo::Command c;
        c.kind = cosmo::Command::Kind::Add;
        c.paths = paths;
        if (!dispatch(c, err)) return false;
        if (!pumpUntilLoaded()) { err = "the add is still decoding after 30 s"; return false; }
        std::vector<int> fresh;
        for (const auto &n : mCosmo.model().nodes)
            if (std::find(before.begin(), before.end(), n.node) == before.end() && !n.group) fresh.push_back(n.node);
        for (int node : fresh)
        {
            std::string e;
            if (select(node, e)) cacheSelectedOwn();
        }
        ++mParamsRevision;
        if (added) *added = fresh;
        return true;
    }

    bool Rack::beginOpen(const std::string &path, std::string &err)
    {
        cosmo::Command c;
        c.kind = cosmo::Command::Kind::ProjectOpen;
        c.path = path;
        if (!dispatch(c, err)) return false;
        mPath = path;
        mAdding = false;
        mOwn.clear();
        return true;
    }

    bool Rack::beginAdd(const std::vector<std::string> &paths, std::string &err)
    {
        if (paths.empty()) { err = "no sources given"; return false; }
        mBeforeAdd.clear();
        for (const auto &n : mCosmo.model().nodes) mBeforeAdd.push_back(n.node);
        cosmo::Command c;
        c.kind = cosmo::Command::Kind::Add;
        c.paths = paths;
        if (!dispatch(c, err)) return false;
        mAdding = true;
        return true;
    }

    bool Rack::loading() const { return mCosmo.loadActive() || mCosmo.model().load.active; }

    void Rack::finishLoad(std::vector<int> *added)
    {
        if (!mAdding)
        {
            refreshParams();
            if (added) added->clear();
            return;
        }
        mAdding = false;
        std::vector<int> fresh;
        for (const auto &n : mCosmo.model().nodes)
            if (!n.group && std::find(mBeforeAdd.begin(), mBeforeAdd.end(), n.node) == mBeforeAdd.end())
                fresh.push_back(n.node);
        const int was = mCosmo.model().selectedNode;
        for (int node : fresh)
        {
            std::string e;
            if (select(node, e)) cacheSelectedOwn();
        }
        std::string e;
        if (was >= 0) select(was, e);
        ++mParamsRevision;
        if (added) *added = fresh;
    }

    bool Rack::reopen(std::string &err)
    {
        if (!save(err)) return false;
        const std::string path = mPath;
        return openProject(path, err);
    }

    bool Rack::setParams(int node, const std::vector<std::pair<std::string, std::string>> &fields, std::string &err)
    {
        if (!isOpen()) { err = "no rack project is open"; return false; }
        if (fields.empty()) return true;
        if (!select(node, err)) return false;
        cosmo::Command c;
        c.kind = cosmo::Command::Kind::Set;
        c.fields = fields;
        if (!dispatch(c, err)) return false;
        mCosmo.pump(0.0);
        cacheSelectedOwn();
        return true;
    }

    bool Rack::groupNodes(const std::string &name, const std::vector<int> &members, int &groupOut, std::string &err)
    {
        if (members.empty()) { err = "group new: no nodes given"; return false; }
        std::vector<int> before;
        for (const auto &n : mCosmo.model().nodes) before.push_back(n.node);
        for (size_t i = 0; i < members.size(); ++i)
        {
            cosmo::Command s;
            s.kind = cosmo::Command::Kind::Select;
            s.index = members[i];
            s.name = i == 0 ? "" : "add";
            if (!dispatch(s, err)) return false;
        }
        cosmo::Command g;
        g.kind = cosmo::Command::Kind::GroupNew;
        g.name = name;
        if (!dispatch(g, err)) return false;
        mCosmo.pump(0.0);
        groupOut = -1;
        for (const auto &n : mCosmo.model().nodes)
            if (n.group && std::find(before.begin(), before.end(), n.node) == before.end()) groupOut = n.node;
        if (groupOut < 0) { err = "group new: Cosmo created no group"; return false; }
        std::string e;
        if (select(groupOut, e)) cacheSelectedOwn();
        ++mParamsRevision;
        return true;
    }

    bool Rack::setBypass(int node, bool on, std::string &err)
    {
        cosmo::Command c;
        c.kind = cosmo::Command::Kind::Bypass;
        c.index = node;
        c.flag = on;
        if (!dispatch(c, err)) return false;
        mCosmo.pump(0.0);
        ++mParamsRevision;
        return true;
    }

    bool Rack::ungroup(int group, std::string &err)
    {
        cosmo::Command c;
        c.kind = cosmo::Command::Kind::GroupUngroup;
        c.index = group;
        if (!dispatch(c, err)) return false;
        mCosmo.pump(0.0);
        ++mParamsRevision;
        return true;
    }

    bool Rack::presetApply(int node, const std::string &name, std::string &err)
    {
        if (!select(node, err)) return false;
        cosmo::Command c;
        c.kind = cosmo::Command::Kind::PresetApply;
        c.name = name;
        if (!dispatch(c, err)) return false;
        mCosmo.pump(0.0);
        cacheSelectedOwn();
        return true;
    }

    bool Rack::presetSave(int node, const std::string &name, std::string &err)
    {
        if (!select(node, err)) return false;
        cosmo::Command c;
        c.kind = cosmo::Command::Kind::PresetSave;
        c.name = name;
        return dispatch(c, err);
    }

    void Rack::setPresetDir(const std::string &dir) { mCosmo.session().setPresetDir(dir); }

    void Rack::applySettings(const cosmo::AppSettings &s) { mCosmo.applySettings(s); }

    int Rack::indexOf(int node) const
    {
        const auto &ns = mCosmo.model().nodes;
        for (size_t i = 0; i < ns.size(); ++i)
            if (ns[i].node == node) return (int)i;
        return -1;
    }

    void Rack::cacheSelectedOwn()
    {
        const auto &m = mCosmo.model();
        if (!m.hasEditTarget || m.selectedNode < 0) return;
        for (auto &kv : mOwn)
            if (kv.first == m.selectedNode) { kv.second = m.ownParams; ++mParamsRevision; return; }
        mOwn.emplace_back(m.selectedNode, m.ownParams);
        ++mParamsRevision;
    }

    void Rack::refreshParams()
    {
        mOwn.clear();
        const int was = mCosmo.model().selectedNode;
        for (const auto &n : mCosmo.model().nodes)
        {
            if (n.failed || n.pending) continue;   // no slot, no params to read — and nothing to render
            std::string e;
            if (select(n.node, e)) cacheSelectedOwn();
        }
        std::string e;
        if (was >= 0) select(was, e);
        ++mParamsRevision;
    }

    bool Rack::ownParams(int node, EditParams &out) const
    {
        for (const auto &kv : mOwn)
            if (kv.first == node) { out = kv.second; return true; }
        return false;
    }

    bool Rack::colourTree(ColourTree &out) const
    {
        const auto &ns = mCosmo.model().nodes;
        out.clear();
        out.reserve(ns.size());
        for (const auto &n : ns)
        {
            ColourNode c;
            // `NodeModel::parent` is the parent's NODE ID (EditSession::TreeRow), not an index into
            // `nodes` as cosmo's AppModel.h comment says — walking it as an index loops forever on
            // the first nested node (found by this suite hanging). Converted to an index here.
            c.parent = n.parent < 0 ? -1 : indexOf(n.parent);
            c.group = n.group;
            c.bypass = n.bypass;
            c.cosmoNode = n.node;
            c.name = n.name;
            if (!ownParams(n.node, c.own) && !n.failed && !n.pending) return false;
            out.push_back(std::move(c));
        }
        return true;
    }

    bool Rack::renderParams(int node, EditParams &out) const
    {
        const int i = indexOf(node);
        if (i < 0) return false;
        EditParams own;
        if (!ownParams(node, own)) return false;
        ColourTree t;
        if (!colourTree(t)) return false;
        out = foldRender(t, i);   // EditSession::effectiveParams(slot), step for step
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
        cacheSelectedOwn();
        return true;
    }

    bool Rack::getParam(int node, const std::string &key, double &out) const
    {
        EditParams p;
        // This node's OWN contribution (the stacked reach is `renderParams`). Read from the cache
        // every write through this class refreshes, so a read does not move Cosmo's selection.
        if (!ownParams(node, p)) return false;

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
