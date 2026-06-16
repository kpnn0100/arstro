/*
 *  Pulsar by arstro — Stepper: a discrete integer control. A box shows the value;
 *  up/down arrows on the right increment/decrement, and dragging vertically over
 *  the box also changes it. A label is drawn below (to align with the knobs).
 */
#pragma once
#include "../Artboard/include/artboard/artboard.h"
#include <functional>
#include <string>

namespace arstro
{
namespace pulsar
{
    class Stepper : public artboard::Segment
    {
    public:
        Stepper();

        std::function<void(int)> onChange;
        int value() const { return mValue; }
        void setRange(int lo, int hi);
        void setValue(int v);
        void setLabel(const std::string &s) { mLabel = s; }
        void setColor(const artboard::Color &c) { mColor = c; }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &localPoint) override;

    private:
        void apply(int v);
        int mValue = 1, mMin = 1, mMax = 16;
        std::string mLabel;
        artboard::Color mColor = artboard::Color::hex(0x4de2ff);
        bool mArrowMode = false;
        int mDragStartVal = 1;
        double mDragStartY = 0.0;
    };
}
}
