/*
 *  cosmo_v2 by arstro — ContextMenu: a small right-click popup (a list of
 *  labelled actions) shown at the cursor, ported in spirit from cosmo's
 *  ContextMenu. Painted in the overlay pass on top of everything and modal
 *  while open (captures the next click). A DIRECT child of the root so raising
 *  it wins hit-testing over the body widgets it overlaps.
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
        struct Item { std::string label; std::function<void()> action; };

        ContextMenu() { focusable = true; }  // rename mode takes keyboard focus (DR-TREE-5)

        void open(std::vector<Item> items, double x, double y);
        void close() { mOpen = false; mRenaming = false; }
        bool isOpen() const { return mOpen; }

        // Rename mode (DR-TREE-5): morph an open menu into an inline rename field, or
        // open one directly (top-bar name click). `onRename` fires with the typed name
        // on Enter. `currentName` seeds the field and starts selected (type to replace).
        void enterRenameMode(const std::string &currentName);
        void openRename(const std::string &currentName, double x, double y);
        bool isRenaming() const { return mOpen && mRenaming; }
        std::function<void(const std::string &)> onRename;

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

        std::vector<Item> mItems;
        double mX = 0, mY = 0;
        bool mOpen = false;
        bool mWasOpen = false;
        artboard::AnimatedProperty mAppear{0.0};    // open/close fade+rise (R-G-1)
        HoverFade mHover;                           // per-item hover cross-fade (R-G-3)

        bool mRenaming = false;
        std::string mRenameText;
        bool mSelectAll = false;                    // whole field selected: first edit replaces it
        double mPreRenameItemsH = 0.0;              // items height captured when rename began (to shrink from)
        double mNowMs = 0.0;
        artboard::AnimatedProperty mRename{0.0};    // 0 = menu, 1 = collapsed header + expanded field
    };
}
}
