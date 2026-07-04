/*
 *  Cosmo by arstro — IconButton: a small square button that draws a vector glyph
 *  (currently the rotate-left / rotate-right circular arrows) instead of a text
 *  label, for a cleaner toolbar. Click fires onClick; a press dims the background.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <functional>

namespace arstro
{
namespace cosmo
{
    class IconButton : public artboard::Segment
    {
    public:
        enum class Icon { RotateCCW, RotateCW, Trash, Sidebar };
        IconButton(Icon icon, const artboard::Color &accent);

        std::function<void()> onClick;
        /** Override the background + glyph colours (e.g. a pastel-red delete). */
        void setColors(const artboard::Color &bg, const artboard::Color &icon) { mBg = bg; mIconColor = icon; mCustom = true; }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        Icon mIcon;
        artboard::Color mAccent;
        artboard::Color mBg, mIconColor;
        bool mCustom = false;
        bool mPressed = false;
    };
}
}
