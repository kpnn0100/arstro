/*
 *  Pulsar by arstro — MuteButton: a small square "audio on" checkbox. Default is
 *  checked (audio on); clicking toggles it. When checked, an inner square grows
 *  inside the box (not touching the border); when unchecked it shrinks to nothing.
 *  The inner size eases, so the toggle animates smoothly.
 */
#pragma once
#include "../../core/Artboard/include/artboard/artboard.h"
#include <functional>

namespace arstro
{
namespace pulsar
{
    class MuteButton : public artboard::Segment
    {
    public:
        MuteButton();

        std::function<void(bool)> onChange; // argument = audio ON (true) / muted (false)
        bool audioOn() const { return mOn; }
        void setColor(const artboard::Color &c) { mColor = c; }

        void advance(double nowMs) override; // eases the inner-square size

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &localPoint) override;

    private:
        bool mOn = true;        // checked = audio on
        double mFill = 1.0;     // inner square size 0..1 (eased toward mOn)
        double mLastMs = -1.0;
        artboard::Color mColor = artboard::Color::hex(0x4de2ff);
    };
}
}
