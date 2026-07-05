#include "PillButton.h"
#include "TextMetrics.h"

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    bool PillButton::handleGesture(const Gesture &g, const Point &local)
    {
        if (g.type == Gesture::Type::Click) { if (onClick) onClick(); return true; }
        return Segment::handleGesture(g, local);
    }

    void PillButton::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        const BoxStyle &box = active ? activeBox : idleBox;
        const TextStyle &text = active ? activeText : idleText;

        if (box.paint.hasFill || box.paint.hasStroke)
            drawRoundedRect(t, Rect{0, 0, w, h}, box.cornerRadius, box.paint);

        const double tw = estimateTextWidth(mLabel, text.sizePx);
        const double tx = (w - tw) * 0.5;
        const double ty = h * 0.5 + text.sizePx * 0.35;
        t.setFill(text.color);
        t.drawText(mLabel, tx, ty, text.sizePx, text.fontFamily, text.letterSpacingPx);
    }
}
}
