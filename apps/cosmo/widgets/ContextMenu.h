/*
 *  cosmo_v2 by arstro — ContextMenu: a small right-click popup (a list of
 *  labelled actions) shown at the cursor, ported in spirit from cosmo's
 *  ContextMenu. Painted in the overlay pass on top of everything and modal
 *  while open (captures the next click). A DIRECT child of the root so raising
 *  it wins hit-testing over the body widgets it overlaps.
 *
 *  R-FAMILY-1 (2026-11-17), the same three opt-in fields as MenuStrip::Item, so a `{label, action}`
 *  item draws as it always did: a right-aligned mono SHORTCUT (the menu then takes its width from
 *  measured text), a SEPARATOR hairline row, a dimmed DISABLED row. An item's index counts every
 *  row. `menuKey` is the keyboard's way through it, for a host that routes keys here.
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
    class ContextMenu : public artboard::Segment
    {
    public:
        struct Item
        {
            std::string label;
            std::function<void()> action;
            // R-FAMILY-1 — after `action`, with defaults, so `{label, action}` still initialises one.
            std::string shortcut;      // drawn right-aligned, mono, muted; never parsed
            bool enabled = true;       // false: dimmed, no hover wash, a click does nothing
            bool separator = false;    // true: a hairline row (label and action unused)
            static Item sep() { Item i; i.separator = true; return i; }
            bool selectable() const { return enabled && !separator; }
        };

        ContextMenu() { focusable = true; }  // rename mode takes keyboard focus (DR-TREE-5)

        void open(std::vector<Item> items, double x, double y);
        /** The open items in other words, nothing closed or moved (opt-in: Solaris, R-ACCESS-2; cosmo never calls it). */
        void relabel(const std::vector<std::string> &labels)
        {
            if (labels.size() != mItems.size()) return;
            for (size_t i = 0; i < labels.size(); ++i) mItems[i].label = labels[i];
        }
        void close() { mOpen = false; mRenaming = false; }
        bool isOpen() const { return mOpen; }
        /** Read-only contents and geometry (local coordinates) — what a test aims a click at. */
        int itemCount() const { return (int)mItems.size(); }
        const Item &item(int i) const { return mItems[(size_t)i]; }
        artboard::Rect itemRect(int i) const;
        /** Where item `i`'s shortcut was last DRAWN (empty if it has none, or before the first paint). */
        artboard::Rect shortcutRect(int i) const
        {
            return mMeasured && i >= 0 && i < (int)mShortcutRects.size() ? mShortcutRects[(size_t)i] : artboard::Rect{};
        }
        /** The row at a point: its index (separators and disabled rows included), or -1. */
        int itemIndexAt(const artboard::Point &local) const { return itemAt(local); }
        /** The row the pointer or the keyboard stands on (-1 = none; never a separator or a disabled
         *  row), and its LIVE eased wash. */
        int cursorItem() const { return mOpen ? mHover.hovered() : -1; }
        double itemHoverAmount(int i) const { return mHover.amount(i); }
        /** The keyboard, while the menu is open and not renaming (R-FAMILY-1): Down / Up move the
         *  cursor, skipping separators and disabled rows and wrapping; Home / End jump; Enter runs
         *  the row; Escape closes. True when consumed. Opt-in: a host routes keys here. */
        bool menuKey(const artboard::KeyEvent &e);

        // Rename mode (DR-TREE-5): morph an open menu into an inline rename field, or
        // open one directly (top-bar name click). `onRename` fires with the typed name
        // on Enter. `currentName` seeds the field and starts selected (type to replace).
        void enterRenameMode(const std::string &currentName);
        void openRename(const std::string &currentName, double x, double y);
        bool isRenaming() const { return mOpen && mRenaming; }
        std::function<void(const std::string &)> onRename;
        std::string renameTitle = "Rename"; // the rename header (opt-in: Solaris draws it in its language, R-ACCESS-2)

        void advance(double nowMs) override;  // drives the open/close + hover + rename morph

    protected:
        void onOverlay(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool handleKey(const artboard::KeyEvent &e) override;
        bool hitTestSelf(const artboard::Point &p) const override { (void)p; return mOpen; }  // modal

    private:
        artboard::Rect menuRect() const;        // outer bg rect (animates in rename mode)
        artboard::Rect fieldRect() const;        // the light rename textbox, once expanded
        int itemAt(const artboard::Point &local) const;
        double renameContentH() const;           // content height at the current morph amount
        double itemHeight(int i) const;          // a separator row is shorter
        double itemTop(int i) const;             // y of row i inside the content (i == count: the total)
        bool hasShortcuts() const;
        int stepCursor(int from, int dir) const;

        std::vector<Item> mItems;
        double mX = 0, mY = 0;
        bool mOpen = false;
        bool mWasOpen = false;
        artboard::AnimatedProperty mAppear{0.0};    // open/close fade+rise (R-G-1)
        HoverFade mHover;                           // per-item hover cross-fade (R-G-3)
        bool mCursorByKey = false;                  // the keyboard put the cursor where it is (R-FAMILY-1)
        // Measured in the paint, for a menu with a shortcut column: widest label, widest shortcut.
        mutable bool mMeasured = false;
        mutable double mMeasuredLabelW = 0.0, mMeasuredShortcutW = 0.0;
        mutable std::vector<artboard::Rect> mShortcutRects;

        bool mRenaming = false;
        std::string mRenameText;
        bool mSelectAll = false;                    // whole field selected: first edit replaces it
        double mPreRenameItemsH = 0.0;              // items height captured when rename began (to shrink from)
        double mNowMs = 0.0;
        artboard::AnimatedProperty mRename{0.0};    // 0 = menu, 1 = collapsed header + expanded field
    };
}
}
