#include "SubOscPanel.h"
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

        double basicShape(int s, double ph)
        {
            switch (s)
            {
            case 0: return std::sin(ph);
            case 1: return std::asin(std::sin(ph)) * (2.0 / kPi);
            case 2: { double q = std::fmod(ph, 2 * kPi); if (q < 0) q += 2 * kPi; return q < kPi ? q / kPi : q / kPi - 2.0; }
            default: return std::sin(ph) >= 0 ? 1.0 : -1.0;
            }
        }
    }

    SubOscPanel::SubOscPanel(const Theme &theme, const Color &accent) : mAccent(accent)
    {
        width.set(514.0);
        height.set(230.0);

        mWave = std::make_shared<ComboBox>(theme.combo);
        mWave->setOptions({"SINE", "TRI", "SAW", "SQUARE"});
        mWave->setSelectedIndex(0);
        mWave->x.set(14.0); mWave->y.set(40.0);
        mWave->width.set(168.0); mWave->height.set(26.0);
        mWave->onChange = [this](int idx) { mShapeTarget = (double)idx; };
        addChild(mWave);

        mOctaveStepper = std::make_shared<Stepper>();
        mOctaveStepper->setColor(accent);
        mOctaveStepper->setRange(-2, 1);
        mOctaveStepper->setValue(mOctave);
        mOctaveStepper->setLabel("oct");
        mOctaveStepper->x.set(14.0); mOctaveStepper->y.set(96.0);
        mOctaveStepper->width.set(78.0); mOctaveStepper->height.set(54.0);
        mOctaveStepper->onChange = [this](int v) { mOctave = v; };
        addChild(mOctaveStepper);

        mLevelKnob = std::make_shared<Knob>(theme.knob);
        mLevelKnob->label = "level";
        mLevelKnob->setRange(0.0, 1.0);
        mLevelKnob->setValue(mLevel);
        mLevelKnob->setDefault(mLevel);
        mLevelKnob->x.set(108.0); mLevelKnob->y.set(98.0);
        mLevelKnob->width.set(58.0); mLevelKnob->height.set(48.0);
        mLevelKnob->onChange = [this](double v) { mLevel = v; };
        addChild(mLevelKnob);
    }

    double SubOscPanel::subSample(double p01) const
    {
        const double seg = mShapeDisplay;
        int i = (int)seg;
        if (i < 0) i = 0;
        if (i > 2) i = 2;
        const double f = seg - i;
        const double ph = p01 * 2.0 * kPi;
        return basicShape(i, ph) * (1.0 - f) + basicShape(i + 1, ph) * f;
    }

    void SubOscPanel::advance(double nowMs)
    {
        double dt = mLastMs < 0.0 ? 0.0 : (nowMs - mLastMs) / 1000.0;
        mLastMs = nowMs;
        if (dt > 0.0)
        {
            if (dt > 0.05) dt = 0.05;
            const double omega = 16.0;
            const double acc = -2.0 * omega * mShapeVel - omega * omega * (mShapeDisplay - mShapeTarget);
            mShapeVel += acc * dt;
            mShapeDisplay += mShapeVel * dt;
        }
        Segment::advance(nowMs);
    }

    void SubOscPanel::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawPanelChrome(t, w, h, mAccent, "SUB");

        // wide wave preview on the right: two cycles, glow + gradient floor
        const double bx = 200.0, by = 36.0, bw = w - bx - 14.0, bh = h - by - 14.0;
        drawRoundedRect(t, Rect{bx, by, bw, bh}, 6.0,
                        Paint::filledStroked(Color{0, 0, 0, 0.35}, Color{1, 1, 1, 0.08}, 1.0));
        const double midY = by + bh * 0.5, amp = bh * 0.36, x0 = bx + 8, x1 = bx + bw - 8;

        // floor gradient under the wave
        t.beginPath();
        t.moveTo(x0, midY); t.lineTo(x1, midY); t.lineTo(x1, by + bh - 4); t.lineTo(x0, by + bh - 4); t.closePath();
        t.setLinearFill(0, midY, 0, by + bh - 4,
                        Color{mAccent.r, mAccent.g, mAccent.b, 0.10},
                        Color{mAccent.r, mAccent.g, mAccent.b, 0.0});
        t.fillPath();

        std::vector<Point> pts;
        for (double x = x0; x <= x1; x += 1.5)
        {
            const double p01 = (x - x0) / (x1 - x0) * 2.0; // two cycles
            pts.push_back({x, midY - subSample(p01 - std::floor(p01)) * amp});
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
    }
}
}
