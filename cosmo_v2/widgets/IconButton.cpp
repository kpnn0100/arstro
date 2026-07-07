#include "IconButton.h"
#include <algorithm>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

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
        const double hv = hoverAmount();  // animated 0..1 from the framework (FR-24)

        // Rounded background that eases in under the pointer and reaches full
        // strength while pressed — never a hard pop (R-G-1). Falls back to a
        // subtle white wash so every icon button gets hover feedback even when
        // the caller didn't set an explicit hoverBg.
        Color bg = hoverBg.a > 0.0 ? hoverBg : Color{1, 1, 1, 0.10};
        const double bgVis = std::max(hv, mPressed ? 1.0 : 0.0);
        if (bgVis > 0.001)
        {
            Color b = bg; b.a *= bgVis;
            drawRoundedRect(t, Rect{0, 0, w, h}, 2.0, Paint::filled(b));
        }

        // The glyph lights up toward white on hover (safe for any idle/active colour).
        const Color base = active ? activeColor : idleColor;
        const Color glyph = brighten(base, 0.4 * hv);
        if (mPainter) mPainter(t, Rect{0, 0, w, h}, glyph);
    }
}
}
