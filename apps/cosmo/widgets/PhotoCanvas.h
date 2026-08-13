/*
 *  cosmo_v2 by arstro — PhotoCanvas: the photo stage. A #0A0A0A backdrop behind
 *  Artboard's ImageView (object-contain fit), with a THREE-state
 *  Before / Split / After switch overlaid bottom-center. The highlight rectangle
 *  slides between the three states and rounds the corner that hugs the switch's
 *  end (Before = left-rounded, Split = square, After = right-rounded).
 *
 *  Split shows the unedited "before" image on the left half (a clip container
 *  holding a full-canvas ImageView, so it stays pixel-aligned with the edited
 *  image behind it) and the edited "after" image on the right, divided by a
 *  1.5px seam.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include "SegmentedControl.h"
#include "MaskOverlay.h"
#include <functional>
#include <memory>

namespace arstro
{
namespace cosmo_v2
{
    class PhotoCanvas : public artboard::Segment
    {
    public:
        enum Mode { Before = 0, Split = 1, After = 2 };

        PhotoCanvas();

        std::shared_ptr<artboard::ImageView> imageView() { return mImageView; }   // main (after) image
        std::shared_ptr<artboard::ImageView> beforeView() { return mBeforeView; }  // left-half split image
        std::shared_ptr<MaskOverlay> maskOverlay() { return mMaskOverlay; }        // on-photo mask editor

        int mode() const { return mPill->selected(); }
        bool showAfter() const { return mPill->selected() != Before; }  // "show the edited result?"
        void setMode(int m) { mPill->setSelectedImmediate(m); applyMode(); }
        std::function<void(int)> onModeChange;      // 0=before, 1=split, 2=after
        std::function<void(double, double)> onContext;  // right-click on the photo (world x,y)

        // ── zoom / pan (ctrl-scroll magnify + drag-to-pan) ──
        // Routed through here so the after AND before(split) views share one
        // zoom/pan state and the split seam stays pixel-aligned (R-ZOOM-3).
        double zoom() const { return mImageView->zoom(); }
        void zoomAbout(double factor, const artboard::Point &localInCanvas);  // localInCanvas == PhotoCanvas-local == ImageView-local
        void resetZoom();

        void layout();  // call after width/height changes

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        void applyMode();  // toggle split clip + divider visibility from the pill state

        std::shared_ptr<artboard::ImageView> mImageView;
        std::shared_ptr<artboard::Segment> mSplitClip;    // clips the before image to the left half
        std::shared_ptr<artboard::ImageView> mBeforeView;
        std::shared_ptr<artboard::RectangleSegment> mDivider;
        std::shared_ptr<MaskOverlay> mMaskOverlay;   // above the image, below the pill (z-order)
        std::shared_ptr<SegmentedControl> mPill;
        artboard::Point mPanLast{0, 0};   // previous drag position while panning a zoomed view
    };
}
}
