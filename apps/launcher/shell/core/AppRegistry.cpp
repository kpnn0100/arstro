#include "AppRegistry.h"
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace arstro
{
namespace shell
{
    static std::string trim(const std::string &s)
    {
        size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
        return a == std::string::npos ? "" : s.substr(a, b - a + 1);
    }

    bool validAppId(const std::string &id)
    {
        if (id.empty() || id.size() > 96 || id.front() == '.' || id.back() == '.') return false;
        for (char c : id)
            if (!(std::isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_')) return false;
        return true;
    }

    bool parseManifest(const std::string &text, const std::string &dir, AppInfo &out, std::string &error)
    {
        out = AppInfo{};
        out.dir = dir;
        std::istringstream in(text);
        std::string line;
        int n = 0;
        while (std::getline(in, line))
        {
            ++n;
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            const size_t eq = line.find('=');
            if (eq == std::string::npos) { error = "line " + std::to_string(n) + ": expected key = value"; return false; }
            const std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
            if (k == "id") out.id = v;
            else if (k == "name") out.name = v;
            else if (k == "version") out.version = v;
            else if (k == "exec") out.exec = v;
            else if (k == "icon") out.icon = v;
            else if (k == "accent") out.accent = v;
            else if (k == "permissions") out.system = (" " + v + " ").find(" system ") != std::string::npos;
            else { error = "line " + std::to_string(n) + ": unknown key '" + k + "'"; return false; }
        }
        if (!validAppId(out.id)) { error = "missing or illegal id"; return false; }
        if (out.name.empty()) { error = "missing name"; return false; }
        if (out.exec.empty()) { error = "missing exec"; return false; }
        if (out.exec[0] != '/') out.exec = dir + "/" + out.exec;
        if (!out.icon.empty() && out.icon[0] != '/') out.icon = dir + "/" + out.icon;
        return true;
    }

    std::vector<std::string> defaultAppRoots()
    {
        std::vector<std::string> roots;
        if (const char *env = std::getenv("ARSTRO_APPS"))
        {
            std::istringstream in(env);
            std::string part;
            while (std::getline(in, part, ':'))
                if (!part.empty()) roots.push_back(part);
            return roots;
        }
        if (const char *home = std::getenv("HOME")) roots.push_back(std::string(home) + "/.local/share/arstro/apps");
        roots.push_back("/opt/arstro/apps");
        return roots;
    }

    std::vector<AppInfo> scanApps(const std::vector<std::string> &roots, std::vector<std::string> *problems)
    {
        std::vector<AppInfo> apps;
        for (const std::string &root : roots)
        {
            DIR *d = opendir(root.c_str());
            if (!d) continue;
            while (dirent *e = readdir(d))
            {
                const std::string name = e->d_name;
                if (name == "." || name == "..") continue;
                const std::string dir = root + "/" + name;
                std::ifstream f(dir + "/manifest.arstro");
                if (!f) continue;
                std::stringstream ss;
                ss << f.rdbuf();
                AppInfo app;
                std::string error;
                if (!parseManifest(ss.str(), dir, app, error))
                {
                    if (problems) problems->push_back(dir + ": " + error);
                    continue;
                }
                // The first root wins, so a per-user install shadows a system one of the same id.
                bool dup = false;
                for (const AppInfo &a : apps) dup = dup || a.id == app.id;
                if (!dup) apps.push_back(app);
            }
            closedir(d);
        }
        return apps;
    }
}
}
