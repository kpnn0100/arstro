#include "Recents.h"
#include "Json.h"
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <cstdlib>
#include <unistd.h>

namespace genesis
{
namespace ui
{
    namespace
    {
        std::string configDir()
        {
            const char *xdg = std::getenv("XDG_CONFIG_HOME");
            if (xdg && *xdg) return std::string(xdg) + "/arstro";
            const char *home = std::getenv("HOME");
            return std::string(home && *home ? home : ".") + "/.config/arstro";
        }
        bool fileExists(const std::string &p, long long *modified = nullptr)
        {
            struct stat st;
            if (::stat(p.c_str(), &st) != 0) return false;
            if (modified) *modified = (long long)st.st_mtime;
            return true;
        }
    }

    std::string Recents::path() { return configDir() + "/genesis-recents.json"; }

    std::vector<RecentEntry> Recents::load()
    {
        std::vector<RecentEntry> out;
        std::ifstream in(path(), std::ios::binary);
        if (!in) return out;
        std::ostringstream ss;
        ss << in.rdbuf();
        std::string err;
        const Json root = Json::parse(ss.str(), &err);
        if (!err.empty()) return out;
        const Json &list = root["recents"];
        for (int i = 0; i < list.size(); ++i)
        {
            RecentEntry e;
            e.path = list.at(i)["path"].asString("");
            e.name = list.at(i)["name"].asString("");
            e.base = list.at(i)["base"].asString("");
            if (e.path.empty()) continue;
            long long modified = 0;
            e.exists = fileExists(e.path, &modified);
            e.modified = modified ? modified : (long long)list.at(i)["modified"].asNumber(0);
            out.push_back(e);
        }
        return out;
    }

    bool Recents::save(const std::vector<RecentEntry> &entries)
    {
        ::mkdir(configDir().c_str(), 0755);
        Json root = Json::object();
        Json list = Json::array();
        for (const auto &e : entries)
        {
            Json j = Json::object();
            j.set("path", Json::string(e.path));
            j.set("name", Json::string(e.name));
            j.set("base", Json::string(e.base));
            j.set("modified", Json::number((double)e.modified));
            list.push(j);
        }
        root.set("recents", list);
        std::ofstream out(path(), std::ios::binary);
        if (!out) return false;
        out << root.dump() << "\n";
        return true;
    }

    std::string Recents::absolute(const std::string &file)
    {
        if (file.empty() || file[0] == '/') return file;
        char resolved[4096];
        if (::realpath(file.c_str(), resolved))
            return resolved;
        char cwd[4096];
        if (::getcwd(cwd, sizeof cwd))
            return std::string(cwd) + "/" + file;
        return file;
    }

    void Recents::remember(const std::string &rawFile, const std::string &name, const std::string &base)
    {
        // Store an ABSOLUTE path: a recent opened from one working directory has to be
        // reopenable from another, and the launcher is exactly where that happens.
        const std::string file = absolute(rawFile);
        if (file.empty()) return;
        std::vector<RecentEntry> list = load();
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [&](const RecentEntry &e) { return e.path == file; }),
                   list.end());
        RecentEntry e;
        e.path = file;
        e.name = name;
        e.base = base;
        e.exists = fileExists(file, &e.modified);
        list.insert(list.begin(), e);
        if (list.size() > kMax) list.resize(kMax);
        save(list);
    }

    std::string Recents::relativeAge(long long modified, long long nowSeconds)
    {
        if (modified <= 0) return "—";
        const long long d = nowSeconds - modified;
        if (d < 90) return "just now";
        if (d < 3600) return std::to_string(d / 60) + "m ago";
        if (d < 86400) return std::to_string(d / 3600) + "h ago";
        if (d < 86400 * 30) return std::to_string(d / 86400) + "d ago";
        return std::to_string(d / (86400 * 30)) + "mo ago";
    }
}
}
