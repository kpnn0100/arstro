/*
 *  cosmo_v2 by arstro — PillButton: a small labeled clickable segment with an
 *  idle/active style pair. The Figma design reuses this exact shape for the
 *  top-bar menu items, the before/after switch, every segmented picker
 *  (RGB/R/G/B, Hue/Sat/Lum, Shadows/Midtones/Highlights), and the outline
 *  chips (aspect ratio, mask mode, flip/auto) -- only the styling differs
 *  (filled-pill "active" vs bordered-outline "active"), so callers set their
 *  own BoxStyle/TextStyle pairs rather than this class assuming one look.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <functional>
#include <string>

namespace arstro
{
namespace cosmo_v2
{
    class PillButton : public artboard::Segment
    {
    public:
        explicit PillButton(std::string label) : mLabel(std::move(label)) {}

        void setLabel(const std::string &label) { mLabel = label; }
        const std::string &label() const { return mLabel; }

        bool active = false;
        artboard::BoxStyle idleBox, activeBox;
        artboard::TextStyle idleText, activeText;
        double cornerRadius = 2.0;  // used only if idleBox/activeBox paint has no fill/stroke set

        std::function<void()> onClick;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        std::string mLabel;
    };
}
}
