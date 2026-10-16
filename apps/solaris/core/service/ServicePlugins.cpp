// SolarisService — other makers' VST3 plugins (R-VST-6; docs/discussion.md Round 9). A scan is the host's (each bundle
// read out of process); the list of what it found is the machine's, kept in a text file the host names — a line per
// class: its id, kind, name, maker and bundle, tab-separated. Nothing here is an edit.
#include "SolarisService.h"
#include <algorithm>
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
}
}
