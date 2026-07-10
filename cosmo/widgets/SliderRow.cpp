#include "SliderRow.h"
#include "TextMetrics.h"
#include "../Theme.h"
#include <cmath>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace { constexpr double kGap = 8.125; }  // gap-2.5

    SliderRow::SliderRow(std::string label, double min, double max, double initial) : mLabel(std::move(label))
    {
        mSlider = std::make_shared<Slider>(sharedTheme().slider);
        mSlider->setRange(min, max);
        mSlider->setValue(initial);
        mSlider->setDefault(initial);
        mSlider->height.set(9.0);
        // Develop sliders are drag-to-set (matching the Figma design): a bare click
        // does NOT jump the value to the cursor. This also makes double-click reset
        // to default unambiguous -- there is no deferred click-jump that could fire
        // around the double-click and slide the thumb toward the cursor.
        mSlider->setClickJumps(false);
        mSlider->onChange = [this](double v) { if (onChange) onChange(v); };
        addChild(mSlider);
        height.set(kRowHeight);
    }

    void SliderRow::setValue(double v) { mSlider->setValue(v); }
    double SliderRow::value() const { return mSlider->value(); }
    void SliderRow::setTrackGradient(const Color &left, const Color &right) { mSlider->setTrackGradient(left, right); }

    void SliderRow::layout()
    {
        const double w = width.value();
        const double trackW = w - kLabelWidth - kGap - kValueWidth - kGap;
        mSlider->x.set(kLabelWidth + kGap);
        mSlider->y.set((kRowHeight - mSlider->height.value()) * 0.5);
        mSlider->width.set(std::max(0.0, trackW));
    }

    void SliderRow::onPaint(IRenderTarget &t) const
    {
        const double h = kRowHeight;
        t.setFill(palette::mutedForeground());
        t.drawText(mLabel, 0.0, h * 0.5 + 10.0 * 0.35, 10.0, font::sans());

        const double v = mSlider->value();
        char buf[32];
        std::snprintf(buf, sizeof(buf), v > 0 ? "+%d" : "%d", (int)std::lround(v));
        const std::string text(buf);
        const double vw = width.value();
        const double tx = vw - estimateTextWidth(text, 10.0);
        t.setFill(palette::mutedForeground());
        t.drawText(text, tx, h * 0.5 + 10.0 * 0.35, 10.0, font::mono());
    }
}
}
