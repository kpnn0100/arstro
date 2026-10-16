// SolarisService — the machine's sample library (R-PRESET-3): tags and favourites on sound files, kept in a text file
// the host names (a line per tagged file: its absolute path, a tab, its tags), and a search through the sample folders
// by name and tag. The machine's, never a song's: nothing here is an edit.
#include "Format.h"
#include "SolarisService.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>

namespace fs = std::filesystem;

namespace arstro
{
namespace solaris
{
    using K = Command::Kind;

    namespace
    {
        std::string lower(std::string s)
        {
            for (auto &ch : s) ch = (char)std::tolower((unsigned char)ch);
            return s;
        }
        bool tagOk(const std::string &t, std::string &err)
        {
            bool ok = !t.empty() && t.size() <= 32;
            for (char ch : t) ok = ok && (std::isalnum((unsigned char)ch) || ch == '-');
            if (!ok) err = "a tag is 1 … 32 letters, digits or dashes — got `" + t + "`";
            return ok;
        }
        std::string absolutePath(const std::string &p) // as the recents keep theirs
        {
            if (p.empty()) return p;
            std::error_code ec;
            const fs::path a = fs::absolute(fs::path(p), ec);
            return ec ? p : a.lexically_normal().string();
        }
        std::string fileName(const std::string &path)
        {
            const auto slash = path.find_last_of('/');
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }
    }

    void SolarisService::loadLibrary()
    {
        mLibrary.clear();
        mLibraryLoaded = true;
        if (mHost.libraryPath.empty()) return;
        std::ifstream f(mHost.libraryPath);
        std::string line;
        while (std::getline(f, line))
        {
            const auto tab = line.find('\t');
            if (line.empty() || tab == std::string::npos) continue;
            std::vector<std::string> tags;
            std::string cur;
            for (char ch : line.substr(tab + 1) + ",")
                if (ch == ',') { if (!cur.empty()) tags.push_back(lower(cur)); cur.clear(); }
                else if (ch != '\r') cur += ch;
            std::sort(tags.begin(), tags.end());
            tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
            if (!tags.empty()) mLibrary[line.substr(0, tab)] = tags;
        }
    }

    bool SolarisService::saveLibrary(std::string &err)
    {
        if (mHost.libraryPath.empty()) { err = "this build keeps no sample library"; return false; }
        std::error_code ec;
        fs::create_directories(fs::path(mHost.libraryPath).parent_path(), ec);
        std::ofstream f(mHost.libraryPath, std::ios::binary);
        if (!f) { err = "cannot write " + mHost.libraryPath; return false; }
        for (const auto &e : mLibrary)
        {
            f << e.first << "\t";
            for (size_t i = 0; i < e.second.size(); ++i) f << (i ? "," : "") << e.second[i];
            f << "\n";
        }
        return (bool)f;
    }

    bool SolarisService::libraryCommand(const Command &c, std::string &err)
    {
        if (!mLibraryLoaded) loadLibrary();
        const std::string path = absolutePath(c.arg(0));
        std::string tag;
        bool add = true;
        if (c.kind == K::SampleFavourite)
        {
            tag = "favourite";
            const std::string v = c.arg(1).empty() ? "on" : c.arg(1);
            if (v != "on" && v != "off") { err = "sample favourite <file> [on|off]"; return false; }
            add = v == "on";
        }
        else
        {
            tag = lower(c.arg(1));
            if (!tagOk(tag, err)) return false;
            add = c.kind == K::SampleTag;
        }
        auto &tags = mLibrary[path];
        const bool had = std::find(tags.begin(), tags.end(), tag) != tags.end();
        if (add && !had) tags.push_back(tag);
        if (!add) tags.erase(std::remove(tags.begin(), tags.end(), tag), tags.end());
        std::sort(tags.begin(), tags.end());
        std::string list;
        for (const auto &t : tags) list += (list.empty() ? "" : ",") + t;
        if (tags.empty()) mLibrary.erase(path);
        if (!saveLibrary(err)) return false;
        emit(Event(Event::Kind::LibraryChanged).with("path", path).with("tags", list));
        mOutput = path + "\t" + list + "\n";
        return true;
    }

    bool SolarisService::searchSamples(const std::string &folder, const std::string &text, bool favourites, std::string &err)
    {
        // R-PRESET-3: by name or tag through the sample folders (eight levels down, the first 200), and the library's
        if (!mLibraryLoaded) loadLibrary();
        const std::string want = lower(text);
        std::vector<BrowserEntry> found;
        std::set<std::string> seen;
        auto tagged = [&](const std::string &path) {
            const auto it = mLibrary.find(path);
            if (it == mLibrary.end()) return false;
            if (favourites) return std::find(it->second.begin(), it->second.end(), "favourite") != it->second.end();
            for (const auto &t : it->second)
                if (t.compare(0, want.size(), want) == 0) return true;
            return false;
        };
        auto take = [&](const std::string &path) {
            if (found.size() >= 200 || !seen.insert(path).second) return;
            found.push_back(BrowserEntry{fileName(path), path, "audio"});
        };
        if (!favourites)
        {
            if (!mHost.listDir) { err = "this build cannot list folders"; return false; }
            int visited = 0;
            std::function<void(const std::string &, int)> walk = [&](const std::string &dir, int depth) {
                if (depth > 8 || found.size() >= 200 || ++visited > 2000) return;
                std::vector<BrowserEntry> entries;
                std::string why;
                if (!mHost.listDir(dir, entries, why)) return;
                for (const auto &e : entries)
                {
                    if (e.kind == "dir") walk(e.path, depth + 1);
                    else if (e.kind == "audio" && (lower(e.name).find(want) != std::string::npos || tagged(e.path))) take(e.path);
                }
            };
            if (!folder.empty()) walk(folder, 0);
            else
                for (const auto &f : mSettings.folders) walk(f, 0);
        }
        for (const auto &e : mLibrary) // the library's own, wherever they are
            if (tagged(e.first) || (!favourites && lower(fileName(e.first)).find(want) != std::string::npos)) take(e.first);
        std::stable_sort(found.begin(), found.end(), [](const BrowserEntry &a, const BrowserEntry &b) { return lower(a.name) < lower(b.name); });
        mBrowser.path = folder;
        mBrowser.entries = found;
        mBrowser.search = favourites ? "favourites" : text;
        for (const auto &e : found) mOutput += e.kind + "\t" + e.path + "\n";
        emit(Event(Event::Kind::BrowseChanged).with("path", favourites ? std::string("favourites") : "search:" + text).with("entries", (long long)found.size()));
        return true;
    }
}
}
