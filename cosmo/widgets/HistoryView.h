/*
 *  Cosmo by arstro — HistoryView: a modal popup that draws the edit history as a
 *  git-style tree (root = oldest step at the top, branches fanning into lanes).
 *  Click a node to jump the image to that state (back or forward); drag to pan a
 *  tree taller/wider than the card. Draws in the overlay pass on top of everything.
 *
 *  It holds only a lightweight snapshot of the tree structure (parent + label per
 *  node) plus the current index — never the pixel/param data — and reports the
 *  clicked node index through onSelect. Built entirely from existing Artboard
 *  primitives (circles, lines, text, clip); no HAL change.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo
{
    class HistoryView : public artboard::Segment
    {
    public:
        struct Node { int parent = -1; std::string label; };

        explicit HistoryView(const artboard::Color &accent);

        /** Show the popup for a tree snapshot with `current` highlighted. */
        void show(std::vector<Node> nodes, int current);
        /** Update just the highlighted node (after a jump) while staying open. */
        void setCurrent(int current);
        /** Scroll the tree by a mouse-wheel delta (positive = toward older/top steps). */
        void scrollBy(double wheelDelta);
        void hide() { mOpen = false; }
        bool isOpen() const { return mOpen; }

        std::function<void(int)> onSelect;  // a node was clicked -> jump to it

        /** World-space centre of node i in the laid-out tree (for tests). */
        artboard::Point testNodeCenter(int i) const { return nodeCenter(i); }

    protected:
        void onOverlay(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return mOpen; }  // modal

    private:
        artboard::Rect cardRect() const;
        artboard::Rect treeRect() const;     // clipped, pannable area inside the card
        artboard::Rect closeBtnRect() const;
        artboard::Point nodeCenter(int i) const;  // world-space, incl. pan
        void relayout();                     // row + lane per node, content size
        void clampPan();
        void scrollToCurrent();

        artboard::Color mAccent;
        bool mOpen = false;
        std::vector<Node> mNodes;
        int mCurrent = -1;
        std::vector<int> mRow;     // unique row per node (git-log order: no two share a line)
        std::vector<int> mLane;    // graph column (branch lane) per node
        int mMaxRow = 0, mMaxLane = 0;
        double mPanX = 0, mPanY = 0;
        artboard::Point mDragLast{0, 0};
    };
}
}
