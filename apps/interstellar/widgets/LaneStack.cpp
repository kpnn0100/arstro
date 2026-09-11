#include "LaneStack.h"
#include <algorithm>
#include <cmath>

using namespace artboard;

namespace arstro
{
namespace interstellar_v1
{
    LaneStack::LaneStack() { clipToBounds = true; }

    void LaneStack::setLanes(std::vector<LaneRow> lanes)
    {
        // Pool the per-row animated values rather than rebuilding them: a rebuild would restart
        // every expansion from zero on every model refresh, which is the snap R-G-1 forbids.
        const size_t before = mLanes.size();
        mLanes = std::move(lanes);
        while (mExpand.size() < mLanes.size())
        {
            mExpand.emplace_back(0.0);
            mExpandTarget.push_back(0.0);
        }
        (void)before;
    }

    void LaneStack::toggleExpanded(const std::string &address)
    {
        for (size_t i = 0; i < mLanes.size(); ++i)
            if (mLanes[i].address == address)
                mExpandTarget[i] = mExpandTarget[i] > 0.5 ? 0.0 : 1.0;
    }

    double LaneStack::expansion(const std::string &address) const
    {
        for (size_t i = 0; i < mLanes.size() && i < mExpand.size(); ++i)
            if (mLanes[i].address == address) return mExpand[i].value();
        return 0.0;
    }

    double LaneStack::rowHeight(const std::string &address) const
    {
        const double e = expansion(address);
        return time::laneHeight() + (time::laneHeightOpen() - time::laneHeight()) * e;
    }

    double LaneStack::yForRow(size_t idx) const
    {
        double y = -mVScroll.value();
        for (size_t i = 0; i < idx && i < mLanes.size(); ++i) y += rowHeight(mLanes[i].address);
        return y;
    }

    double LaneStack::contentHeight() const
    {
        double h = 0;
        for (const auto &l : mLanes) h += rowHeight(l.address);
        return h;
    }

    void LaneStack::scrollBy(double dy)
    {
        // Clamped at BOTH ends, and against the same viewport the rows are placed in.
        const double maxScroll = std::max(0.0, contentHeight() - viewport().h);
        mVScrollTarget = std::max(0.0, std::min(maxScroll, mVScrollTarget + dy));
    }

    void LaneStack::layout() {}

    void LaneStack::advance(double nowMs)
    {
        mLastMs = nowMs;
        for (size_t i = 0; i < mExpand.size() && i < mExpandTarget.size(); ++i)
            if (std::fabs(mExpandTarget[i] - mExpand[i].value()) > 1e-9 && !mExpand[i].isAnimating())
                mExpand[i].animateTo(mExpandTarget[i], 200.0, Easing::EaseOutCubic, nowMs);
        for (auto &p : mExpand) p.update(nowMs);
        if (std::fabs(mVScrollTarget - mVScroll.value()) > 1e-9 && !mVScroll.isAnimating())
            mVScroll.animateTo(mVScrollTarget, 180.0, Easing::EaseOutCubic, nowMs);
        mVScroll.update(nowMs);
        Segment::advance(nowMs);
    }

    Rect LaneStack::linkRect(const std::string &address) const
    {
        for (size_t i = 0; i < mLanes.size(); ++i)
        {
            if (mLanes[i].address != address) continue;
            const auto &l = mLanes[i];
            const double x = time::headerWidth() + (l.at - mScroll) * mPps;
            return Rect{x, yForRow(i) + 2.0, std::max(2.0, l.dur * mPps),
                        rowHeight(l.address) - 4.0};
        }
        return {};
    }

    void LaneStack::onPaint(IRenderTarget &t) const
    {
        const Rect vp = viewport();
        auto fillRect = [&](double x, double y, double w, double h, const Color &c) {
            if (w <= 0 || h <= 0) return;
            t.setFill(c);
            t.beginPath();
            t.moveTo(x, y); t.lineTo(x + w, y); t.lineTo(x + w, y + h); t.lineTo(x, y + h);
            t.closePath();
            t.fillPath();
        };
        fillRect(0, 0, vp.w, vp.h, surface::deckBg());

        if (mLanes.empty())
        {
            // The empty state, in words: "nothing is automated" and "it has not loaded" look
            // identical and ask for different things (arstro.design.rule §7).
            const std::string msg = "no automation yet — pick a parameter to automate";
            const double tw = t.measureText(msg, 12.0, font::sans(), 0.0);
            t.setFill(palette::mutedForeground());
            t.drawText(msg, (vp.w - tw) * 0.5, vp.h * 0.5, 12.0, font::sans(), 0.0);
            return;
        }

        const double labelW = time::headerWidth();
        for (size_t i = 0; i < mLanes.size(); ++i)
        {
            const auto &l = mLanes[i];
            const double y = yForRow(i), rh = rowHeight(l.address);
            // The SAME test the placement used: a row drawn but unhittable, or hittable but
            // undrawn, is the failure this shares one rect to prevent.
            if (y + rh < 0 || y > vp.h) continue;

            const bool sel = l.address == mSelected;
            fillRect(0, y, labelW, rh - 1.0, palette::card());
            fillRect(labelW, y, vp.w - labelW, rh - 1.0,
                     i % 2 ? surface::trackBgAlt() : surface::trackBg());
            if (sel) fillRect(0, y, 2.0, rh - 1.0, palette::primary());

            // The address, ellipsized against the space actually left.
            std::string label = l.address;
            while (!label.empty() && t.measureText(label, 10.0, font::sans(), 0.0) > labelW - 14.0)
                label.pop_back();
            t.setFill(sel ? palette::foreground() : palette::secondaryForeground());
            t.drawText(label, 8.0, y + time::laneHeight() * 0.5 + 3.5, 10.0, font::sans(), 0.0);

            // "ac_push · 2 links" — the affordance that makes a shared shape discoverable.
            std::string shape = l.shapeName;
            if (l.shapeUsers > 1) shape += " · " + std::to_string(l.shapeUsers) + " links";
            t.setFill(l.shapeUsers > 1 ? palette::primary() : palette::mutedForeground());
            t.drawText(shape, 8.0, y + time::laneHeight() * 0.5 + 14.0, 9.0, font::sans(), 0.13 * 9.0);

            // The link, as a clip in time.
            const Rect lr = linkRect(l.address);
            if (lr.w > 0 && lr.x + lr.w > labelW && lr.x < vp.w)
            {
                drawRoundedRect(t, lr, radius::control(), Paint::filled(surface::laneFill()));
                // The curve, sampled — and drawn only when the row is expanded enough to read.
                if (expansion(l.address) > 0.05 && !l.curve.empty())
                {
                    t.setStroke(surface::curve(), 1.0);
                    t.beginPath();
                    for (size_t k = 0; k < l.curve.size(); ++k)
                    {
                        const double cx = lr.x + lr.w * l.curve[k].first;
                        const double cy = lr.y + lr.h * (1.0 - l.curve[k].second);
                        if (k == 0) t.moveTo(cx, cy);
                        else t.lineTo(cx, cy);
                    }
                    t.strokePath();
                }
                if (l.stepsOnEntry)
                {
                    // The discontinuity, drawn: a hard change the user asked for is legitimate,
                    // one they did not notice is a defect they will blame on the renderer.
                    t.setStroke(palette::destructive(), 2.0);
                    t.beginPath();
                    t.moveTo(lr.x, lr.y); t.lineTo(lr.x, lr.y + lr.h);
                    t.closePath();
                    t.strokePath();
                }
            }
        }

        // The label gutter's divider, and the playhead over everything.
        t.setStroke(palette::border(), 1.0);
        t.beginPath();
        t.moveTo(labelW, 0); t.lineTo(labelW, vp.h);
        t.closePath();
        t.strokePath();

        const double px = time::headerWidth() + (mPlayhead - mScroll) * mPps;
        if (px >= time::headerWidth() && px <= vp.w)
        {
            t.setStroke(surface::playhead(), 1.0);
            t.beginPath();
            t.moveTo(px, 0); t.lineTo(px, vp.h);
            t.closePath();
            t.strokePath();
        }

        // A scrollbar ONLY when there is something to scroll: a permanent track claims there is
        // more when there is not.
        const double content = contentHeight();
        if (content > vp.h)
        {
            const double frac = vp.h / content;
            const double thumbH = std::max(18.0, vp.h * frac);
            const double maxScroll = content - vp.h;
            const double ty = (vp.h - thumbH) * (maxScroll > 0 ? mVScroll.value() / maxScroll : 0.0);
            drawRoundedRect(t, Rect{vp.w - 5.0, ty, 3.0, thumbH}, radius::pill(),
                            Paint::filled(palette::whiteAlpha(0.18)));
        }
    }

    bool LaneStack::handleGesture(const Gesture &g, const Point &local)
    {
        if (g.type == Gesture::Type::Scroll)
        {
            const double content = contentHeight();
            if (content <= viewport().h)
                return Segment::handleGesture(g, local);   // an unscrollable list BUBBLES the wheel
            scrollBy(-g.delta.y * 10.0);               // a notch is ~10 px, not one pixel
            return true;
        }
        if (g.type == Gesture::Type::Click)
        {
            for (size_t i = 0; i < mLanes.size(); ++i)
            {
                const double y = yForRow(i), rh = rowHeight(mLanes[i].address);
                if (local.y < y || local.y > y + rh) continue;
                mSelected = mLanes[i].address;
                if (local.x < time::headerWidth()) toggleExpanded(mLanes[i].address);
                if (onLanePicked) onLanePicked(mSelected);
                return true;
            }
        }
        return Segment::handleGesture(g, local);
    }
}
}
