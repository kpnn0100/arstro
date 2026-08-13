/*
 *  cosmo_v2 by arstro — StackPanel: a vertical stack of fixed-height child panels
 *  with eased scroll, used to MERGE several editors into one scrollable edit-stack
 *  tab (e.g. Mixer + Curve). Each item keeps its own internal layout (supplied as a
 *  relayout callback); the stack only sizes + positions them and scrolls the column.
 *  Scroll eases toward its target (R-G-1) rather than jumping per wheel notch.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class StackPanel : public artboard::Segment
    {
    public:
        StackPanel() { clipToBounds = true; }

        /** Add a child panel with a fixed content height; `relayout` re-lays the
         *  child's internals at the width/height the stack gives it. */
        void addItem(std::shared_ptr<artboard::Segment> seg, double height, std::function<void()> relayout = {})
        {
            addChild(seg);
            mItems.push_back({std::move(seg), height, std::move(relayout)});
        }

        void scrollBy(double delta)
        {
            const double maxS = std::max(0.0, mContentH - height.value());
            mTarget = std::min(maxS, std::max(0.0, mTarget - delta));
        }

        void layout()  // size + relayout children, position from the current scroll
        {
            double y = -mScroll.value();
            for (auto &it : mItems)
            {
                it.seg->x.set(0.0);
                it.seg->width.set(width.value());
                it.seg->height.set(it.h);
                it.seg->y.set(y);
                if (it.relayout) it.relayout();
                y += it.h;
            }
            mContentH = y + mScroll.value();
        }

        void advance(double nowMs) override
        {
            if (mTarget != mLast)
            {
                mScroll.animateTo(mTarget, 180.0, artboard::Easing::EaseOutCubic, nowMs);
                mLast = mTarget;
            }
            mScroll.update(nowMs);  // layout() (run every frame by the owner) positions from this
            artboard::Segment::advance(nowMs);
        }

    private:
        struct Item { std::shared_ptr<artboard::Segment> seg; double h; std::function<void()> relayout; };
        std::vector<Item> mItems;
        artboard::AnimatedProperty mScroll{0.0};
        double mTarget = 0.0, mLast = 0.0, mContentH = 0.0;
    };
}
}
