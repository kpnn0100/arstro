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
    class MenuStrip : public artboard::Segment
    {
    public:
        struct Item { std::string label; std::function<void()> action; };
        struct Menu { std::string title; std::vector<Item> items; };

        MenuStrip();
        void addMenu(Menu m);

        int openIndex() const { return mOpen; }
        void close();
        /** True if a local-space point is on the bar or the open dropdown. */
        bool pointInActiveArea(const artboard::Point &local) const;
        /** Total width of all titles laid end to end (for TopBar layout). */
        double contentWidth() const;

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

        std::vector<Menu> mMenus;
        int mOpen = -1;
        artboard::AnimatedProperty mHiCenter;  // highlight centre-x (slides between titles)
        artboard::AnimatedProperty mHiW;       // highlight width (grows from 0 / resizes / shrinks)
        artboard::AnimatedProperty mDropReveal;  // dropdown open progress 0..1 (grows the panel)
        bool mAnimPending = false;
        bool mDropRevealPending = false;       // (re)start the dropdown grow on the next advance
        bool mOpenFromClosed = false;          // this open transition started from nothing open

        // Hover (R-G-1): a non-active title lifts toward white; an open dropdown's
        // hovered item washes with hoverWash. Both indices tracked from Move.
        int mHoverTitle = -1;
        int mHoverItem = -1;
        bool mTitleHoverPrev = false;
        bool mItemHoverPrev = false;
        artboard::AnimatedProperty mTitleHoverAmt{0.0};
        artboard::AnimatedProperty mItemHoverAmt{0.0};
    };
}
}
