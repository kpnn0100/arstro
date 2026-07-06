#include "ProjectStore.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace arstro
{
namespace cosmo
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr int kMaxRecents = 24;

        // Fields are tab-separated on one line; strip any tab/newline defensively.
        std::string sanitize(std::string s)
        {
            for (char &c : s) if (c == '\t' || c == '\n' || c == '\r') c = ' ';
            return s;
        }

        std::string homeDir()
        {
            if (const char *h = std::getenv("HOME")) return h;
            return ".";
        }
    }

    std::string ProjectStore::configDir()
    {
        std::string base;
        if (const char *xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) base = xdg;
        else base = homeDir() + "/.config";
        const std::string dir = base + "/cosmo_v2";
        std::error_code ec;
        fs::create_directories(dir, ec);
        return dir;
    }

    std::string ProjectStore::indexPath() { return configDir() + "/recent.tsv"; }

    std::vector<RecentEntry> ProjectStore::recents()
    {
        std::vector<RecentEntry> out;
        std::ifstream f(indexPath());
        if (!f) return out;
        std::string line;
        while (std::getline(f, line))
        {
            if (line.empty()) continue;
            std::stringstream ss(line);
            RecentEntry e;
            std::string count, size, opened;
            std::getline(ss, e.name, '\t');
            std::getline(ss, e.path, '\t');
            std::getline(ss, e.firstImagePath, '\t');
            std::getline(ss, count, '\t');
            std::getline(ss, size, '\t');
            std::getline(ss, opened, '\t');
            if (e.path.empty()) continue;
            std::error_code ec;
            if (!fs::exists(e.path, ec)) continue;  // project deleted/moved -> drop it
            try { e.photoCount = count.empty() ? 0 : std::stoi(count); } catch (...) { e.photoCount = 0; }
            try { e.sizeBytes = size.empty() ? 0 : std::stoll(size); } catch (...) { e.sizeBytes = 0; }
            try { e.lastOpened = opened.empty() ? 0 : std::stoll(opened); } catch (...) { e.lastOpened = 0; }
            out.push_back(std::move(e));
        }
        return out;
    }

    void ProjectStore::remember(RecentEntry e)
    {
        if (e.path.empty()) return;
        std::vector<RecentEntry> list = recents();
        // Drop any existing entry for the same project, then push to the front.
        for (auto it = list.begin(); it != list.end();)
            it = (it->path == e.path) ? list.erase(it) : it + 1;
        list.insert(list.begin(), std::move(e));
        if ((int)list.size() > kMaxRecents) list.resize(kMaxRecents);

        std::ofstream f(indexPath(), std::ios::trunc);
        if (!f) return;
        for (const auto &r : list)
            f << sanitize(r.name) << '\t' << sanitize(r.path) << '\t' << sanitize(r.firstImagePath) << '\t'
              << r.photoCount << '\t' << r.sizeBytes << '\t' << r.lastOpened << '\n';
    }
}
}
