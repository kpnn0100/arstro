#include "IconButton.h"

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
        if (mPressed && hoverBg.a > 0.0)
            drawRoundedRect(t, Rect{0, 0, w, h}, 2.0, Paint::filled(hoverBg));
        const Color glyph = active ? activeColor : idleColor;
        if (mPainter) mPainter(t, Rect{0, 0, w, h}, glyph);
    }
}
}
