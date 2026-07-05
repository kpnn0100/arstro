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
#include "../../Artboard/include/artboard/artboard.h"
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
        std::function<void(double)> onChange;

        void layout();  // call after width changes

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        std::string mLabel;
        std::shared_ptr<artboard::Slider> mSlider;
    };
}
}
