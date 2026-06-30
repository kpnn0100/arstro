/*
 *  Cosmo by arstro — ContextMenu: a right-click popup list. While open it is modal
 *  (catches every click: one on an item runs it, one outside dismisses) and draws in
 *  the Artboard overlay pass so it sits on top of everything. The host sizes it to
 *  cover the root so the popup can be clamped on-screen.
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
    class ContextMenu : public artboard::Segment
    {
    public:
        struct Item { std::string label; std::function<void()> action; bool enabled = true; };
        explicit ContextMenu(const artboard::Color &accent);

        void show(double x, double y, std::vector<Item> items);
        void hide() { mOpen = false; }
        bool isOpen() const { return mOpen; }

    protected:
        void onOverlay(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return mOpen; }  // modal

    private:
        artboard::Rect menuRect() const;
        int itemAt(const artboard::Point &world) const;

        artboard::Color mAccent;
        bool mOpen = false;
        double mX = 0, mY = 0;
        std::vector<Item> mItems;
    };
}
}
