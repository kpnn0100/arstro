/*
 *  interstellar_v1 — StatePill: a state said in words, with the way out of it as its button.
 *
 *  cosmo's "NODE BYPASSED" pill (R-BYPASS-4's look: `secondary` plate, hairline border, a glyph, 9 px
 *  Medium caps) is a label painted in an overlay. A pinned version needs the same pill to be
 *  CLICKABLE — "COLOUR PINNED @a41c9e2 · Unpin" (R-UI-18a) — and a painted overlay cannot take a
 *  click from the panels under it. So this is that pill as a Segment: the owner adds it LAST (above
 *  what it covers), gives it a box, and drives its `opacity` from its own eased amount; the pill
 *  centres itself in the box at its measured width and takes the pointer only inside the plate.
 *  Hover is the framework's `hoverAmount()` (design rule §5: one clickable Segment).
 */
#pragma once
#include "../Theme.h"
#include "Glyphs.h"
#include "TextFit.h"
#include <functional>
#include <string>

namespace arstro
{
namespace interstellar_v1
{
    class StatePill : public artboard::Segment
    {
    public:
        static constexpr double kHeight = 19.0;   // cosmo's bypass pill

        /** `state` in muted caps, `action` after a middle dot in the foreground ("Unpin"). */
        void setText(const std::string &state, const std::string &action) { mState = state; mAction = action; }
        const std::string &state() const { return mState; }
        /** The plate as last drawn (local) — where a click lands. Empty before the first paint. */
        artboard::Rect plateRect() const { return mPlate; }
        std::function<void()> onAction;

    protected:
        // drawn in the OVERLAY pass: the owner's scrim is an overlay too, and the pill sits on it
        void onOverlay(artboard::IRenderTarget &t) const override
        {
            using namespace artboard;
            const double px = 9.0, padX = 9.0;
            const std::string dot = "  \xC2\xB7  ";
            const double sw = t.measureText(mState, px, font::sansMedium());
            const double dw = mAction.empty() ? 0.0 : t.measureText(dot, px, font::sansMedium());
            const double aw = mAction.empty() ? 0.0 : t.measureText(mAction, px, font::sansSemiBold());
            const double pw = std::min(width.value(), padX + 11.0 + sw + dw + aw + padX);
            mPlate = Rect{(width.value() - pw) * 0.5, (height.value() - kHeight) * 0.5, pw, kHeight};
            const double hv = hoverAmount();
            drawRoundedRect(t, mPlate, radius::control(), Paint::filledStroked(palette::secondary(), lerpColor(palette::border(), palette::primary(), 0.7 * hv), 1.0));
            if (hv > 0.001) drawRoundedRect(t, mPlate, radius::control(), Paint::filled(palette::hoverWash(hv)));
            glyph::lock(t, Rect{mPlate.x + padX - 1.0, mPlate.y + (kHeight - 10.0) * 0.5, 8.0, 10.0}, palette::whiteAlpha(0.6));
            const double by = textfit::baseline(mPlate.y + kHeight * 0.5, px);
            double x = mPlate.x + padX + 11.0;
            const double room = mPlate.right() - padX - x - dw - aw;
            t.setFill(palette::whiteAlpha(0.66));
            t.drawText(textfit::ellipsize(t, mState, std::max(0.0, room), px, font::sansMedium()), x, by, px, font::sansMedium());
            if (mAction.empty()) return;
            x = mPlate.right() - padX - aw - dw;
            t.setFill(palette::whiteAlpha(0.4));
            t.drawText(dot, x, by, px, font::sansMedium());
            t.setFill(lerpColor(palette::foreground(), palette::white(), hv));
            t.drawText(mAction, x + dw, by, px, font::sansSemiBold());
        }
        bool hitTestSelf(const artboard::Point &p) const override { return mPlate.contains(p); }
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override
        {
            if (g.type == artboard::Gesture::Type::Down) return true;
            if (g.type == artboard::Gesture::Type::Click) { if (onAction) onAction(); return true; }
            return Segment::handleGesture(g, local);
        }

    private:
        std::string mState, mAction;
        mutable artboard::Rect mPlate{0, 0, 0, 0};
    };
}
}
