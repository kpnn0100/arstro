/*
 *  cosmo_v2 by arstro — SliderRow: label + bipolar slider + value readout,
 *  the single most-repeated control in the design (App.tsx's SliderRow: `86px
 *  label, flex-1 track, value column`). Wraps Artboard's stock Slider rather
 *  than hand-rolling a track: Slider's zero-crossing range-fill behavior
 *  (FR-8 -- fills from the zero point, not the left edge, when the range
 *  spans zero) is exactly the bipolar fill App.tsx computes by hand via its
 *  own `pct`/`ctr` math, so no custom slider control is needed here.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include <functional>
#include <memory>
#include <string>

namespace arstro
{
namespace cosmo_v2
{
    class SliderRow : public artboard::Segment
    {
    public:
        static constexpr double kRowHeight = 20.0;   // py-[3.5px]*2 + ~10px line height
        static constexpr double kLabelWidth = 86.0;  // w-[86px]
        static constexpr double kValueWidth = 22.75; // w-7

        SliderRow(std::string label, double min, double max, double initial);

        void setValue(double v);  // programmatic -- does not fire onChange
        double value() const;
        /** Green "stacked reach": the amount ancestor groups add on top of this value
         *  (in the slider's value units). Draws a reach from the thumb to value+offset
         *  so the effective total is visible; 0 hides it (DR-EDIT-4 / group stacking). */
        void setSubValueOffset(double offset);
        /** Paint the track as a left->right colour ramp (e.g. temperature blue->
         *  amber, tint green->magenta) so the drag direction reads as a colour. */
        void setTrackGradient(const artboard::Color &left, const artboard::Color &right);
        std::function<void(double)> onChange;

        /** Opt-in, for an embedder that animates parameters (Interstellar's mark, its R-ANIM-9): a
         *  diamond in a gutter after the value — state 0 a faint outline, 1 an outline, 2 filled; the
         *  embedder says what they mean (Interstellar: 0 not animated, 2 marked to animate). A state
         *  change eases (the fill and the outline). Off by default: cosmo draws none and its rows lay
         *  out exactly as before. */
        static constexpr double kKeyGutter = 14.0;
        void setKeyGutter(bool on) { mKeyGutter = on; }
        void setKeyState(int state) { mKeyWanted = state; }
        int keyState() const { return mKeyWanted; }
        double keyFillAmount() const { return mKeyFill.value(); }   // the LIVE eased fill, for a test
        artboard::Rect keyRect() const;
        std::function<void()> onKeyClick;

        void layout();  // call after width changes
        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;

    private:
        std::string mLabel;
        std::shared_ptr<artboard::Slider> mSlider;
        bool mKeyGutter = false;
        int mKeyWanted = 0, mKeyApplied = -1;
        artboard::AnimatedProperty mKeyFill{0.0}, mKeyLine{0.0};
    };
}
}
