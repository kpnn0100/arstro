/*
 *  Cosmo by arstro — MenuBar: a top-left menu strip (File, Settings, ...). A menu
 *  with items opens a dropdown; a menu with no items is a direct-action button. The
 *  app closes an open menu on an outside click (closeIfOutside).
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
    class MenuBar : public artboard::Segment
    {
    public:
        struct Item { std::string label; std::function<void()> action; };
        struct Menu { std::string title; std::vector<Item> items; std::function<void()> action; };

        explicit MenuBar(const artboard::Color &accent);
        void addMenu(Menu m);

        bool isOpen() const { return mOpen >= 0; }
        void close() { mOpen = -1; }
        /** Close if a (local-space) point is outside the bar + any open dropdown. */
        void closeIfOutside(const artboard::Point &local);

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override;

    private:
        double titleX(int i) const;
        double titleW(int i) const;
        artboard::Rect dropdownRect(int i) const;
        int titleAt(const artboard::Point &p) const;  // -1 if none
        int itemAt(int menu, const artboard::Point &p) const;  // -1 if none

        std::vector<Menu> mMenus;
        artboard::Color mAccent;
        int mOpen = -1;
    };
}
}
