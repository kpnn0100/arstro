#include "MuteButton.h"
#include <cmath>

namespace arstro
{
namespace pulsar
{
    using namespace artboard;

    MuteButton::MuteButton()
    {
        width.set(18.0);
        height.set(18.0);
    }

    void MuteButton::advance(double nowMs)
    {
        double dt = mLastMs < 0.0 ? 0.0 : (nowMs - mLastMs) / 1000.0;
        mLastMs = nowMs;
        if (dt > 0.0)
        {
            if (dt > 0.05) dt = 0.05;
            const double target = mOn ? 1.0 : 0.0;
            mFill += (target - mFill) * (1.0 - std::exp(-dt / 0.09)); // smooth grow/shrink
        }
        Segment::advance(nowMs);
    }

    void MuteButton::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        // outer box
        drawRoundedRect(t, Rect{0, 0, w, h}, 3.0,
                        Paint::filledStroked(Color{0, 0, 0, 0.4},
                                             Color{mColor.r, mColor.g, mColor.b, 0.7}, 1.3));
        // inner square grows inside (gap from the border), shrinks to nothing when muted
        const double pad = 4.0;
        const double maxSide = w - pad * 2.0;
        const double side = maxSide * mFill;
        if (side > 0.5)
        {
            const double off = (w - side) * 0.5;
            drawRoundedRect(t, Rect{off, off, side, side}, 2.0, Paint::filled(mColor));
        }
    }

    bool MuteButton::handleGesture(const Gesture &g, const Point &localPoint)
    {
        if (g.type == Gesture::Type::Click)
        {
            mOn = !mOn;
            if (onChange)
                onChange(mOn);
            return true;
        }
        return Segment::handleGesture(g, localPoint);
    }
}
}
