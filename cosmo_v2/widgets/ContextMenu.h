/*
 *  cosmo_v2 by arstro — ContextMenu: a small right-click popup (a list of
 *  labelled actions) shown at the cursor, ported in spirit from cosmo's
 *  ContextMenu. Painted in the overlay pass on top of everything and modal
 *  while open (captures the next click). A DIRECT child of the root so raising
 *  it wins hit-testing over the body widgets it overlaps.
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
    class ContextMenu : public artboard::Segment
    {
    public:
        struct Item { std::string label; std::function<void()> action; };

        ContextMenu() = default;

        void open(std::vector<Item> items, double x, double y);
        void close() { mOpen = false; }
        bool isOpen() const { return mOpen; }

    protected:
        void onOverlay(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { (void)p; return mOpen; }  // modal

    private:
        artboard::Rect menuRect() const;
        int itemAt(const artboard::Point &local) const;

        std::vector<Item> mItems;
        double mX = 0, mY = 0;
        bool mOpen = false;
    };
}
}
