/*
 *  Cosmo by arstro — Filmstrip: a horizontal strip showing the CURRENT group's
 *  children — image thumbnails and sub-group chips. It is presentational: the host
 *  (CosmoApp) owns the group tree + selection and feeds cells via setCells() and the
 *  highlighted set via setSelection(); the strip reports clicks. Selection modifiers:
 *  plain = single, Shift = range, Ctrl/Cmd = toggle. Double-click activates (drill
 *  into a group); right-click requests a context menu at the pointer.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo
{
    class Filmstrip : public artboard::Segment
    {
    public:
        explicit Filmstrip(const artboard::Color &accent);

        struct Cell { bool group = false; int node = -1; int thumbSlot = -1; std::string name; int count = 0; };

        /** Register a thumbnail for an image slot (one per image, in slot order). */
        void addThumb(const uint8_t *rgba, int w, int h);
        /** Replace the visible cells (the current group's children). */
        void setCells(std::vector<Cell> cells);
        /** Highlight these cell indices; `primary` gets the strong ring. */
        void setSelection(const std::vector<int> &sel, int primary);
        int cellCount() const { return (int)mCells.size(); }

        std::function<void(int, bool, bool)> onSelect;       // cell index, shift, ctrl
        std::function<void(int)> onActivate;                 // double-click (drill into group)
        std::function<void(int, double, double)> onContext;  // right-click + world position

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &localPoint) override;

    private:
        int cellAt(const artboard::Point &local) const;
        void placeThumbs();

        artboard::Color mAccent;
        std::vector<std::shared_ptr<artboard::ImageView>> mThumbs;  // indexed by image slot
        std::vector<Cell> mCells;
        std::vector<int> mSel;
        int mPrimary = -1;
    };
}
}
