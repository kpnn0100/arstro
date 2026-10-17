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
#include "../../../core/Artboard/include/artboard/artboard.h"
#include "HoverFade.h"
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

        /** OPT-IN (Solaris R-HIST-3): the current node's highlight EASES from the node it left to the node it
         *  reaches instead of jumping there (design rule §1). Off by default, so cosmo draws as it always has. */
        bool easeCurrent = false;
        /** The line under the title. A host whose keys are settings says its own (Solaris R-SET-4). */
        std::string hint = "click a node to jump  .  Ctrl+Z undo  .  Ctrl+Y redo";
        std::string title = "HISTORY"; // the card's header (opt-in: Solaris draws it in its language, R-ACCESS-2)
        // read-only, for a host's tests: the LIVE open amount, a node's centre (this segment's space) and how lit it is
        double appearAmount() const { return mAppear.value(); }
        int nodeCount() const { return (int)mNodes.size(); }
        int current() const { return mCurrent; }
        artboard::Point nodePoint(int i) const { return nodeCenter(i); }
        double currentAmount(int i) const;

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

        // hover + open/close animation (R-G-1/R-G-3: everything animates, nothing snaps)
        HoverFade mNodeHover;                       // per-node wash — each cross-fades independently
        bool mCloseHover = false;                   // pointer over the close X
        bool mCloseHoverPrev = false, mWasOpen = false;
        artboard::AnimatedProperty mAppear{0.0};    // open/close fade + rise
        artboard::AnimatedProperty mCloseAmt{0.0};  // close-X lift fade
        std::vector<artboard::AnimatedProperty> mCurAmt; // easeCurrent: each node's highlight, eased
        std::vector<double> mCurLast;
        artboard::AnimatedProperty mPanXAnim{0.0};  // eased pan (glides toward mPanX/mPanY)
        artboard::AnimatedProperty mPanYAnim{0.0};
    };
}
}
