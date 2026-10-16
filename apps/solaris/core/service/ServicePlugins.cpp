// SolarisService — other makers' VST3 plugins (R-VST-6; docs/discussion.md Round 9). A scan is the host's (each bundle
// read out of process); the list of what it found is the machine's, kept in a text file the host names — a line per
// class: its id, kind, name, maker and bundle, tab-separated. Nothing in the list is an edit.
//
// A song's plugin is a `vst3` device; what PLAYS it is a connection (PluginDevice.h) — one per device, started on first
// need (the model, an engine) and kept while the song is open, so an edit swaps the engine and not the plugin's process.
// A render makes its own, from where the live one stands; saving the song asks each for its state.
#include "Compile.h"
#include "Engine.h"
#include "Format.h"
#include "SolarisService.h"
#include "device/Device.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace arstro
{
namespace solaris
{
    using K = Command::Kind;

    void SolarisService::loadPlugins()
    {
        mPlugins.clear();
        mPluginsLoaded = true;
        if (mHost.pluginsPath.empty()) return;
        std::ifstream f(mHost.pluginsPath);
        std::string line;
        while (std::getline(f, line))
        {
            std::vector<std::string> v;
            size_t a = 0;
            for (size_t t; (t = line.find('\t', a)) != std::string::npos; a = t + 1) v.push_back(line.substr(a, t - a));
            v.push_back(line.substr(a));
            if (v.size() == 5) mPlugins.push_back(PluginModel{v[0], v[2], v[3], v[1], v[4]});
        }
    }

    bool SolarisService::pluginsCommand(const Command &c, std::string &err)
    {
        if (!mPluginsLoaded) loadPlugins();
        auto line = [](const PluginModel &p) { return p.id + "\t" + p.kind + "\t" + p.name + " (" + p.vendor + ")\n"; };
        if (c.kind == K::PluginsList)
        {
            for (const auto &p : mPlugins) mOutput += line(p);
            return true;
        }
        // K::PluginsScan
        if (!mHost.scanPlugins) { err = "this build hosts no plugins (the VST3 SDK was not found when it was made)"; return false; }
        std::vector<PluginModel> found;
        std::vector<std::string> problems;
        if (!mHost.scanPlugins(found, problems, err)) return false;
        std::stable_sort(found.begin(), found.end(), [](const PluginModel &a, const PluginModel &b) { return a.name < b.name; });
        mPlugins = found;
        if (!mHost.pluginsPath.empty())
        {
            std::error_code ec;
            fs::create_directories(fs::path(mHost.pluginsPath).parent_path(), ec);
            std::ofstream f(mHost.pluginsPath, std::ios::binary);
            for (const auto &p : mPlugins) f << p.id << "\t" << p.kind << "\t" << p.name << "\t" << p.vendor << "\t" << p.path << "\n";
            if (!f) { err = "cannot write " + mHost.pluginsPath; return false; }
        }
        for (const auto &p : mPlugins) mOutput += line(p);
        for (const auto &x : problems) mOutput += "problem: " + x + "\n";
        emit(Event(Event::Kind::PluginsChanged).with("count", (long long)mPlugins.size()).with("problems", (long long)problems.size()));
        return true;
    }

    const PluginModel *SolarisService::findPlugin(const std::string &idOrName, std::string &err)
    {
        if (!mPluginsLoaded) loadPlugins();
        auto lower = [](std::string x) { std::transform(x.begin(), x.end(), x.begin(), [](unsigned char ch) { return (char)std::tolower(ch); }); return x; };
        for (const auto &p : mPlugins)
            if (p.id == idOrName) return &p;
        for (const auto &p : mPlugins)
            if (lower(p.name) == lower(idOrName)) return &p;
        err = "no plugin `" + idOrName + "`";
        if (mPlugins.empty()) { err += " — none found on this machine yet (plugins scan)"; return nullptr; }
        std::vector<std::string> names;
        for (const auto &p : mPlugins) names.push_back(p.name);
        const auto near = nearest(idOrName, names);
        if (!near.empty()) err += " (did you mean: " + near[0] + "?)";
        else err += " (plugins list names them)";
        return nullptr;
    }

    bool SolarisService::pluginNode(const std::string &idOrName, DeviceNode &out, std::string &err)
    {
        const PluginModel *pm = findPlugin(idOrName, err);
        if (!pm) return false;
        out.type = "vst3";
        out.plugin = pm->id;
        out.bundle = pm->path;
        out.pluginName = pm->name;
        out.pluginKind = pm->kind;
        return true;
    }

    std::shared_ptr<PluginConnection> SolarisService::startPlugin(const DeviceNode &d, int rate, const std::string &state)
    {
        auto c = std::make_shared<PluginConnection>();
        c->uid = d.plugin;
        c->bundle = d.bundle;
        c->name = d.pluginName;
        c->instrument = d.pluginKind == "instrument"; // the song's word: a strip stays what it was where the bundle is missing
        c->rate = rate;
        c->maxBlock = engine::Engine::kBlock;
        std::vector<std::string> names;
        for (const auto &kv : d.params) names.push_back(kv.first);
        std::string err;
        if (!mHost.loadPlugin) c->failed = "this build plays no plugins (the VST3 SDK was not found when it was made)";
        else if (d.plugin.empty() || d.bundle.empty()) c->failed = "it names no plugin";
        else if (!(c->inst = mHost.loadPlugin(d.bundle, d.plugin, rate, c->maxBlock, err))) c->failed = err.empty() ? "it could not be started" : err;
        else if (!state.empty() && !c->inst->setState(state, err))
            emit(Event(Event::Kind::Info).with("text", d.id + " (" + d.pluginName + ") refused its state — it starts as new: " + err));
        if (c->inst && !c->inst->offline())
        {
            // the values the song sets, given now (a block of no frames — VST3's flush), so its state agrees with the song
            // before any engine plays it
            std::vector<PluginChange> set;
            for (const auto &kv : d.params)
            {
                uint32_t id = 0;
                double v = 0;
                if (pluginParamId(kv.first, id) && parseNumber(kv.second, v) && std::isfinite(v)) set.push_back(PluginChange{id, 0, std::min(1.0, std::max(0.0, v))});
            }
            float none[1] = {0.0f};
            float *out[2] = {none, none};
            if (!set.empty()) c->inst->process(nullptr, out, 0, false, nullptr, 0, set.data(), (int)set.size(), 15.0);
        }
        setPluginType(*c, names);
        return c;
    }

    std::shared_ptr<PluginConnection> SolarisService::livePlugin(const DeviceNode &d)
    {
        auto &c = mPluginLive[d.id];
        const int rate = mProject.header.sampleRate;
        if (c && c->uid == d.plugin && c->bundle == d.bundle && c->rate == rate) return c;
        std::string state = d.state;
        if (c && c->uid == d.plugin && !c->offline())
        {
            std::string s, e;
            if (c->inst->getState(s, e)) state = s; // the song's rate changed: it goes on where it stands
        }
        c = startPlugin(d, rate, state);
        if (c->offline())
        {
            c->told = true;
            emit(Event(Event::Kind::PluginOffline).with("device", d.id).with("why", c->why()));
        }
        return c;
    }

    const DeviceType *SolarisService::typeOf(const DeviceNode &d)
    {
        if (d.type == "vst3") return &livePlugin(d)->type;
        return DeviceRegistry::find(d.type);
    }

    std::vector<std::pair<std::string, std::shared_ptr<PluginConnection>>> SolarisService::attachPlugins(CompileResult &cr, const Project &p, PluginUse use)
    {
        const bool live = use != PluginUse::Render;
        std::vector<std::pair<std::string, std::shared_ptr<PluginConnection>>> made;
        for (const auto &r : p.racks)
            for (const auto &d : r.devices)
            {
                if (d.type != "vst3") continue;
                const auto at = cr.devices.find(d.id);
                if (at == cr.devices.end()) continue;
                engine::DeviceDesc &desc = at->second.first < 0 ? cr.graph.masterRack[(size_t)at->second.second]
                                                                : cr.graph.strips[(size_t)at->second.first].rack[(size_t)at->second.second];
                std::shared_ptr<PluginConnection> c;
                if (use == PluginUse::Live) c = livePlugin(d);
                else
                {
                    // its own instance, at this song's rate (a render's may differ), from where the live one stands
                    std::string state = d.state;
                    const auto lc = mPluginLive.find(d.id);
                    if (lc != mPluginLive.end() && !lc->second->offline())
                    {
                        std::string s, e;
                        if (lc->second->inst->getState(s, e)) state = s;
                    }
                    c = startPlugin(d, p.header.sampleRate, state);
                }
                desc.make = [c, live] { return makePluginDevice(c, live); };
                made.emplace_back(d.id, c);
            }
        return made;
    }

    DeviceModel SolarisService::pluginFacts(const DeviceNode &d, DeviceModel m)
    {
        if (d.type != "vst3") return m;
        const auto c = livePlugin(d);
        m.plugin = d.plugin;
        m.offline = c->offline();
        m.why = c->why();
        m.label = c->type.label;
        return m;
    }

    void SolarisService::prunePlugins()
    {
        for (auto it = mPluginLive.begin(); it != mPluginLive.end();)
        {
            const DeviceNode *d = mOpen ? mProject.device(it->first) : nullptr;
            if (!d || d->type != "vst3") it = mPluginLive.erase(it); // an engine still playing it keeps it until it is freed
            else ++it;
        }
    }

    void SolarisService::capturePluginStates()
    {
        for (auto &r : mProject.racks)
            for (auto &d : r.devices)
            {
                if (d.type != "vst3") continue;
                const auto it = mPluginLive.find(d.id);
                if (it == mPluginLive.end() || it->second->offline()) continue; // offline: the song keeps what it had
                std::string s, e;
                if (it->second->inst->getState(s, e)) d.state = s;
            }
    }

    bool SolarisService::pluginReload(const Command &c, std::string &err)
    {
        if (!requireOpen(err)) return false;
        const DeviceNode *d = mProject.device(c.arg(0));
        if (!d) { err = "no device `" + c.arg(0) + "`"; return false; }
        if (d->type != "vst3") { err = c.arg(0) + " is a " + d->type + ", not a plugin — only a plugin is reloaded"; return false; }
        std::string state = d->state;
        const auto it = mPluginLive.find(d->id);
        if (it != mPluginLive.end() && !it->second->offline())
        {
            std::string s, e;
            if (it->second->inst->getState(s, e)) state = s;
        }
        // a new process; the playing engine keeps the old one until the swap below takes it
        mPluginLive[d->id] = startPlugin(*d, mProject.header.sampleRate, state);
        const auto &now = mPluginLive[d->id];
        liveUpdate(c); // not a Set: the engine is rebuilt with the new one
        refreshModel();
        emit(Event(Event::Kind::Info).with("text", d->id + " (" + d->pluginName + ") " + (now->offline() ? "is still offline: " + now->why() : "reloaded")));
        mOutput = now->offline() ? "offline: " + now->why() + "\n" : "playing\n";
        return true;
    }
}
}
