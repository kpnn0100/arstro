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
        };

        Filmstrip();

        /** Registers one ImageView per image slot, in slot order (call once per
         *  newly opened image, matching cosmo's own Filmstrip::addThumb). */
        void addThumb(const uint8_t *rgba, int w, int h);
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

        std::vector<std::shared_ptr<artboard::ImageView>> mThumbs;  // indexed by slot
        std::vector<Cell> mCells;
        std::vector<int> mSel;
        int mPrimary = -1;
        double mScrollX = 0.0;
        // The primary-selection ring slides to the newly-selected cell (mRingPos =
        // a fractional cell index, converted to x/width in onPaint so it also
        // follows the cell during scroll).
        artboard::AnimatedProperty mRingPos;
        int mRingTarget = -1;
        bool mRingInit = false;
    };
}
}
