#include "WaveDisplay.h"
#include <cmath>
#include <vector>

namespace arstro
{
namespace pulsar
{
    using namespace artboard;

    namespace
    {
        constexpr double kPi = 3.14159265358979323846;

        double shapeSample(int shape, double ph)
        {
            double q = std::fmod(ph, 2 * kPi);
            if (q < 0) q += 2 * kPi;
            switch (shape)
            {
            case WaveDisplay::Sine: return std::sin(ph);
            case WaveDisplay::Triangle: return std::asin(std::sin(ph)) * (2.0 / kPi);
            // Saw with the discontinuity in the MIDDLE: 0 → +1 over the first half,
            // jump to −1, then −1 → 0 over the second half.
            case WaveDisplay::Saw: return q < kPi ? q / kPi : q / kPi - 2.0;
            case WaveDisplay::Square: return std::sin(ph) >= 0 ? 1.0 : -1.0;
            default: return std::sin(ph);
            }
        }
    }

    void WaveDisplay::setShape(int shape)
    {
        mShape = shape;
        if (shape == Morph3D)
            mFree = true;
        else
        {
            mFree = false;
            mPosTarget = (double)shape / 3.0; // snap to the shape's morph stop
        }
    }

    void WaveDisplay::setPosition(double pos01)
    {
        double p = pos01 < 0 ? 0 : (pos01 > 1 ? 1 : pos01);
        if (mFree)
            mPosTarget = p;
    }

    void WaveDisplay::setPhase(double turns01)
    {
        mPhaseTarget = turns01 < 0 ? 0 : (turns01 > 1 ? 1 : turns01);
    }

    double WaveDisplay::sample(double ph) const
    {
        ph += mPhaseDisplay * 2 * kPi; // phase shift
        const double seg = mPosDisplay * 3.0; // 0..3 over [sine,tri,saw,square]
        int i = (int)seg;
        if (i < 0) i = 0;
        if (i > 2) i = 2;
        const double f = seg - i;
        return shapeSample(i, ph) * (1.0 - f) + shapeSample(i + 1, ph) * f;
    }

    void WaveDisplay::advance(double nowMs)
    {
        double dt = mLastMs < 0.0 ? 0.0 : (nowMs - mLastMs) / 1000.0;
        mLastMs = nowMs;
        if (dt > 0.0)
        {
            if (dt > 0.05) dt = 0.05;
            const double omega = 16.0; // critically damped morph + phase
            double acc = -2.0 * omega * mVel - omega * omega * (mPosDisplay - mPosTarget);
            mVel += acc * dt;
            mPosDisplay += mVel * dt;
            acc = -2.0 * omega * mPhaseVel - omega * omega * (mPhaseDisplay - mPhaseTarget);
            mPhaseVel += acc * dt;
            mPhaseDisplay += mPhaseVel * dt;
        }
        Segment::advance(nowMs);
    }

    void WaveDisplay::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, 6.0,
                        Paint::filledStroked(Color{0, 0, 0, 0.35}, Color{1, 1, 1, 0.08}, 1.0));
        const double midY = h * 0.5;
        t.beginPath(); t.moveTo(6, midY); t.lineTo(w - 6, midY);
        t.setStroke(Color{1, 1, 1, 0.08}, 1.0); t.strokePath();

        const double amp = h * 0.40;
        std::vector<Point> pts;
        for (double x = 6; x <= w - 6; x += 1.5)
        {
            double ph = (x - 6) / (w - 12) * 2 * kPi;
            pts.push_back({x, midY - sample(ph) * amp});
        }
        if (pts.size() < 2) return;
        // soft glow (three widening, fading passes) + crisp core
        const double passW[3] = {6.0, 4.0, 2.6};
        const double passA[3] = {0.07, 0.13, 0.24};
        for (int pass = 0; pass < 3; ++pass)
        {
            t.beginPath(); t.moveTo(pts[0].x, pts[0].y);
            for (size_t i = 1; i < pts.size(); ++i) t.lineTo(pts[i].x, pts[i].y);
            t.setStroke(Color{mColor.r, mColor.g, mColor.b, passA[pass]}, passW[pass]);
            t.strokePath();
        }
        t.beginPath(); t.moveTo(pts[0].x, pts[0].y);
        for (size_t i = 1; i < pts.size(); ++i) t.lineTo(pts[i].x, pts[i].y);
        t.setStroke(mColor, 2.0); t.strokePath();
    }
}
}
