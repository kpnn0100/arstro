#include "LfoPanel.h"
#include "Chrome.h"
#include <cmath>

namespace arstro
{
namespace pulsar
{
    using namespace artboard;

    LfoPanel::LfoPanel(const Theme &theme, const Color &accent) : mAccent(accent)
    {
        width.set(360.0);
        height.set(250.0);

        mTabs = std::make_shared<TabView>(theme.tab);
        mTabs->tabHeight = 26.0;
        mTabs->x.set(14.0); mTabs->y.set(30.0);
        mTabs->width.set(332.0); mTabs->height.set(212.0);

        for (int i = 0; i < kCount; ++i)
        {
            auto page = std::make_shared<Segment>();
            page->width.set(332.0); page->height.set(180.0);

            auto curve = std::make_shared<LfoCurve>();
            curve->setAccent(accent);
            curve->x.set(4.0); curve->y.set(0.0);
            curve->width.set(324.0); curve->height.set(120.0);
            page->addChild(curve);
            mCurves[i] = curve;

            int idx = i;
            auto rate = std::make_shared<Knob>(theme.knob);
            rate->label = "rate";
            rate->setRange(0.0, 1.0); rate->setValue(mRate[i]); rate->setDefault(mRate[i]);
            rate->x.set(105.0); rate->y.set(126.0);
            rate->width.set(58.0); rate->height.set(48.0);
            rate->onChange = [this, idx](double v) { mRate[idx] = v; };
            page->addChild(rate);

            auto depth = std::make_shared<Knob>(theme.knob);
            depth->label = "depth";
            depth->setRange(0.0, 1.0); depth->setValue(0.8); depth->setDefault(0.8);
            depth->x.set(169.0); depth->y.set(126.0);
            depth->width.set(58.0); depth->height.set(48.0);
            page->addChild(depth);

            auto badge = std::make_shared<ModSourceBadge>(sourceId(i), accent, "LFO");
            badge->x.set(280.0); badge->y.set(2.0);
            page->addChild(badge);
            mBadges[i] = badge;

            mTabs->addPage(std::to_string(i + 1), page);
        }
        addChild(mTabs);
    }

    double LfoPanel::output(int i) const
    {
        return mGate ? mCurves[i]->valueAt(mPhase[i]) : 0.0;
    }

    void LfoPanel::setAssignSink(std::function<void(int, const Color &, const Point &)> fn)
    {
        for (auto &b : mBadges) b->onAssign = fn;
    }

    void LfoPanel::setGate(bool on)
    {
        if (on && !mGate) // note-on: retrigger every LFO from phase 0
            for (auto &p : mPhase) p = 0.0;
        mGate = on;
    }

    void LfoPanel::advance(double nowMs)
    {
        double dt = mLastMs < 0.0 ? 0.0 : (nowMs - mLastMs) / 1000.0;
        mLastMs = nowMs;
        if (dt > 0.0 && dt < 0.05 && mGate)
            for (int i = 0; i < kCount; ++i)
            {
                const double hz = 0.2 + mRate[i] * 5.0;
                mPhase[i] += dt * hz;
                mPhase[i] -= std::floor(mPhase[i]);
            }
        for (int i = 0; i < kCount; ++i)
            mCurves[i]->setPlayhead(mPhase[i], mGate);
        Segment::advance(nowMs);
    }

    void LfoPanel::onPaint(IRenderTarget &t) const
    {
        drawPanelChrome(t, width.value(), height.value(), mAccent, "LFO");
    }
}
}
