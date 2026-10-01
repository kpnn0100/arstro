/*
 *  interstellar_v1 — TextFit: measured text, ellipsized against the space actually left (R5).
 *
 *  The design rule's R5 names the primitive: `IRenderTarget::measureText`, which Cairo answers
 *  with real metrics. cosmo's `estimateTextWidth` (len × px × 0.6) is font-independent and so
 *  wrong for any real font — it is what once detached cosmo's wordmark dot. Every string this app
 *  places against something else goes through here instead.
 *
 *  Measurement is only valid DURING A RENDER (the target's context exists only then), so these
 *  take the target that `onPaint` was handed. A widget that needs a width outside paint — a hit
 *  rect, a layout — caches what its last paint measured, and says so where it does.
 *
 *  Ellipsizing walks UTF-8 code points, never bytes: "Night Ferry — Day 3" holds a three-byte em
 *  dash, and cutting through it draws a replacement glyph.
 */
#pragma once
#include "../../../../core/Artboard/include/artboard/artboard.h"
#include <string>

namespace arstro
{
namespace interstellar_v1
{
namespace textfit
{
    /** Byte offsets of each code point boundary in `s` (including the end). */
    inline int prevBoundary(const std::string &s, int i)
    {
        if (i <= 0) return 0;
        --i;
        while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) --i;
        return i;
    }

    inline double width(const artboard::IRenderTarget &t, const std::string &s, double px,
                        const char *family, double tracking = 0.0)
    {
        return t.measureText(s, px, family, tracking);
    }

    /** `s`, or the longest prefix of it plus "…" that fits `maxW`. Empty when even "…" does not
     *  fit — nothing is better than a glyph drawn over a neighbour. */
    inline std::string ellipsize(const artboard::IRenderTarget &t, const std::string &s, double maxW,
                                 double px, const char *family, double tracking = 0.0)
    {
        if (maxW <= 0.0) return std::string();
        if (t.measureText(s, px, family, tracking) <= maxW) return s;
        static const std::string kEll = "\xE2\x80\xA6";
        if (t.measureText(kEll, px, family, tracking) > maxW) return std::string();
        // Binary search over the code-point boundaries: the largest prefix whose "prefix…" fits.
        int bounds[512];
        int n = 0;
        for (int i = 0; i < (int)s.size() && n < 511; ++i)
            if (((unsigned char)s[i] & 0xC0) != 0x80) bounds[n++] = i;
        int lo = 0, hi = n - 1;   // index into bounds; bounds[0] == 0 (empty prefix always fits)
        while (lo < hi)
        {
            const int mid = (lo + hi + 1) / 2;
            if (t.measureText(s.substr(0, bounds[mid]) + kEll, px, family, tracking) <= maxW) lo = mid;
            else hi = mid - 1;
        }
        std::string head = s.substr(0, n > 0 ? bounds[lo] : 0);
        while (!head.empty() && head.back() == ' ') head.pop_back();   // no "Night …"
        return head + kEll;
    }

    /** The largest size ≤ `maxPx` (and ≥ `minPx`) at which `s` fits `maxW` — sized-to-fit, for a
     *  wordmark whose column is narrower than its design size. Tracking scales with the size. */
    inline double fitSize(const artboard::IRenderTarget &t, const std::string &s, double maxW,
                          double maxPx, double minPx, const char *family, double trackingPerPx = 0.0)
    {
        const double w = t.measureText(s, maxPx, family, trackingPerPx * maxPx);
        if (w <= maxW || w <= 0.0) return maxPx;
        const double px = maxPx * maxW / w;
        return px < minPx ? minPx : px;
    }

    /** Baseline for text vertically centred on `centerY` — the universal 0.35 rule. */
    inline double baseline(double centerY, double px) { return centerY + px * 0.35; }
}
}
}
