#include "LfoPanel.h"
#include "Chrome.h"
#include <cmath>
#include <cstdio>

namespace arstro
{
namespace pulsar
{
    using namespace artboard;

    namespace
    {
        // tab strip (left), curve (centre), controls (right)
        constexpr double kTabX = 14.0, kTabY = 40.0, kTabW = 46.0, kTabH = 46.0, kTabGap = 6.0;
        constexpr double kCurveX = 70.0, kCurveY = 40.0, kCurveW = 252.0, kCurveH = 158.0;
        constexpr double kRightX = 332.0;
        const double kRatios[6] = {1.0 / 16, 1.0 / 8, 1.0 / 4, 1.0 / 2, 1.0, 2.0};
        const char *kRatioLabels[6] = {"1/16", "1/8", "1/4", "1/2", "1", "2"};
        int ratioIndex(double v01) { int i = (int)std::lround(v01 * 5.0); return i < 0 ? 0 : (i > 5 ? 5 : i); }
    }

    LfoPanel::LfoPanel(const Theme &theme, const Color &accent) : mAccent(accent)
    {
        width.set(420.0);
        height.set(250.0);

        for (int i = 0; i < kCount; ++i)
        {
            auto curve = std::make_shared<LfoCurve>();
            curve->setAccent(accent);
            curve->x.set(kCurveX); curve->y.set(kCurveY);
            curve->width.set(kCurveW); curve->height.set(kCurveH);
            curve->visible = (i == 0);
            addChild(curve);
            mCurves[i] = curve;
        }

        mRateKnob = std::make_shared<Knob>(theme.knob);
        mRateKnob->label = "rate";
        mRateKnob->setRange(0.0, 1.0); mRateKnob->setValue(mRate[0]); mRateKnob->setDefault(mRate[0]);
        mRateKnob->x.set(kRightX); mRateKnob->y.set(46.0);
        mRateKnob->width.set(64.0); mRateKnob->height.set(54.0);
        mRateKnob->onChange = [this](double v) { mRate[mSelected] = v; };
        addChild(mRateKnob);

        mBpmToggle = std::make_shared<ToggleSwitch>(theme.toggle);
        mBpmToggle->setOn(false);
        mBpmToggle->x.set(kRightX + 6.0); mBpmToggle->y.set(126.0);
        mBpmToggle->width.set(40.0); mBpmToggle->height.set(20.0);
        mBpmToggle->onChange = [this](bool on) { mBpm[mSelected] = on; };
        addChild(mBpmToggle);

        mBadge = std::make_shared<ModSourceBadge>(sourceId(0), accent, "LFO");
        mBadge->x.set(kRightX); mBadge->y.set(176.0);
        addChild(mBadge);
    }

    void LfoPanel::selectTab(int i)
    {
        mSelected = i;
        for (int k = 0; k < kCount; ++k) mCurves[k]->visible = (k == i);
        mCurves[i]->triggerReveal(); // fade the newly shown curve in
        mRateKnob->setValue(mRate[i]);
        mBpmToggle->setOn(mBpm[i]);
        mBadge->setSourceId(sourceId(i));
    }

    double LfoPanel::rateHz(int i) const
    {
        if (mBpm[i]) // ratio of a beat at an assumed 120 BPM (2 beats/sec)
            return (120.0 / 60.0) / kRatios[ratioIndex(mRate[i])];
        return 0.2 + mRate[i] * 5.0; // 0.2 .. 5.2 Hz
    }

    double LfoPanel::output(int i) const { return mGate ? mCurves[i]->valueAt(mPhase[i]) : 0.0; }

    void LfoPanel::setGate(bool on)
    {
        if (on && !mGate)
            for (auto &p : mPhase) p = 0.0;
        mGate = on;
    }

    void LfoPanel::setAssignSink(std::function<void(int, const Color &, const Point &)> fn)
    {
        mBadge->onAssign = fn;
    }

    void LfoPanel::advance(double nowMs)
    {
        double dt = mLastMs < 0.0 ? 0.0 : (nowMs - mLastMs) / 1000.0;
        mLastMs = nowMs;
        if (dt > 0.0 && dt < 0.05 && mGate)
            for (int i = 0; i < kCount; ++i)
            {
                mPhase[i] += dt * rateHz(i);
                mPhase[i] -= std::floor(mPhase[i]);
            }
        for (int i = 0; i < kCount; ++i)
            mCurves[i]->setPlayhead(mPhase[i], mGate);
        Segment::advance(nowMs);
    }

    void LfoPanel::onPaint(IRenderTarget &t) const
    {
        drawPanelChrome(t, width.value(), height.value(), mAccent, "LFO");

        // left tab strip
        for (int i = 0; i < kCount; ++i)
        {
            const double ty = kTabY + i * (kTabH + kTabGap);
            const bool sel = (i == mSelected);
            drawRoundedRect(t, Rect{kTabX, ty, kTabW, kTabH}, 5.0,
                            Paint::filledStroked(sel ? Color{mAccent.r, mAccent.g, mAccent.b, 0.30}
                                                     : Color{1, 1, 1, 0.04},
                                                 sel ? mAccent : Color{1, 1, 1, 0.10}, 1.0));
            t.setFill(sel ? mAccent : Color{1, 1, 1, 0.5});
            char buf[4]; std::snprintf(buf, sizeof buf, "%d", i + 1);
            t.drawText(buf, kTabX + kTabW * 0.5 - 4.0, ty + kTabH * 0.5 + 5.0, 14.0);
        }

        // rate readout (under the rate knob): musical ratio or Hz
        char buf[24];
        if (mBpm[mSelected])
            std::snprintf(buf, sizeof buf, "%s beat", kRatioLabels[ratioIndex(mRate[mSelected])]);
        else
            std::snprintf(buf, sizeof buf, "%.2f Hz", 0.2 + mRate[mSelected] * 5.0);
        t.setFill(Color{1, 1, 1, 0.6});
        t.drawText(buf, kRightX, 116.0, 10.0);
        t.setFill(Color{1, 1, 1, 0.4});
        t.drawText("BPM", kRightX + 50.0, 140.0, 9.0);
    }

    bool LfoPanel::handleGesture(const Gesture &g, const Point &lp)
    {
        using T = Gesture::Type;
        if (g.type == T::Down || g.type == T::Click)
            for (int i = 0; i < kCount; ++i)
            {
                const double ty = kTabY + i * (kTabH + kTabGap);
                if (lp.x >= kTabX && lp.x <= kTabX + kTabW && lp.y >= ty && lp.y <= ty + kTabH)
                {
                    selectTab(i);
                    return true;
                }
            }
        return Segment::handleGesture(g, lp);
    }
}
}
