/*
 *  Cosmo by arstro — MenuBar: a top-left menu strip (File, Settings, ...). The bar
 *  has a single ACTIVE menu (at most one open at a time, tab-like): clicking one
 *  closes the other. A menu either drops down a list of items, or is an `overlay`
 *  menu whose open state is shown by the app (e.g. the Settings panel) — signalled
 *  via onOpenChanged.
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
        struct Menu
        {
            std::string title;
            std::vector<Item> items;  // dropdown items (empty for an overlay menu)
            bool overlay = false;     // open state shown by the app, not a dropdown
        };

        explicit MenuBar(const artboard::Color &accent);
        void addMenu(Menu m);
        int menuCount() const { return (int)mMenus.size(); }
        /** Replace a menu's dropdown items (e.g. a dynamic preset list). */
        void setMenuItems(int index, std::vector<Item> items);

        /** Fired whenever the active menu changes (index, or -1 = none open). */
        std::function<void(int)> onOpenChanged;

        int openIndex() const { return mOpen; }
        void close() { setOpen(-1); }
        /** True if a local-space point is on the bar or the open dropdown. */
        bool pointInActiveArea(const artboard::Point &local) const;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        /** The open dropdown draws here (like ContextMenu/PresetDialog/HistoryView)
         *  so it's always on top, regardless of what else is added to the root
         *  after the menu bar (e.g. the left-docked preset panel). */
        void onOverlay(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override;

    private:
        void setOpen(int idx);
        double titleX(int i) const;
        double titleW(int i) const;
        artboard::Rect dropdownRect(int i) const;
        int titleAt(const artboard::Point &p) const;
        int itemAt(int menu, const artboard::Point &p) const;

        std::vector<Menu> mMenus;
        artboard::Color mAccent;
        int mOpen = -1;
    };
}
}
