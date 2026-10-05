/*
 *  interstellar_core — image sequences and the camera RAW formats by name (R-MEDIA-1).
 *
 *  A run of numbered frames is ONE video source, named the way FFmpeg's image2 and Nuke name it:
 *  `A001_C001/A001_C001_%06d.dng`. The pattern is the source's identity everywhere — the .isp, the
 *  Cosmo slot, every cache key — so nothing downstream needs a second notion of "file". A folder
 *  added to the rack becomes the pattern of its numbered DNG run. Frame i of the source is the i-th
 *  number from the first; a missing number holds the frame before it (a dropped frame on a card is
 *  shown, not refused).
 *
 *  The vendor formats — ARRIRAW, REDCODE RAW, Blackmagic RAW — are NAMED here, as data, so the
 *  service can say which SDK a file needs; decoding them is the host's, through a seam an SDK build
 *  fills (`interstellar_host::registerVendorDecoder`). This header opens directories, never files.
 */
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace arstro
{
namespace interstellar
{
namespace seq
{
    struct Pattern
    {
        std::string dir, prefix, suffix;   // dir/prefix<number, width digits>suffix
        int width = 0;
    };

    inline std::string lower(std::string s)
    {
        for (char &c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    }

    /** `dir/name_%06d.dng` → its parts. Only DNG sequences are sources (a numbered JPEG run is a
     *  folder of stills to Cosmo). */
    inline bool parse(const std::string &spec, Pattern &p)
    {
        const auto slash = spec.find_last_of("/\\");
        const std::string name = slash == std::string::npos ? spec : spec.substr(slash + 1);
        const auto pc = name.find("%0");
        if (pc == std::string::npos) return false;
        size_t i = pc + 2;
        int width = 0;
        while (i < name.size() && std::isdigit((unsigned char)name[i])) width = width * 10 + (name[i++] - '0');
        if (i >= name.size() || name[i] != 'd' || width < 1 || width > 12) return false;
        const std::string suffix = name.substr(i + 1);
        if (lower(suffix).size() < 4 || lower(suffix).compare(lower(suffix).size() - 4, 4, ".dng") != 0) return false;
        p.dir = slash == std::string::npos ? std::string() : spec.substr(0, slash);
        p.prefix = name.substr(0, pc);
        p.suffix = suffix;
        p.width = width;
        return true;
    }

    inline bool isSequence(const std::string &spec)
    {
        Pattern p;
        return parse(spec, p);
    }

    inline std::string fileOf(const Pattern &p, long long n)
    {
        char digits[32];
        std::snprintf(digits, sizeof digits, "%0*lld", p.width, n);
        return (p.dir.empty() ? std::string() : p.dir + "/") + p.prefix + digits + p.suffix;
    }

    /** The frame numbers present on disk, ascending. */
    inline std::vector<long long> numbers(const Pattern &p)
    {
        namespace fs = std::filesystem;
        std::vector<long long> out;
        std::error_code ec;
        for (const auto &e : fs::directory_iterator(p.dir.empty() ? fs::path(".") : fs::path(p.dir), ec))
        {
            const std::string n = e.path().filename().string();
            if (n.size() != p.prefix.size() + (size_t)p.width + p.suffix.size()) continue;
            if (n.compare(0, p.prefix.size(), p.prefix) != 0 || n.compare(n.size() - p.suffix.size(), p.suffix.size(), p.suffix) != 0) continue;
            const std::string d = n.substr(p.prefix.size(), (size_t)p.width);
            if (!std::all_of(d.begin(), d.end(), [](char c) { return std::isdigit((unsigned char)c); })) continue;
            out.push_back(std::stoll(d));
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    inline bool firstFrameExists(const std::string &spec)
    {
        Pattern p;
        return parse(spec, p) && !numbers(p).empty();
    }

    /** The pattern of the numbered DNG run in `dir` — the longest, when a card folder holds two.
     *  False, saying why, when there is none. */
    inline bool fromFolder(const std::string &dir, std::string &pattern, std::string &why)
    {
        namespace fs = std::filesystem;
        std::map<std::tuple<std::string, int, std::string>, int> runs;   // (prefix, width, suffix) → frames
        std::error_code ec;
        for (const auto &e : fs::directory_iterator(dir, ec))
        {
            const std::string n = e.path().filename().string();
            const auto dot = n.find_last_of('.');
            if (dot == std::string::npos || lower(n.substr(dot)) != ".dng") continue;
            size_t a = dot;
            while (a > 0 && std::isdigit((unsigned char)n[a - 1])) --a;
            if (a == dot) continue;   // no number: a still, not a frame
            ++runs[{n.substr(0, a), (int)(dot - a), n.substr(dot)}];
        }
        if (ec) { why = "cannot read the folder " + dir; return false; }
        if (runs.empty()) { why = dir + " holds no numbered .dng frames (a CinemaDNG clip is a folder of name_000001.dng …)"; return false; }
        auto best = runs.begin();
        for (auto it = runs.begin(); it != runs.end(); ++it)
            if (it->second > best->second) best = it;
        pattern = (fs::path(dir) / (std::get<0>(best->first) + "%0" + std::to_string(std::get<1>(best->first)) + "d" + std::get<2>(best->first))).string();
        return true;
    }

    /** A readable name for the sequence: its prefix without the separator before the number. */
    inline std::string stem(const std::string &spec)
    {
        Pattern p;
        if (!parse(spec, p)) return std::string();
        std::string s = p.prefix;
        while (!s.empty() && (s.back() == '_' || s.back() == '.' || s.back() == '-' || s.back() == ' ')) s.pop_back();
        if (s.empty())
        {
            const auto slash = p.dir.find_last_of("/\\");
            s = slash == std::string::npos ? p.dir : p.dir.substr(slash + 1);
        }
        return s;
    }
}

    /** R-MEDIA-1: the camera RAW formats whose decoders are their vendors' SDKs — licensed per user,
     *  so not in this build. Named so a refusal says exactly what is missing. */
    struct VendorRaw
    {
        const char *ext;      // lower case, no dot
        const char *format;   // what a person calls it
        const char *sdk;      // what they would install
    };

    inline const VendorRaw *vendorRawFor(const std::string &spec)
    {
        static const VendorRaw kVendors[] = {
            {"ari", "ARRIRAW", "the ARRI Image SDK"},
            {"r3d", "REDCODE RAW", "the RED R3D SDK"},
            {"braw", "Blackmagic RAW", "the Blackmagic RAW SDK"},
        };
        const auto hash = spec.find('#');
        const std::string file = hash == std::string::npos ? spec : spec.substr(0, hash);
        const auto dot = file.find_last_of('.');
        if (dot == std::string::npos) return nullptr;
        const std::string e = seq::lower(file.substr(dot + 1));
        for (const auto &v : kVendors)
            if (e == v.ext) return &v;
        return nullptr;
    }
}
}
