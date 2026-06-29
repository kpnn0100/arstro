/*
 *  Cosmo by arstro — Filmstrip: a horizontal strip of image thumbnails for the
 *  open images. Clicking a cell selects that image; the selected cell gets an
 *  accent border. Thumbnails are small ImageViews (reusing the raster primitive).
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace arstro
{
namespace cosmo
{
    class Filmstrip : public artboard::Segment
    {
    public:
        explicit Filmstrip(const artboard::Color &accent);

        /** Append a thumbnail (straight RGBA8); copies the bytes into an ImageView. */
        void addThumb(const uint8_t *rgba, int w, int h);
        void setSelected(int idx) { mSelected = idx; mSelection = idx >= 0 ? std::vector<int>{idx} : std::vector<int>{}; }
        int selected() const { return mSelected; }
        int count() const { return (int)mThumbs.size(); }
        /** Indices in the multi-selection (alt-click extends it); always includes the primary. */
        const std::vector<int> &selection() const { return mSelection; }

        std::function<void(int)> onSelect;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &localPoint) override;

    private:
        artboard::Color mAccent;
        int mSelected = -1;
        std::vector<int> mSelection;  // multi-selection (sync target); includes the primary
        std::vector<std::shared_ptr<artboard::ImageView>> mThumbs;
    };
}
}
