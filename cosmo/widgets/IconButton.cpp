#include "IconButton.h"
#include "../CosmoTheme.h"
#include <cmath>

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    IconButton::IconButton(Icon icon, const Color &accent) : mIcon(icon), mAccent(accent) {}

    bool IconButton::handleGesture(const Gesture &g, const Point &local)
    {
        if (g.type == Gesture::Type::Down) { mPressed = true; return true; }
        if (g.type == Gesture::Type::Up || g.type == Gesture::Type::Drop) { mPressed = false; return true; }
        if (g.type == Gesture::Type::Click) { mPressed = false; if (onClick) onClick(); return true; }
        return Segment::handleGesture(g, local);
    }

    void IconButton::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        const Color bg = mCustom ? mBg : palette::surface();
        const Color ink = mCustom ? mIconColor : palette::ink();
        drawRoundedRect(t, Rect{0, 0, w, h}, radius::control(),
                        Paint::filled(mPressed ? palette::line() : bg));

        const double cx = w * 0.5, cy = h * 0.5;

        if (mIcon == Icon::Sidebar)
        {
            // a frame with a divider near the left third -- the standard "toggle
            // side panel" glyph.
            const double bw = std::min(w, h) * 0.62, bh = std::min(w, h) * 0.5;
            const Rect box{cx - bw * 0.5, cy - bh * 0.5, bw, bh};
            t.setStroke(ink, 1.4);
            t.beginPath();
            t.moveTo(box.x, box.y); t.lineTo(box.x + box.w, box.y);
            t.lineTo(box.x + box.w, box.y + box.h); t.lineTo(box.x, box.y + box.h);
            t.closePath();
            t.strokePath();
            const double divX = box.x + box.w * 0.38;
            t.beginPath(); t.moveTo(divX, box.y); t.lineTo(divX, box.y + box.h); t.strokePath();
            return;
        }

        if (mIcon == Icon::Trash)
        {
            const double bw = std::min(w, h) * 0.34, bh = std::min(w, h) * 0.40;
            t.setStroke(ink, 1.6);
            // lid
            t.beginPath(); t.moveTo(cx - bw, cy - bh * 0.7); t.lineTo(cx + bw, cy - bh * 0.7); t.strokePath();
            // handle
            t.beginPath(); t.moveTo(cx - bw * 0.4, cy - bh * 0.7); t.lineTo(cx - bw * 0.4, cy - bh); t.lineTo(cx + bw * 0.4, cy - bh); t.lineTo(cx + bw * 0.4, cy - bh * 0.7); t.strokePath();
            // can body
            t.beginPath();
            t.moveTo(cx - bw * 0.8, cy - bh * 0.7); t.lineTo(cx - bw * 0.65, cy + bh);
            t.lineTo(cx + bw * 0.65, cy + bh); t.lineTo(cx + bw * 0.8, cy - bh * 0.7);
            t.strokePath();
            return;
        }

        const double r = std::min(w, h) * 0.26;
        const bool cw = mIcon == Icon::RotateCW;
        // A ~290° arc with a small gap; arrowhead at the open end.
        const double gap = 0.9;                       // radians of gap (where the arrowhead sits)
        const double a0 = (cw ? -1.0 : 1.0) * gap * 0.5 - 1.5708;  // start near top
        const double sweep = (cw ? 1.0 : -1.0) * (6.2832 - gap);
        const int n = 26;
        t.setStroke(ink, 1.6);
        t.beginPath();
        for (int i = 0; i <= n; ++i)
        {
            double a = a0 + sweep * (double)i / n;
            double x = cx + r * std::cos(a), y = cy + r * std::sin(a);
            if (i == 0) t.moveTo(x, y); else t.lineTo(x, y);
        }
        t.strokePath();

        // arrowhead at the arc's end, pointing along the tangent
        const double aEnd = a0 + sweep;
        const double ex = cx + r * std::cos(aEnd), ey = cy + r * std::sin(aEnd);
        const double tang = aEnd + (cw ? 1.5708 : -1.5708);  // tangent direction
        const double ah = std::min(w, h) * 0.16;
        t.setFill(ink);
        t.beginPath();
        t.moveTo(ex + ah * std::cos(tang), ey + ah * std::sin(tang));
        t.lineTo(ex + ah * std::cos(tang + 2.4), ey + ah * std::sin(tang + 2.4));
        t.lineTo(ex + ah * std::cos(tang - 2.4), ey + ah * std::sin(tang - 2.4));
        t.closePath();
        t.fillPath();
    }
}
}
