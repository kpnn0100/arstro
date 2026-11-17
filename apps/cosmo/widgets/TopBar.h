/*
 *  cosmo_v2 by arstro — TopBar: wordmark, menu bar, the project name, the filename
 *  readout, the left-rail toggle and the Export button. Pixel spec: App.tsx's top bar
 *  (`flex items-center h-9 border-b border-border px-3 gap-3`).
 *
 *  Two things here are not in the mock (R-UX-4, R-UX-6):
 *   * **Export** — the accent-filled button at the right end. Exporting photos is the
 *     editor's primary action and had no button at all (File ▸ Export… was the only route),
 *     while the filled button in the action bar was a PRESET save.
 *   * **The project name is fitted.** It was drawn centred at its full width with an
 *     estimated metric, so a long name ran over the menu strip. It is now measured, centred
 *     in the bar when that fits, else centred in the free span between the menus and the
 *     right-hand group, and ellipsized to that span.
 *
 *  The family standard (R-FAMILY-9, 2026-11-17) changed three things:
 *   * **The project name sits LEFT**, directly after the menus (it was centred), followed by
 *     the `DirtyDot` while the project has unsaved changes.
 *   * **A save icon button** left of Export: dim when there is nothing to save, lit while the
 *     project is unsaved; a click saves. One eased value (the dot's) lights both, so the dot
 *     and the icon can never disagree.
 *   * The wordmark is drawn by `Wordmark::draw` - the accent dot at the MEASURED end of the name.
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
#include "../../../core/Artboard/include/artboard/artboard.h"
#include "MenuStrip.h"
#include "IconButton.h"
#include "DirtyDot.h"
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
        void setProjectName(const std::string &name) { mProjectName = name; }  // shown left, after the menus (R-FAMILY-9)
        /** R-FAMILY-9: whether the project has unsaved changes (the model's flag). A setter has
         *  no clock - the dot and the save icon ease from the next `advance`. */
        void setDirty(bool dirty) { mDirty.set(dirty); }
        bool dirty() const { return mDirty.target(); }
        /** LIVE, eased 0 … 1: the dot's opacity and how lit the save icon is. */
        double dirtyAmount() const { return mDirty.amount(); }
        /** Where the dot was DRAWN in the last paint (this bar's space; {0,0} when not drawn). */
        artboard::Point dirtyDotCentre() const { return mDotCentre; }
        /** The save icon button's box (this bar's space). */
        artboard::Rect saveRect() const;
        const std::string &projectName() const { return mProjectName; }
        void setRailOpen(bool open);
        /** R-UX-4: with no photo there is nothing to export - the button dims (eased) and does
         *  nothing, rather than looking live and opening an empty dialog. */
        void setExportEnabled(bool enabled) { mExportWanted = enabled; }
        /** Geometry and text a test or a shot aims at: the Export button's box, and the project
         *  name AS DRAWN in the last paint (its box in this bar's space; empty when not drawn). */
        artboard::Rect exportRect() const;
        artboard::Rect titleRect() const { return mTitleRect; }
        const std::string &titleDrawn() const { return mTitleDrawn; }
        double exportEnabledAmount() const { return mExportEnable.value(); }   // live, eased

        std::function<void()> onRailToggle;
        std::function<void()> onHome;       // clicking the "cosmo." wordmark returns to the launcher
        std::function<void()> onNameClick;  // clicking the group name -> rename (DR-TREE-5)
        std::function<void()> onExport;     // R-UX-4: the Export button -> the Export modal
        std::function<void()> onSave;       // R-FAMILY-9: the save icon (only while unsaved)

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
        // R-UX-4: the Export button (self-drawn: one region, its own eased hover + enabled).
        bool mExportWanted = true, mExportApplied = true, mExportHovered = false, mExportPressed = false;
        artboard::AnimatedProperty mExportHover{0.0}, mExportEnable{1.0};
        // R-UX-6: what the last paint drew for the project name (measurement needs a target).
        mutable artboard::Rect mTitleRect{0, 0, 0, 0};
        mutable std::string mTitleDrawn;
        // R-FAMILY-9: the unsaved dot (a value, drawn after the measured name) and the save icon.
        DirtyDot mDirty;
        mutable artboard::Point mDotCentre{0, 0};
        bool mSaveHovered = false, mSavePressed = false;
        artboard::AnimatedProperty mSaveHover{0.0};
    };
}
}
