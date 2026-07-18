#include "ProjectCard.h"
#include "TextMetrics.h"

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    void drawProjectCardChrome(IRenderTarget &t, const Rect &s, const ProjectCardData &d,
                               double hv, double alpha)
    {
        if (alpha <= 0.001)
            return;
        auto fa = [&](Color c) { c.a *= alpha; return c; };  // fade the whole card by alpha
        const double th = s.w * 9.0 / 16.0;                  // 16:9 thumbnail band

        // Card border + surface; hover brightens the surface + lifts the border (R-G-1).
        const Color cardBg = brighten(palette::folderChipBg(), 0.06 * hv);
        const Color cardBorder = lerpColor(palette::border(), palette::primaryAlpha(0.7), hv);
        drawRoundedRect(t, s, radius::control(), Paint::filledStroked(fa(cardBg), fa(cardBorder), 1.0 + 0.5 * hv));
        // Thumbnail placeholder (shows through until the real thumbnail is drawn over it).
        drawRoundedRect(t, Rect{s.x, s.y, s.w, th}, 0.0, Paint::filled(fa(Color::hex(0x111111))));

        if (d.edited)
        {
            const double bw = estimateTextWidth("Edited", 8.0) + 10.0;
            const Rect badge{s.x + s.w - bw - 8.0, s.y + 8.0, bw, 14.0};
            drawRoundedRect(t, badge, 1.0, Paint::filledStroked(fa(Color{0, 0, 0, 0.6}),
                            fa(Color{palette::primary().r, palette::primary().g, palette::primary().b, 0.3}), 1.0));
            t.setFill(fa(palette::primary()));
            t.drawText("Edited", badge.x + 5.0, badge.y + 10.0, 8.0, font::sansSemiBold());
        }

        // Meta band: name, then "photos · size" with a right-aligned date.
        const double mx = s.x + 12.0;
        double my = s.y + th + 18.0;
        t.setFill(fa(palette::foreground()));
        t.drawText(d.name, mx, my, 11.0, font::sansMedium());
        my += 15.0;
        t.setFill(fa(palette::mutedForeground()));
        std::string metaLeft = d.photos;
        if (!d.size.empty())
            metaLeft += "  \xC2\xB7  " + d.size;
        t.drawText(metaLeft, mx, my, 10.0, font::sans());
        t.setFill(fa(Color{palette::mutedForeground().r, palette::mutedForeground().g, palette::mutedForeground().b, 0.6}));
        t.drawText(d.date, s.x + s.w - 12.0 - estimateTextWidth(d.date, 9.0), my, 9.0, font::sans());
    }
}
}
