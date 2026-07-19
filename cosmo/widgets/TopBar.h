/*
 *  cosmo_v2 by arstro — TopBar: wordmark, menu bar, filename readout, and the
 *  left-rail toggle. Pixel spec: App.tsx's top bar
 *  (`flex items-center h-9 border-b border-border px-3 gap-3`).
 *
 *  The menu bar is a MenuStrip child (real dropdowns with the original cosmo's
 *  options, centred titles, an animated highlight); TopBar only positions it.
 *
 *  Conversion note (see Theme.h doc comment / plan): the Figma source sets
 *  `html { font-size: 13px }`, so every rem-based Tailwind utility below is
 *  scaled to 13/16 of its nominal value: 1 spacing unit = 0.25rem = 3.25px.
 *    h-9 = 29.25px   px-3 = 9.75px   gap-3 = 9.75px   mr-1 = 3.25px
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "MenuStrip.h"
#include "IconButton.h"
#include <functional>
#include <memory>
#include <string>

namespace arstro
{
namespace cosmo_v2
{
    class TopBar : public artboard::Segment
    {
    public:
        static constexpr double kHeight = 29.25;

        TopBar();

        std::shared_ptr<MenuStrip> menuStrip() { return mMenuStrip; }
        void setFilename(const std::string &name, int index, int total);  // total<=0 hides it
        // Show a group's name in the right slot (in place of a filename) as a clickable,
        // hovered affordance that opens rename (DR-TOPBAR); empty clears back to filenames.
        void setGroupName(const std::string &name);
        void setProjectName(const std::string &name) { mProjectName = name; }  // shown centred
        void setRailOpen(bool open);

        std::function<void()> onRailToggle;
        std::function<void()> onHome;       // clicking the "cosmo." wordmark returns to the launcher
        std::function<void()> onNameClick;  // clicking the group name -> rename (DR-TREE-5)

        void advance(double nowMs) override;  // eases the group-name hover
        void layout();  // call after width changes

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;

    private:
        static constexpr double kPad = 9.75;

        artboard::Rect wordmarkRect() const;
        artboard::Rect groupNameRect() const;  // hit/hover box for the group name (empty if none)

        std::shared_ptr<MenuStrip> mMenuStrip;
        std::shared_ptr<IconButton> mRailToggle;
        std::string mFilename;
        std::string mGroupName;   // non-empty => right slot shows this group name, clickable
        std::string mProjectName;
        int mFileIndex = 0, mFileTotal = 0;
        bool mRailOpen = true;
        artboard::AnimatedProperty mNameHover{0.0};
        bool mNameHovered = false;
        double mNowMs = 0.0;
    };
}
}
