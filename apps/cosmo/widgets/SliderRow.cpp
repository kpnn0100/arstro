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
    void SliderRow::setSubValueOffset(double offset) { mSlider->setSubValueOffset(offset); }
    void SliderRow::setTrackGradient(const Color &left, const Color &right) { mSlider->setTrackGradient(left, right); }

    Rect SliderRow::keyRect() const { return Rect{width.value() - kKeyGutter, 0.0, kKeyGutter, kRowHeight}; }

    void SliderRow::advance(double nowMs)
    {
        if (mKeyGutter && mKeyWanted != mKeyApplied)
        {
            const double fill = mKeyWanted == 2 ? 1.0 : 0.0, line = mKeyWanted == 0 ? 0.0 : 1.0;
            if (mKeyApplied < 0) { mKeyFill.set(fill); mKeyLine.set(line); }   // first placement: nowhere to travel from
            else
            {
                mKeyFill.animateTo(fill, 180.0, Easing::EaseOutCubic, nowMs);
                mKeyLine.animateTo(line, 180.0, Easing::EaseOutCubic, nowMs);
            }
            mKeyApplied = mKeyWanted;
        }
        mKeyFill.update(nowMs);
        mKeyLine.update(nowMs);
        Segment::advance(nowMs);
    }

    bool SliderRow::handleGesture(const Gesture &g, const Point &local)
    {
        if (mKeyGutter && g.type == Gesture::Type::Click && keyRect().contains(local))
        {
            if (onKeyClick) onKeyClick();
            return true;
        }
        return Segment::handleGesture(g, local);
    }

    void SliderRow::layout()
    {
        const double w = width.value() - (mKeyGutter ? kKeyGutter : 0.0);
        const double trackW = w - kLabelWidth - kGap - mValueWidth - kGap;
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
        std::string text;
        if (formatValue) text = formatValue(v);
        else
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), v > 0 ? "+%d" : "%d", (int)std::lround(v));
            text = buf;
        }
        const double vw = width.value() - (mKeyGutter ? kKeyGutter : 0.0);
        const double tx = vw - estimateTextWidth(text, 10.0);
        t.setFill(palette::mutedForeground());
        t.drawText(text, tx, h * 0.5 + 10.0 * 0.35, 10.0, font::mono());
        if (mKeyGutter)
        {
            // the diamond: a faint outline (no curve) → an outline (animated) → filled (a key here)
            const double cx = width.value() - kKeyGutter * 0.5, cy = h * 0.5, r = 4.0;
            const double line = mKeyLine.value(), fill = mKeyFill.value();
            const Color outline = lerpColor(palette::whiteAlpha(0.22), palette::foreground(), line);
            auto diamond = [&] {
                t.beginPath();
                t.moveTo(cx, cy - r); t.lineTo(cx + r, cy); t.lineTo(cx, cy + r); t.lineTo(cx - r, cy); t.closePath();
            };
            if (fill > 0.01) { diamond(); t.setFill(palette::primaryAlpha(fill)); t.fillPath(); }
            diamond();
            t.setStroke(outline, 1.0);
            t.strokePath();
        }
    }
}
}
