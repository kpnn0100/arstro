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
        const double hv = hoverAmount();  // animated 0..1 from the framework (FR-24)
        const BoxStyle &baseBox = active ? activeBox : idleBox;
        const TextStyle &baseText = active ? activeText : idleText;

        // Hover: brighten the fill + pull the border toward hoverEmphasis, eased
        // by hoverAmount() so it never pops (R-G-1). Shared treatment so every
        // pill reads identically (consistency lock).
        const BoxStyle box = hoverBox(baseBox, hoverEmphasis, hv);
        if (box.paint.hasFill || box.paint.hasStroke)
            drawRoundedRect(t, Rect{0, 0, w, h}, box.cornerRadius, box.paint);

        // An idle label lifts toward its active colour on hover; an active label stays.
        const Color textColor = active ? baseText.color
                                       : lerpColor(idleText.color, activeText.color, 0.55 * hv);
        const double tw = estimateTextWidth(mLabel, baseText.sizePx);
        const double tx = (w - tw) * 0.5;
        const double ty = h * 0.5 + baseText.sizePx * 0.35;
        t.setFill(textColor);
        t.drawText(mLabel, tx, ty, baseText.sizePx, baseText.fontFamily, baseText.letterSpacingPx);
    }
}
}
