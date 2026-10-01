/*
 *  interstellar_v1 — EasedScroll: the five co-located parts of a scrollable surface, once (R6).
 *
 *  The design rule lists what a scroll needs and every list in this app needs all of it: a plain
 *  TARGET that `scrollBy` moves and clamps at BOTH ends; an eased live value `advance` drives
 *  (180 ms, R1); the viewport and content extents re-measured every layout so a resize cannot
 *  leave a stale limit; "unscrollable" reported to the caller so the wheel can BUBBLE to the
 *  parent; and a bar drawn only when there is something to scroll. Four lists (rack tree, source
 *  bin, render queue, version dropdown) plus the Home grid would otherwise each re-derive it, and
 *  two copies of one fact always drift (gotcha 15).
 *
 *  ONE viewport rectangle: the owner hands `setExtent` the same top/height it lays rows out in,
 *  clips to, culls against and draws the bar in — `viewTop`/`viewH` are stored here so those
 *  four reads cannot come from two copies.
 */
#pragma once
#include "../Theme.h"
#include <algorithm>

namespace arstro
{
namespace interstellar_v1
{
    class EasedScroll
    {
    public:
        /** Re-measure every layout. Clamps the target if the content shrank under it. */
        void setExtent(double viewTop, double viewH, double contentH)
        {
            mViewTop = viewTop;
            mViewH = std::max(0.0, viewH);
            mContentH = std::max(0.0, contentH);
            mTarget = std::clamp(mTarget, 0.0, maxScroll());
        }
        double maxScroll() const { return std::max(0.0, mContentH - mViewH); }
        bool scrollable() const { return maxScroll() > 0.5; }

        /** Move the target by `px` (positive = toward the end). Returns false when there is
         *  nothing to scroll, so the caller lets the wheel bubble to its parent. */
        bool scrollBy(double px)
        {
            if (!scrollable()) return false;
            mTarget = std::clamp(mTarget + px, 0.0, maxScroll());
            return true;
        }
        /** Bring [top, top+h) (content coords) fully into view with the minimum travel. */
        void reveal(double top, double h)
        {
            if (top < mTarget) mTarget = top;
            else if (top + h > mTarget + mViewH) mTarget = top + h - mViewH;
            mTarget = std::clamp(mTarget, 0.0, maxScroll());
        }
        /** Snap with no travel — only for a NEW list, where there is nowhere to travel from. */
        void reset() { mTarget = mLast = 0.0; mLive.set(0.0); }

        /** Starts the tween when the target moved; returns true while moving (re-layout then). */
        bool advance(double nowMs)
        {
            if (mTarget != mLast)
            {
                mLive.animateTo(mTarget, motion::kScrollMs, artboard::Easing::EaseOutCubic, nowMs);
                mLast = mTarget;
            }
            const bool moving = mLive.isAnimating();
            mLive.update(nowMs);
            return moving;
        }

        double value() const { return mLive.value(); }     // the DRAWN offset
        double target() const { return mTarget; }
        double viewTop() const { return mViewTop; }
        double viewH() const { return mViewH; }
        double contentH() const { return mContentH; }

        /** Is a content-space band [top, top+h) at least partly inside the viewport right now?
         *  The same test culls paint AND hides row widgets (gotcha 1). */
        bool bandVisible(double top, double h) const
        {
            const double y = top - value();
            return y + h > 0.0 && y < mViewH;
        }

        /** A thin pill thumb at the right edge of the viewport — only when there is more. */
        void drawBar(artboard::IRenderTarget &t, double right, double alpha = 1.0) const
        {
            if (!scrollable() || mViewH <= 0.0) return;
            const double frac = mViewH / mContentH;
            const double thumbH = std::max(18.0, mViewH * frac);
            const double y = mViewTop + (mViewH - thumbH) * (value() / maxScroll());
            artboard::Color c = palette::whiteAlpha(0.14);
            c.a *= alpha;
            artboard::drawRoundedRect(t, artboard::Rect{right - 4.0, y, 3.0, thumbH}, radius::pill(),
                                      artboard::Paint::filled(c));
        }

    private:
        double mTarget = 0.0, mLast = 0.0;
        artboard::AnimatedProperty mLive{0.0};
        double mViewTop = 0.0, mViewH = 0.0, mContentH = 0.0;
    };
}
}
