#include "LfoPanel.h"
#include "Chrome.h"
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

        // deterministic [-1,1] value held per step index (sample & hold, no RNG)
        double held(double step)
        {
            double s = std::sin(std::floor(step) * 12.9898) * 43758.5453;
            return (s - std::floor(s)) * 2.0 - 1.0;
        }

        double shape(int s, double p01)
        {
            const double ph = p01 * 2.0 * kPi;
            switch (s)
            {
            case 0: return std::sin(ph);
            case 1: return std::asin(std::sin(ph)) * (2.0 / kPi);
            case 2: return 1.0 - 2.0 * p01;                 // saw (down)
            case 3: return std::sin(ph) >= 0 ? 1.0 : -1.0;  // square
            default: return held(p01 * 8.0);                // sample & hold (8 steps/cycle)
            }
        }
    }

    LfoPanel::LfoPanel(const Theme &theme, const Color &accent) : mAccent(accent)
    {
        width.set(340.0);
        height.set(230.0);

        mShape = std::make_shared<ComboBox>(theme.combo);
        mShape->setOptions({"SINE", "TRI", "SAW", "SQUARE", "S&H"});
        mShape->setSelectedIndex(0);
        mShape->x.set(14.0); mShape->y.set(34.0);
        mShape->width.set(312.0); mShape->height.set(24.0);
        mShape->onChange = [this](int idx) { mShapeTarget = (double)idx; };
        addChild(mShape);

        mRateKnob = std::make_shared<Knob>(theme.knob);
        mRateKnob->label = "rate";
        mRateKnob->setRange(0.0, 1.0); mRateKnob->setValue(mRate); mRateKnob->setDefault(mRate);
        mRateKnob->x.set(109.0); mRateKnob->y.set(162.0);
        mRateKnob->width.set(58.0); mRateKnob->height.set(48.0);
        mRateKnob->onChange = [this](double v) { mRate = v; };
        addChild(mRateKnob);

        mDepthKnob = std::make_shared<Knob>(theme.knob);
        mDepthKnob->label = "depth";
        mDepthKnob->setRange(0.0, 1.0); mDepthKnob->setValue(mDepth); mDepthKnob->setDefault(mDepth);
        mDepthKnob->x.set(173.0); mDepthKnob->y.set(162.0);
        mDepthKnob->width.set(58.0); mDepthKnob->height.set(48.0);
        mDepthKnob->onChange = [this](double v) { mDepth = v; };
        addChild(mDepthKnob);
    }

    double LfoPanel::lfoSample(double p01) const
    {
        const double seg = mShapeDisp;
        int i = (int)seg;
        if (i < 0) i = 0;
        if (i > 3) i = 3;
        const double f = seg - i;
        return shape(i, p01) * (1.0 - f) + shape(i + 1, p01) * f;
    }

    void LfoPanel::advance(double nowMs)
    {
        double dt = mLastMs < 0.0 ? 0.0 : (nowMs - mLastMs) / 1000.0;
        mLastMs = nowMs;
        if (dt > 0.0)
        {
            if (dt > 0.05) dt = 0.05;
            const double omega = 16.0;
            const double acc = -2.0 * omega * mShapeVel - omega * omega * (mShapeDisp - mShapeTarget);
            mShapeVel += acc * dt; mShapeDisp += mShapeVel * dt;
            const double hz = 0.2 + mRate * 5.0; // 0.2 .. 5.2 Hz
            mPhase += dt * hz;
            mPhase -= std::floor(mPhase);
        }
        Segment::advance(nowMs);
    }

    void LfoPanel::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawPanelChrome(t, w, h, mAccent, "LFO");

        const double bx = 14.0, by = 64.0, bw = w - 28.0, bh = 90.0;
        drawRoundedRect(t, Rect{bx, by, bw, bh}, 6.0,
                        Paint::filledStroked(Color{0, 0, 0, 0.35}, Color{1, 1, 1, 0.08}, 1.0));
        const double midY = by + bh * 0.5, amp = bh * 0.38 * (0.15 + 0.85 * mDepth);
        const double x0 = bx + 8, x1 = bx + bw - 8;

        t.beginPath(); t.moveTo(x0, midY); t.lineTo(x1, midY);
        t.setStroke(Color{1, 1, 1, 0.08}, 1.0); t.strokePath();

        std::vector<Point> pts;
        for (double x = x0; x <= x1; x += 1.5)
        {
            const double p = (x - x0) / (x1 - x0) * 2.0; // two cycles
            pts.push_back({x, midY - lfoSample(p - std::floor(p)) * amp});
        }
        if (pts.size() < 2) return;
        const double passW[3] = {6.0, 4.0, 2.5};
        const double passA[3] = {0.06, 0.12, 0.22};
        for (int pass = 0; pass < 3; ++pass)
        {
            t.beginPath(); t.moveTo(pts[0].x, pts[0].y);
            for (size_t i = 1; i < pts.size(); ++i) t.lineTo(pts[i].x, pts[i].y);
            t.setStroke(Color{mAccent.r, mAccent.g, mAccent.b, passA[pass]}, passW[pass]);
            t.strokePath();
        }
        t.beginPath(); t.moveTo(pts[0].x, pts[0].y);
        for (size_t i = 1; i < pts.size(); ++i) t.lineTo(pts[i].x, pts[i].y);
        t.setStroke(mAccent, 2.0); t.strokePath();

        // playhead dot cycling at the rate (first of the two drawn cycles)
        const double px = x0 + (mPhase * 0.5) * (x1 - x0);
        const double py = midY - lfoSample(mPhase) * amp;
        drawCircle(t, px, py, 6.0, Paint::filled(Color{mAccent.r, mAccent.g, mAccent.b, 0.18}));
        drawCircle(t, px, py, 3.2, Paint::filled(Color{1, 1, 1, 0.95}));
    }
}
}
