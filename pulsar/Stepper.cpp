#include "Stepper.h"
#include <cmath>
#include <cstdio>

namespace arstro
{
namespace pulsar
{
    using namespace artboard;

    Stepper::Stepper()
    {
        focusable = true;
        width.set(88.0);
        height.set(54.0);
    }

    void Stepper::setRange(int lo, int hi)
    {
        mMin = lo;
        mMax = hi < lo ? lo : hi;
        apply(mValue);
    }
    void Stepper::setValue(int v) { apply(v); }

    void Stepper::apply(int v)
    {
        if (v < mMin) v = mMin;
        if (v > mMax) v = mMax;
        if (v == mValue) return;
        mValue = v;
        if (onChange) onChange(mValue);
    }

    void Stepper::onPaint(IRenderTarget &t) const
    {
        const double w = width.value();
        const double boxH = height.value() - 14.0; // leave room for the label
        const double arrowW = 18.0;
        // box
        drawRoundedRect(t, Rect{0, 0, w, boxH}, 5.0,
                        Paint::filledStroked(Color{0, 0, 0, 0.35},
                                             Color{mColor.r, mColor.g, mColor.b, 0.5}, 1.0));
        // value (left of the arrows)
        char buf[8]; std::snprintf(buf, sizeof buf, "%d", mValue);
        std::string s = buf;
        t.setFill(mColor);
        t.drawText(s, 10.0, boxH * 0.5 + 7.0, 20.0);
        // arrow divider + up/down arrows on the right
        const double ax = w - arrowW;
        t.beginPath(); t.moveTo(ax, 3); t.lineTo(ax, boxH - 3);
        t.setStroke(Color{1, 1, 1, 0.12}, 1.0); t.strokePath();
        const double cx = ax + arrowW * 0.5;
        // up
        t.beginPath();
        t.moveTo(cx - 4, boxH * 0.5 - 2); t.lineTo(cx + 4, boxH * 0.5 - 2); t.lineTo(cx, boxH * 0.5 - 8); t.closePath();
        t.setFill(Color{mColor.r, mColor.g, mColor.b, 0.85}); t.fillPath();
        // down
        t.beginPath();
        t.moveTo(cx - 4, boxH * 0.5 + 2); t.lineTo(cx + 4, boxH * 0.5 + 2); t.lineTo(cx, boxH * 0.5 + 8); t.closePath();
        t.setFill(Color{mColor.r, mColor.g, mColor.b, 0.85}); t.fillPath();
        // label below (matches the knobs)
        t.setFill(Color{1, 1, 1, 0.35});
        t.drawText(mLabel, w * 0.5 - mLabel.size() * 8.0 * 0.28, height.value() - 1.0, 8.0);
    }

    bool Stepper::handleGesture(const Gesture &g, const Point &localPoint)
    {
        using T = Gesture::Type;
        const double boxH = height.value() - 14.0;
        const double arrowX = width.value() - 18.0;
        if (g.type == T::Down)
        {
            if (localPoint.x >= arrowX && localPoint.y <= boxH)
            {
                mArrowMode = true;
                apply(mValue + (localPoint.y < boxH * 0.5 ? 1 : -1)); // up / down arrow
            }
            else
            {
                mArrowMode = false;
                mDragStartVal = mValue;
                mDragStartY = g.pos.y;
            }
            return true;
        }
        if (g.type == T::Drag && !mArrowMode)
        {
            int delta = (int)std::lround((mDragStartY - g.pos.y) / 14.0); // 14px per step
            apply(mDragStartVal + delta);
            return true;
        }
        return Segment::handleGesture(g, localPoint);
    }
}
}
