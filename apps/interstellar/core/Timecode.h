/*
 *  interstellar_core — Timecode: frames ↔ SMPTE timecode at a rate, drop-frame included (R-XCH-5).
 *  Header-only and dependency-free, so the UI can show a source's timecode without the core.
 *
 *  29.97 and 59.94 count drop-frame (two or four frame NUMBERS skipped each minute but every tenth —
 *  no frame is dropped, only labels); 23.976 has no drop-frame form and counts 24 a labelled second.
 */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace arstro
{
namespace interstellar
{
namespace xch
{
    /** A frame rate as timecode counts it: nominal frames per labelled second, and drop-frame. */
    struct TcRate
    {
        double fps = 24.0;
        int nominal = 24;
        bool drop = false;
        static TcRate of(double fps);           // 29.97 / 59.94 → drop-frame
    };
    std::string tcFromFrames(long long frames, const TcRate &r);
    /** "HH:MM:SS:FF" or "HH:MM:SS;FF" (drop) → frames; false when it is not a timecode. */
    bool framesFromTc(const std::string &tc, const TcRate &r, long long &frames);

    inline TcRate TcRate::of(double fps)
    {
        TcRate r;
        r.fps = fps > 0 ? fps : 24.0;
        r.nominal = (int)std::lround(r.fps);
        // 29.97 and 59.94 count drop-frame; 23.976 does not (it has no drop-frame form)
        r.drop = std::fabs(r.fps - 30000.0 / 1001.0) < 1e-3 || std::fabs(r.fps - 60000.0 / 1001.0) < 1e-3;
        return r;
    }

    inline std::string tcFromFrames(long long frames, const TcRate &r)
    {
        const bool neg = frames < 0;
        if (neg) frames = -frames;
        const int n = std::max(1, r.nominal);
        if (r.drop)
        {
            const int d = n == 60 ? 4 : 2;                    // frames dropped per minute
            const long long per10 = (long long)n * 600 - d * 9, perMin = (long long)n * 60 - d;
            const long long tens = frames / per10, rem = frames % per10;
            frames += d * 9 * tens + (rem > d ? d * ((rem - d) / perMin) : 0);
        }
        const long long ff = frames % n, totalS = frames / n;
        char b[32];
        std::snprintf(b, sizeof b, "%s%02lld:%02lld:%02lld%c%02lld", neg ? "-" : "", totalS / 3600, (totalS / 60) % 60, totalS % 60, r.drop ? ';' : ':', ff);
        return b;
    }

    inline bool framesFromTc(const std::string &tc, const TcRate &r, long long &frames)
    {
        int h = 0, m = 0, s = 0, f = 0;
        char c1 = 0, c2 = 0, c3 = 0;
        if (std::sscanf(tc.c_str(), "%d%c%d%c%d%c%d", &h, &c1, &m, &c2, &s, &c3, &f) != 7) return false;
        if (h < 0 || m < 0 || m > 59 || s < 0 || s > 59 || f < 0) return false;
        const int n = std::max(1, r.nominal);
        frames = ((long long)h * 3600 + m * 60 + s) * n + f;
        if (r.drop || c3 == ';' || c3 == ',')
        {
            const int d = n == 60 ? 4 : 2;
            const long long minutes = (long long)h * 60 + m;
            if (TcRate::of(r.fps).drop) frames -= d * (minutes - minutes / 10);
        }
        return true;
    }

}
}
}
