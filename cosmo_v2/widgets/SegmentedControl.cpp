#include "SegmentedControl.h"
#include "RoundedRectExt.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace { constexpr double kSlideMs = 220.0; }

    SegmentedControl::SegmentedControl(std::vector<std::string> labels)
    {
        for (auto &label : labels)
        {
            auto seg = std::make_shared<PillButton>(label);
            const int idx = (int)mSegs.size();
            seg->onClick = [this, idx] { setSelected(idx); };
            addChild(seg);
            mSegs.push_back(seg);
        }
        if (!mSegs.empty()) mSegs[0]->active = true;
    }

    void SegmentedControl::setSelected(int index)
    {
        if (index < 0 || index >= (int)mSegs.size()) return;
        const bool changed = (index != mSelected);
        mSelected = index;
        for (size_t i = 0; i < mSegs.size(); ++i) mSegs[i]->active = ((int)i == index);
        if (changed) mPendingTarget = index;  // advance() starts the slide (it has nowMs)
        if (onChange) onChange(index);
    }

    void SegmentedControl::setSelectedImmediate(int index)
    {
        if (index < 0 || index >= (int)mSegs.size()) return;
        mSelected = index;
        for (size_t i = 0; i < mSegs.size(); ++i) mSegs[i]->active = ((int)i == index);
        mHiPos.set(index);
        mPendingTarget = -1;
        mInit = true;
    }

    void SegmentedControl::advance(double nowMs)
    {
        if (!mInit) { mHiPos.set(mSelected); mInit = true; }
        else if (mPendingTarget >= 0) { mHiPos.animateTo(mPendingTarget, kSlideMs, Easing::EaseOutCubic, nowMs); mPendingTarget = -1; }
        mHiPos.update(nowMs);
        Segment::advance(nowMs);
    }

    void SegmentedControl::layout()
    {
        const double w = width.value(), h = height.value();
        const double innerW = w - 2 * padding, innerH = h - 2 * padding;
        const int n = (int)mSegs.size();
        if (n == 0) return;
        const double segW = (innerW - gap * (n - 1)) / n;
        double cx = padding;
        for (auto &seg : mSegs)
        {
            seg->idleBox = idleSegBox;
            seg->activeBox = {Paint{}, 0.0};  // highlight is drawn by the parent, not the button
            seg->idleText = idleText;
            seg->activeText = activeText;
            seg->x.set(cx);
            seg->y.set(padding);
            seg->width.set(segW);
            seg->height.set(innerH);
            cx += segW + gap;
        }
    }

    void SegmentedControl::cornerRadii(int seg, double &tl, double &tr, double &br, double &bl) const
    {
        const int n = (int)mSegs.size();
        const double ri = activeSegBox.cornerRadius;
        const double ro = edgeRadius < 0 ? ri : edgeRadius;
        const bool first = (seg <= 0), last = (seg >= n - 1);
        tl = bl = first ? ro : ri;
        tr = br = last ? ro : ri;
    }

    void SegmentedControl::onPaint(IRenderTarget &t) const
    {
        if (containerBox.paint.hasFill || containerBox.paint.hasStroke)
            drawRoundedRect(t, Rect{0, 0, width.value(), height.value()}, containerBox.cornerRadius, containerBox.paint);

        const int n = (int)mSegs.size();
        if (n == 0 || !(activeSegBox.paint.hasFill || activeSegBox.paint.hasStroke)) return;

        const double innerW = width.value() - 2 * padding, innerH = height.value() - 2 * padding;
        const double segW = (innerW - gap * (n - 1)) / n;
        const double p = std::clamp(mHiPos.value(), 0.0, (double)(n - 1));
        const double x = padding + p * (segW + gap);

        // Blend per-corner radii between the two nearest slots so the rounding
        // eases across the split point instead of snapping at the midpoint.
        const int a = (int)std::floor(p), b = std::min(n - 1, a + 1);
        const double f = p - a;
        double atl, atr, abr, abl, btl, btr, bbr, bbl;
        cornerRadii(a, atl, atr, abr, abl);
        cornerRadii(b, btl, btr, bbr, bbl);
        auto mix = [f](double u, double v) { return u + (v - u) * f; };
        drawRoundedRectCorners(t, Rect{x, padding, segW, innerH},
                               mix(atl, btl), mix(atr, btr), mix(abr, bbr), mix(abl, bbl),
                               activeSegBox.paint);
    }
}
}
