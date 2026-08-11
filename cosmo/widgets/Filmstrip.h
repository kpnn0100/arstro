/*
 *  cosmo_v2 by arstro — Filmstrip: the horizontal thumbnail strip below the
 *  breadcrumb (App.tsx: `flex items-center gap-1.5 px-3 bg-[#121212]
 *  border-t border-border h-[86px]`). Presentational only, mirroring cosmo's
 *  own Filmstrip contract (the host owns the group tree + selection and
 *  feeds cells via setCells(); thumbnails are pre-decoded and registered via
 *  addThumb() in slot order) -- new geometry/paint matching the Figma spec:
 *  86x62px photo cells with a 2px accent ring + 2px offset when primary-
 *  selected, 78x62px dashed folder chips, an active-cell name label bar.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "HoverFade.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class Filmstrip : public artboard::Segment
    {
    public:
        static constexpr double kHeight = 86.0;

        struct Cell
        {
            bool group = false;
            int node = -1;
            int thumbSlot = -1;   // image leaf only -- index into addThumb() order
            std::string name;
            int count = 0;        // group leaf only
            bool bypassed = false;  // R-BYPASS-5: this node's filter is disabled
        };

        Filmstrip();

        /** Registers one ImageView per image slot, in slot order (call once per
         *  newly opened image, matching cosmo's own Filmstrip::addThumb). Reuses the
         *  pool grown by earlier projects (see clearThumbs). */
        void addThumb(const uint8_t *rgba, int w, int h);
        /** Reset to zero active thumbnails (a new workspace) so addThumb restarts at
         *  slot 0, in lockstep with the engine's reset slot ids. */
        void clearThumbs();
        void setCells(std::vector<Cell> cells);
        void setSelection(std::vector<int> selCells, int primaryCell);
        int cellCount() const { return (int)mCells.size(); }
        void scrollBy(double delta);

        std::function<void(int cell, bool shift, bool ctrl)> onSelect;
        std::function<void(int cell)> onActivate;                         // double-click (drill into a group)
        std::function<void(int cell, double x, double y)> onContext;      // right-click

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        void onOverlay(artboard::IRenderTarget &t) const override;  // selection rings/name ABOVE thumbnails
        void advance(double nowMs) override;  // slides the primary ring between cells
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        static constexpr double kCellH = 62.0, kPhotoW = 86.0, kFolderW = 78.0;
        static constexpr double kGap = 4.875, kPadX = 9.75;

        double cellX(int i) const;
        double cellW(int i) const { return mCells[i].group ? kFolderW : kPhotoW; }
        int cellAt(double localX) const;
        void positionThumbs();  // re-place visible thumbnails at the current scroll

        std::vector<std::shared_ptr<artboard::ImageView>> mThumbs;  // pool of ImageView children (child = never removed)
        int mThumbCount = 0;   // active thumbnails for the current workspace (indexes into mThumbs)
        std::vector<Cell> mCells;
        std::vector<int> mSel;
        int mPrimary = -1;
        // Horizontal scroll eases toward mScrollTarget (R-G-1) rather than jumping
        // per wheel notch; cellX() reads the animated value so cells glide.
        artboard::AnimatedProperty mScrollX{0.0};
        double mScrollTarget = 0.0;
        double mScrollLastTarget = 0.0;
        // The primary-selection ring slides to the newly-selected cell (mRingPos =
        // a fractional cell index, converted to x/width in onPaint so it also
        // follows the cell during scroll).
        artboard::AnimatedProperty mRingPos;
        int mRingTarget = -1;
        bool mRingInit = false;
        // Per-cell hover wash, tracked from Move; each cell cross-fades (R-G-3).
        HoverFade mHover;
        // R-BYPASS-5: per-cell "filter disabled" amount, eased 0..1 so toggling a
        // cell's bypass fades its badge + wash in/out instead of popping (R-G-1).
        // HoverFade can't back this (it models ONE hovered item; any number of cells
        // can be bypassed at once), so it's a plain per-cell fade of the same shape.
        std::vector<double> mByAmt;
        double mByLastMs = -1.0;
        void advanceBypassFades(double nowMs);
        double bypassAmount(int cell) const;
    };
}
}
