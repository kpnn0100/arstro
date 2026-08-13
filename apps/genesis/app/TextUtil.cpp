#include "TextUtil.h"
#include "Theme.h"

namespace genesis
{
namespace ui
{
    namespace
    {
        artboard::IRenderTarget *&slot()
        {
            static artboard::IRenderTarget *t = nullptr;
            return t;
        }
        /** Headless fallback: the same estimate IRenderTarget's default uses. */
        double estimate(const std::string &text, double sizePx, double tracking)
        {
            size_t glyphs = 0;
            for (unsigned char c : text)
                if ((c & 0xC0) != 0x80) ++glyphs;
            double w = (double)glyphs * sizePx * 0.5;
            if (glyphs > 1) w += (double)(glyphs - 1) * tracking;
            return w;
        }
    }

    void setMeasureTarget(artboard::IRenderTarget *t) { slot() = t; }

    double textWidth(const std::string &text, double sizePx, const char *family, double tracking)
    {
        const std::string fam = family ? family : font::sans();
        if (artboard::IRenderTarget *t = slot())
            return t->measureText(text, sizePx, fam, tracking);
        return estimate(text, sizePx, tracking);
    }

    std::string ellipsize(const std::string &text, double maxPx, double sizePx,
                          const char *family, double tracking)
    {
        if (maxPx <= 0.0) return std::string();
        if (textWidth(text, sizePx, family, tracking) <= maxPx) return text;

        const std::string dots = "…";
        if (textWidth(dots, sizePx, family, tracking) > maxPx) return std::string();

        // Trim whole UTF-8 codepoints so a multi-byte glyph is never cut in half.
        std::string cut = text;
        while (!cut.empty())
        {
            do
                cut.pop_back();
            while (!cut.empty() && ((unsigned char)cut.back() & 0xC0) == 0x80);
            if (textWidth(cut + dots, sizePx, family, tracking) <= maxPx)
                return cut + dots;
        }
        return dots;
    }
}
}
