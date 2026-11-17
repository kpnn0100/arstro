/*
 *  cosmo_v2 by arstro — MenuStrip: the top-bar menu bar (File / Settings /
 *  Develop / History / Preset), ported in spirit from cosmo's MenuBar so
 *  cosmo_v2 shows the SAME dropdown options as the original editor. At most one
 *  menu is open at a time (tab-like). Titles are CENTRED in their hit region.
 *
 *  Highlight motion (task follow-up point 1):
 *    - opening from closed  -> the highlight EXPANDS from zero width, centred on
 *      the clicked title, out to its full width ("smooth expand from the tab");
 *    - switching while open  -> the highlight SLIDES (its centre + width animate)
 *      from the old title to the newly-clicked one;
 *    - closing               -> it shrinks back to zero width in place.
 *
 *  The dropdown itself EXPANDS open too: on an open-from-closed it grows
 *  downward out of the title (height 0 -> full, items revealed top-down); on a
 *  switch it stays full and just swaps its contents.
 *
 *  The open dropdown paints in onOverlay so it sits above every sibling
 *  (unclipped), like cosmo's MenuBar.
 *
 *  R-FAMILY-1 (2026-11-17), all three opt-in per item, so a `{label, action}` item draws as it
 *  always did: a SHORTCUT drawn right-aligned in the mono face, muted (a dropdown that has one is
 *  as wide as its widest label + its widest shortcut, by measured text); a SEPARATOR, a hairline
 *  row that takes no click and no hover; a DISABLED row, dimmed, no hover wash, its click does
 *  nothing. An item's index counts every row, separators included. `menuKey` is the keyboard's
 *  way through an open menu (the host routes keys to it; it skips separators and disabled rows).
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
    class MenuStrip : public artboard::Segment
    {
    public:
        struct Item
        {
            std::string label;
            std::function<void()> action;
            // R-FAMILY-1 — after `action`, with defaults, so `{label, action}` still initialises one.
            std::string shortcut;      // "Ctrl+S": drawn right-aligned, mono, muted; never parsed
            bool enabled = true;       // false: dimmed, no hover wash, a click does nothing
            bool separator = false;    // true: a hairline row (label and action unused)
            /** A separator row. It IS an item: it has an index, and `itemRect` answers for it. */
            static Item sep() { Item i; i.separator = true; return i; }
            /** A row a pointer or the keyboard can stand on and activate. */
            bool selectable() const { return enabled && !separator; }
        };
        struct Menu { std::string title; std::vector<Item> items; };

        MenuStrip();
        void addMenu(Menu m);
        /** Replace one menu's items — a menu whose contents follow data (Interstellar's Preset
         *  menu lists the library). Closes the strip if that menu is the open one, so a dropdown
         *  never shows rows that changed under the pointer. */
        void setItems(int menu, std::vector<Item> items);
        /** The same menu in other words — its title and its items' labels, nothing closed or moved (opt-in: Solaris
         *  draws each paint pass in its language, R-ACCESS-2; cosmo never calls it). A count that differs is ignored. */
        void relabel(int menu, const std::string &title, const std::vector<std::string> &labels)
        {
            if (menu < 0 || menu >= (int)mMenus.size() || labels.size() != mMenus[(size_t)menu].items.size()) return;
            mMenus[(size_t)menu].title = title;
            for (size_t i = 0; i < labels.size(); ++i) mMenus[(size_t)menu].items[i].label = labels[i];
        }

        int openIndex() const { return mOpen; }
        void close();
        /** True if a local-space point is on the bar or the open dropdown. */
        bool pointInActiveArea(const artboard::Point &local) const;
        /** Total width of all titles laid end to end (for TopBar layout). */
        double contentWidth() const;
        /** Read-only geometry and contents, in local coordinates — what a test aims a click at. */
        int menuCount() const { return (int)mMenus.size(); }
        const Menu &menu(int i) const { return mMenus[(size_t)i]; }
        artboard::Rect titleRect(int i) const;
        artboard::Rect itemRect(int menu, int item) const;
        /** Where an item's shortcut was last DRAWN (local coordinates; empty if it has none or the
         *  menu has not been painted) — what a test checks the right-aligned column against. */
        artboard::Rect shortcutRect(int menu, int item) const;
        /** The row at a point of the open dropdown: its index (separators and disabled rows
         *  included), or -1. */
        int itemIndexAt(const artboard::Point &local) const { return mOpen >= 0 ? itemAt(mOpen, local) : -1; }
        /** The row the pointer or the keyboard stands on in the open menu (-1 = none) — never a
         *  separator, never a disabled row — and its LIVE eased wash, 0 … 1. */
        int cursorItem() const { return mCursor; }
        double itemHoverAmount(int item) const { return mItemHover.amount(item); }

        /** The keyboard, for an open menu (R-FAMILY-1): Down / Up move the cursor, skipping
         *  separators and disabled rows and wrapping; Home / End jump; Left / Right open the
         *  neighbouring menu; Enter runs the row under the cursor; Escape closes. Returns true when
         *  the key was consumed. Opt-in: nothing calls it until a host routes keys here. */
        bool menuKey(const artboard::KeyEvent &e);

        std::function<void(int)> onOpenChanged;  // index, or -1 = none open

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        void onOverlay(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override;

    private:
        void setOpen(int idx);
        double titleW(int i) const;
        double titleX(int i) const;
        artboard::Rect dropdownRect(int i) const;
        int titleAt(const artboard::Point &p) const;
        int itemAt(int menu, const artboard::Point &p) const;
        double itemTop(int menu, int item) const;      // y of a row inside the dropdown's content
        double itemHeight(int menu, int item) const;   // a row's height: a separator is shorter
        bool hasShortcuts(int menu) const;
        int stepCursor(int from, int dir) const;       // the next selectable row, wrapping; -1 = none

        std::vector<Menu> mMenus;
        int mOpen = -1;
        artboard::AnimatedProperty mHiCenter;  // highlight centre-x (slides between titles)
        artboard::AnimatedProperty mHiW;       // highlight width (grows from 0 / resizes / shrinks)
        artboard::AnimatedProperty mDropReveal;  // dropdown open progress 0..1 (grows the panel)
        bool mAnimPending = false;
        bool mDropRevealPending = false;       // (re)start the dropdown grow on the next advance
        bool mOpenFromClosed = false;          // this open transition started from nothing open

        // Hover (R-G-3): a non-active title lifts toward white; an open dropdown's
        // hovered item washes. Each title/item cross-fades independently.
        HoverFade mTitleHover;
        HoverFade mItemHover;

        // R-FAMILY-1: the row the pointer or the keyboard stands on. One wash for both, so moving
        // from one to the other cross-fades; the keyboard's survives the pointer being elsewhere.
        int mCursor = -1;
        bool mCursorByKey = false;
        // Measured in the paint (measurement is only valid there): the open menu's widest label and
        // widest shortcut, and where each shortcut was drawn. -1 = not measured yet -> estimate.
        mutable int mMeasuredMenu = -1;
        mutable double mMeasuredLabelW = 0.0, mMeasuredShortcutW = 0.0;
        mutable std::vector<artboard::Rect> mShortcutRects;
    };
}
}
