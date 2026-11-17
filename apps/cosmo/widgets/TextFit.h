/*
 *  cosmo_v2 by arstro — TextFit: ellipsize a string to a pixel width with REAL metrics (R5).
 *
 *  `estimateTextWidth` (TextMetrics.h) is `chars * px * 0.6` — font-independent, and so wrong
 *  for every actual font; it is fine for a layout guess and wrong for "does this fit". These
 *  helpers ask the render target, which is why they take one: measurement is only valid during
 *  a paint (the design law, §R5). Cuts happen on UTF-8 code-point boundaries, so a name in
 *  Vietnamese is never left with half a letter before the ellipsis.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include <string>

namespace arstro
{
namespace cosmo_v2
{
    /** `s` if it fits `maxW`, else its start plus "…" (empty if not even "…" fits). */
    inline std::string fitTextEnd(const artboard::IRenderTarget &t, const std::string &s, double maxW,
                                  double px, const char *family, double tracking = 0.0)
    {
        if (maxW <= 0.0) return std::string();
        if (t.measureText(s, px, family, tracking) <= maxW) return s;
        static const char *const kDots = "\xE2\x80\xA6";
        std::string cut = s;
        while (!cut.empty())
        {
            // One whole code point: its continuation bytes (10xxxxxx), then its lead byte.
            while (!cut.empty() && ((unsigned char)cut.back() & 0xC0) == 0x80) cut.pop_back();
            if (!cut.empty()) cut.pop_back();
            while (!cut.empty() && cut.back() == ' ') cut.pop_back();
            if (t.measureText(cut + kDots, px, family, tracking) <= maxW) return cut.empty() ? std::string() : cut + kDots;
        }
        return std::string();
    }

    /** `s` if it fits `maxW`, else "…" plus its END — for a path, whose tail is the part that
     *  tells two apart. */
    inline std::string fitTextFront(const artboard::IRenderTarget &t, const std::string &s, double maxW,
                                    double px, const char *family, double tracking = 0.0)
    {
        if (maxW <= 0.0) return std::string();
        if (t.measureText(s, px, family, tracking) <= maxW) return s;
        static const char *const kDots = "\xE2\x80\xA6";
        size_t from = 0;
        while (from < s.size())
        {
            ++from;
            while (from < s.size() && ((unsigned char)s[from] & 0xC0) == 0x80) ++from;
            const std::string cand = kDots + s.substr(from);
            if (t.measureText(cand, px, family, tracking) <= maxW) return from >= s.size() ? std::string() : cand;
        }
        return std::string();
    }
}
}
