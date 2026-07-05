/*
 *  cosmo_v2 by arstro — IconButton: a small square button that draws a vector
 *  glyph (see Icons.h) instead of a text label. Generic over which glyph it
 *  draws (a Painter callback) so one class serves the rail toggle, mask
 *  delete icons, and the curve/transform reset buttons.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <functional>

namespace arstro
{
namespace cosmo_v2
{
    class IconButton : public artboard::Segment
    {
    public:
        using Painter = std::function<void(artboard::IRenderTarget &, const artboard::Rect &, const artboard::Color &)>;

        explicit IconButton(Painter painter) : mPainter(std::move(painter)) {}

        std::function<void()> onClick;
        bool active = false;              // e.g. the rail toggle's "open" state
        artboard::Color activeColor;
        artboard::Color idleColor;
        artboard::Color hoverBg{0, 0, 0, 0};  // background while pressed (approximates hover)

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        Painter mPainter;
        bool mPressed = false;
    };
}
}
