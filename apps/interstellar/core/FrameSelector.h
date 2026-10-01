/*
 *  interstellar_core — FrameSelector: which frame of a video Cosmo grades (R-RACK-3), shared with
 *  the host's decoder.
 *
 *  A video source reaches Cosmo as an image path with a frame selector — `clip.mp4#t=2.0` — and the
 *  host decoder extracts that frame. Choosing a DIFFERENT reference frame must change no parameter
 *  and no Cosmo node, so the path Cosmo stores is left alone and this table answers "for this
 *  stored path, which frame?" instead. The decoder runs on Cosmo's load workers, hence the mutex.
 *  The `.isp` (`#rackobj frame=`) is the authority; this is its runtime projection.
 */
#pragma once
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>

namespace arstro
{
namespace interstellar
{
    /** `clip.mp4#t=2.000` → (`clip.mp4`, 2.0). No selector → (spec, 0). Pure string work, so the
     *  core can bind a `.cmp` path to its `#rackobj` without a codec. */
    inline void splitFrameSelector(const std::string &spec, std::string &path, double &seconds)
    {
        seconds = 0.0;
        const auto hash = spec.rfind("#t=");
        if (hash == std::string::npos) { path = spec; return; }
        path = spec.substr(0, hash);
        seconds = std::atof(spec.c_str() + hash + 3);
        if (!std::isfinite(seconds) || seconds < 0) seconds = 0.0;
    }

    inline std::string joinFrameSelector(const std::string &path, double seconds)
    {
        if (seconds <= 0.0) return path;
        char buf[32];
        std::snprintf(buf, sizeof buf, "#t=%.3f", seconds);
        return path + buf;
    }

    /** By extension — the same list the host decoder uses to decide what FFmpeg opens. */
    inline bool looksLikeVideo(const std::string &spec)
    {
        std::string file;
        double t = 0;
        splitFrameSelector(spec, file, t);
        const auto dot = file.find_last_of('.');
        if (dot == std::string::npos) return false;
        std::string e = file.substr(dot + 1);
        for (char &c : e) c = (char)std::tolower((unsigned char)c);
        static const char *kVideo[] = {"mp4", "mov", "mkv", "m4v", "avi", "mxf", "webm", "mts",
                                       "m2ts", "wmv", "flv", "r3d", "braw"};
        for (const char *v : kVideo)
            if (e == v) return true;
        return false;
    }

    class FrameSelector
    {
    public:
        /** Seconds to decode for a path as Cosmo stores it, or `fallback` (the path's own `#t=`). */
        double frameFor(const std::string &storedPath, double fallback) const
        {
            std::lock_guard<std::mutex> l(mMu);
            const auto it = mFrames.find(storedPath);
            return it == mFrames.end() ? fallback : it->second;
        }
        void set(const std::string &storedPath, double seconds)
        {
            std::lock_guard<std::mutex> l(mMu);
            mFrames[storedPath] = seconds;
        }
        void clear()
        {
            std::lock_guard<std::mutex> l(mMu);
            mFrames.clear();
        }

    private:
        mutable std::mutex mMu;
        std::map<std::string, double> mFrames;
    };
}
}
