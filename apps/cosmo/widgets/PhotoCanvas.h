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
 *
 *  THE PHOTO DISSOLVES (R-VIEW-1). Dragging a slider is the one place where the
 *  photo itself changes, and replacing the pixels in one frame made a drag read
 *  as a stutter of separate pictures — exactly what R-G-1 forbids. So the stage
 *  holds TWO stacked image views and the top one's opacity IS the dissolve: the
 *  composite is a*top + (1-a)*bottom, which is a true cross-dissolve because the
 *  layer being covered stays opaque instead of fading out and letting the canvas
 *  through. A render lands in whichever view is hidden and the opacity eases
 *  toward it, so successive renders dissolve in alternating directions and no
 *  pixels are ever copied between the two.
 *
 *  Two rules keep that composite CONTINUOUS, and the first version of this file
 *  broke both, which is what the user saw as the photo blinking:
 *
 *  1. Nothing is written into a layer that is on screen (R-VIEW-1a). A render
 *     arriving mid-dissolve is HELD and applied when the dissolve settles, at
 *     the moment the hidden view's weight is exactly zero. Opacity being smooth
 *     is not enough — swapping the PIXELS under weight (1-a) is a step of that
 *     size, and mid-drag that is nearly every render.
 *  2. The covered layer is skipped only while the layer above is exactly opaque,
 *     decided from THIS frame's alpha (R-VIEW-1e). Reading the previous frame's
 *     value hid the base for the first frame of every 1->0 dissolve and let the
 *     canvas through the partly-transparent top: a one-frame darkening on every
 *     other render.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include "HoverFade.h"
#include "SegmentedControl.h"
#include "CropOverlay.h"
#include "MaskOverlay.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class PhotoCanvas : public artboard::Segment
    {
    public:
        enum Mode { Before = 0, Split = 1, After = 2 };

        PhotoCanvas();

        /** Show these pixels (R-VIEW-1): they cross-dissolve onto whatever is on screen. The
         *  caller hands over pixels, not a view, so the pair below stays an implementation
         *  detail — and every path that changes the photo (a new render, a Before/After
         *  toggle) gets the dissolve for free. Sets instead of dissolving only when there is
         *  nothing to dissolve from (first photo, R-VIEW-1b) or when the frame's pixel size
         *  differs and therefore cannot cover what is on screen (R-VIEW-1c). */
        void setPhoto(const uint8_t *rgba, int w, int h, double nowMs);
        /** Back to an empty stage (a new workspace): both views, no dissolve. */
        void clearPhoto();

        std::shared_ptr<artboard::ImageView> beforeView() { return mBeforeView; }  // left-half split image
        std::shared_ptr<MaskOverlay> maskOverlay() { return mMaskOverlay; }        // on-photo mask editor
        std::shared_ptr<CropOverlay> cropOverlay() { return mCropOverlay; }        // on-photo crop box (R-CROP-5)
        /** While set, an arriving render is taken IMMEDIATELY instead of cross-dissolving
         *  (R-CROP-7). R-VIEW-1 dissolves because a CONTENT change must not pop; consecutive
         *  frames of a zoom are not a content change, and dissolving them would smear the
         *  motion and — through R-VIEW-1a's hold — drop most of the frames. R-VIEW-1c already
         *  carried this reasoning for a differently-shaped frame; this states it as a mode
         *  rather than leaving it to a shape comparison to notice. */
        void setGeometricTransition(bool on) { mGeometric = on; }
        /** The photo's display rect in canvas-local coords, zoom and pan included. Public
         *  because a test that aims at the crop box has to convert normalised crop coordinates
         *  to pixels the same way the overlay does. */
        artboard::Rect photoFittedRect() const;

        int mode() const { return mPill->selected(); }
        bool showAfter() const { return mPill->selected() != Before; }  // "show the edited result?"
        void setMode(int m) { mPill->setSelectedImmediate(m); applyMode(); }
        std::function<void(int)> onModeChange;      // 0=before, 1=split, 2=after

        // ── R-VIEW-3: the split seam. Public because a test that drives the ASSEMBLED app
        //    has to ask where the seam IS and whether a point counts as on it; restating the
        //    pick radius in the test is how a test comes to aim where the widget does not.
        /** Where the seam is drawn, in canvas-local px. */
        double seamX() const { return mSplitPos.value() * width.value(); }
        /** Is `local` close enough to the seam to count as a grab? False whenever the seam is
         *  not on screen, so a press in Before/After mode still reaches the pan. */
        bool onSeam(const artboard::Point &local) const;
        std::function<void(double, double)> onContext;  // right-click on the photo (world x,y)

        // ── zoom / pan (ctrl-scroll magnify + drag-to-pan) ──
        // Routed through here so the after AND before(split) views share one
        // zoom/pan state and the split seam stays pixel-aligned (R-ZOOM-3).
        double zoom() const { return mPhotoBase->zoom(); }
        void zoomAbout(double factor, const artboard::Point &localInCanvas);  // localInCanvas == PhotoCanvas-local == ImageView-local
        void resetZoom();

        void layout();  // call after width/height changes
        void advance(double nowMs) override;  // starts the mode fade the pill could not (no nowMs)

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        void applyMode(bool immediate = false);  // record the wanted split state (advance tweens it)
        /** setPhoto's body, minus the wait: starts a dissolve (or sets both views when there is
         *  nothing to dissolve between). Only ever called when no dissolve is in flight. */
        void showPhoto(const uint8_t *rgba, int w, int h, double nowMs);
        /** Do these two frames fit the same rect? The dissolve needs the incoming photo to cover
         *  the one on screen, and that is a question about SHAPE, not about pixel count — the same
         *  photo at a different preview resolution fits identically (R-VIEW-1c). */
        static bool sameShape(int w1, int h1, int w2, int h2);
        /** The view the photographer is actually looking at — the top one once the dissolve has
         *  carried it past halfway. What `layout()` measures the mask overlay against. */
        artboard::ImageView *visibleView() const;

        // The dissolving pair: fixed in z-order (base below, top above), so the only thing that
        // moves is mPhotoTop->opacity and `mTopIsCurrent` says which one holds the newest pixels.
        std::shared_ptr<artboard::ImageView> mPhotoBase;
        std::shared_ptr<artboard::ImageView> mPhotoTop;
        bool mTopIsCurrent = false;
        std::shared_ptr<artboard::Segment> mSplitClip;    // clips the before image to the left half
        std::shared_ptr<artboard::ImageView> mBeforeView;
        std::shared_ptr<artboard::RectangleSegment> mDivider;
        std::shared_ptr<MaskOverlay> mMaskOverlay;   // above the image, below the pill (z-order)
        // Above the mask overlay and below the pill. Only one of the two is ever active — the
        // Mask tab and the Xform tab are different tabs — so their gestures cannot collide, and
        // an inactive overlay is click-through by contract.
        std::shared_ptr<CropOverlay> mCropOverlay;
        std::shared_ptr<SegmentedControl> mPill;
        artboard::Point mPanLast{0, 0};   // previous drag position while panning a zoomed view
        bool mSplitWanted = false;        // pill state; advance() eases the clip + seam to it
        bool mSplitApplied = false;
        // ── R-VIEW-3: the seam's position, and the state of dragging it ──
        //
        // PRESENTATION, so it lives here and not in AppModel (R-SVC-4): it is where THIS
        // window is comparing, not something the project contains. Not persisted, and a
        // second front end is free to have its own.
        //
        // An AnimatedProperty even though a drag writes it directly: the drag is direct
        // manipulation, where the pointer IS the animation and easing would read as lag —
        // but a double-click returns it to the centre, and that the app does on the user's
        // behalf, so it eases (R-G-1). One property serves both because `set` and
        // `animateTo` are the two ways to write it.
        artboard::AnimatedProperty mSplitPos{0.5};
        bool mSeamDrag = false;           // a press landed on the seam and has not been released
        double mSeamGrabDX = 0.0;         // pointer-to-seam offset at the grab (D-32: no teleport)
        HoverFade mSeamHover;             // eased brighten, so the seam looks grabbable
        double mNowMs = 0.0;              // last advance()'s clock, for the re-centre tween
        // The render that arrived while a dissolve was in flight (R-VIEW-1a). One slot, newest
        // wins; the buffer keeps its capacity so a drag does not reallocate per frame.
        std::vector<uint8_t> mHeld;
        int mHeldW = 0, mHeldH = 0;
        bool mHeldPending = false;
        bool mGeometric = false;   // a zoom is in flight: take frames whole, do not dissolve
    };
}
}
