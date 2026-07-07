/*
 *  cosmo_v2 by arstro — HistoryView: the History ▸ "Show History Tree…" modal,
 *  ported from cosmo's HistoryView so the menu item is a real, working feature
 *  (not a dead host seam). Draws the branching edit history as a git-style tree
 *  (root/oldest at the top, branches fanning into lanes); click a node to jump
 *  the image to that state, drag to pan. Holds only a lightweight snapshot
 *  (parent + label per node) + the current index, reporting a click via
 *  onSelect. Painted in the overlay pass on top of everything; modal while open.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class HistoryView : public artboard::Segment
    {
    public:
        struct Node { int parent = -1; std::string label; };

        HistoryView() = default;

        void show(std::vector<Node> nodes, int current);  // open for a tree snapshot
        void setCurrent(int current);                     // re-highlight after a jump, stay open
        void scrollBy(double wheelDelta);
        void hide() { mOpen = false; }
        bool isOpen() const { return mOpen; }

        void advance(double nowMs) override;  // drives the open/close + hover fades + eased pan (R-G-1)

        std::function<void(int)> onSelect;  // a node was clicked -> jump to it

    protected:
        void onOverlay(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { (void)p; return mOpen; }  // modal

    private:
        artboard::Rect cardRect() const;
        artboard::Rect treeRect() const;
        artboard::Rect closeBtnRect() const;
        artboard::Point nodeCenter(int i) const;
        int nodeAt(const artboard::Point &local) const;  // node under a point (-1 = none)
        double appearRise() const;                       // open/close y-rise, driven by mAppear
        double panX() const;                             // eased (drawn) pan, not the target
        double panY() const;
        void relayout();
        void clampPan();
        void scrollToCurrent();

        bool mOpen = false;
        std::vector<Node> mNodes;
        int mCurrent = -1;
        std::vector<int> mRow;
        std::vector<int> mLane;
        int mMaxRow = 0, mMaxLane = 0;
        double mPanX = 0, mPanY = 0;              // TARGET pan (clamped); eased into place below
        double mPanIssuedX = 0, mPanIssuedY = 0;  // last target handed to the pan tweens
        double mPanDurMs = 0.0;                   // glide duration for the pending pan change
        artboard::Point mDragLast{0, 0};

        // hover + open/close animation (R-G-1: everything animates, nothing snaps)
        int mHoverNode = -1;                        // node under the pointer (-1 = none)
        bool mCloseHover = false;                   // pointer over the close X
        bool mNodeHoverPrev = false, mCloseHoverPrev = false, mWasOpen = false;
        artboard::AnimatedProperty mAppear{0.0};    // open/close fade + rise
        artboard::AnimatedProperty mHoverAmt{0.0};  // hovered-node wash fade
        artboard::AnimatedProperty mCloseAmt{0.0};  // close-X lift fade
        artboard::AnimatedProperty mPanXAnim{0.0};  // eased pan (glides toward mPanX/mPanY)
        artboard::AnimatedProperty mPanYAnim{0.0};
    };
}
}
