/*
 *  cosmo_v2 by arstro — CropOverlay: the interactive crop box drawn over the photo (R-CROP-5).
 *
 *  PARITY #3: cosmo had numeric crop only, so the aspect chips could pick a shape and nothing
 *  could choose a REGION. This is the missing half — drag a corner or an edge to resize, drag
 *  inside to move, with the discarded area dimmed and a thirds grid over what is kept.
 *
 *  It is a sibling of MaskOverlay and follows it deliberately: same place in the z-order (above
 *  the image, below the compare pill), same "click-through while inactive" contract, same
 *  normalised coordinate space, and the same rule that a gesture in flight outranks the model
 *  (D-50). Inactive — the Xform tab closed — it must be completely transparent to input, or it
 *  would steal the zoom/pan drag and the split seam.
 *
 *  All the arithmetic lives in CropGeometry.h, shared with XformPanel, so a corner dragged here
 *  and a ratio typed there cannot disagree.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include "CropGeometry.h"
#include "HoverFade.h"
#include <functional>

namespace arstro
{
namespace cosmo_v2
{
    class CropOverlay : public artboard::Segment
    {
    public:
        CropOverlay();

        /** The crop changed (normalised 0..1 of the source), and whether the gesture is still
         *  in flight. `live` lets the caller send an interactive edit while dragging and a
         *  final one on release, which is what R-PREVIEW-1's coarse-then-refine wants. */
        std::function<void(double x, double y, double w, double h, bool live)> onChange;

        /** The photo's display rect in this overlay's local space — from `ImageView::fittedRect`,
         *  so it already includes zoom and pan (as R-MASK-3 does for the mask). Everything is
         *  drawn and hit-tested inside it: the crop is a region of the PHOTO, not of the canvas,
         *  and using the canvas would put the box in the letterbox. */
        void setFittedRect(const artboard::Rect &localFitted) { mFitted = localFitted; }

        /** Show/hide. Hidden means click-through: `hitTestSelf` returns false so a press goes to
         *  the pan and the seam exactly as if this widget were not there. */
        void setActive(bool on) { mActive = on; }
        bool active() const { return mActive; }

        /** Adopt the crop from the model. **Ignored while a gesture is in flight** — R-SVC-12
         *  re-seeds every view whenever a frame lands, and during a drag that is every few tens
         *  of milliseconds, so this would fight the pointer. That is D-50, which cost the curve
         *  editor its bezier handles; the rule is written once in `CurvePanel::setCurves`. */
        void setCrop(double x, double y, double w, double h)
        {
            if (mPart != crop::Part::None) return;
            mCrop = artboard::Rect{x, y, w, h};
        }

        /** The locked pixel ratio (w/h), or 0 for Free (R-CROP-2). */
        void setAspectLock(double pixelRatio) { mRatio = pixelRatio; }
        /** The photo's full-resolution shape, needed to turn the pixel ratio into a normalised
         *  one (R-CROP-1). */
        void setSourceSize(int w, int h) { mSrcW = w; mSrcH = h; }

        artboard::Rect cropRect() const { return mCrop; }
        /** Which part of the box `local` is on — for a test, and for the hover feedback. */
        crop::Part partAt(const artboard::Point &local) const;

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        /** Click-through while inactive — the MaskOverlay contract, and for the same reason:
         *  an overlay that swallows presses it has no use for is how four panels once made the
         *  whole editor unclickable. */
        bool hitTestSelf(const artboard::Point &p) const override
        {
            return mActive && localBounds().contains(p);
        }

    private:
        /** Normalised (0..1 of the photo) <-> overlay-local pixels, through the fitted rect. */
        artboard::Rect boxPx() const;
        double nxOf(double localX) const;
        double nyOf(double localY) const;
        /** The normalised ratio the current lock implies, or 0 when free/unknown. */
        double normRatio() const { return crop::normalisedRatio(mRatio, mSrcW, mSrcH); }
        void emitChange(bool live);

        artboard::Rect mFitted{0, 0, 0, 0};
        artboard::Rect mCrop{0, 0, 1, 1};
        bool mActive = false;
        double mRatio = 0.0;          // pixel w/h, 0 = free
        int mSrcW = 0, mSrcH = 0;

        crop::Part mPart = crop::Part::None;   // what is being dragged, None = nothing
        double mGrabDX = 0.0, mGrabDY = 0.0;   // pointer-to-anchor offset (D-32: no teleport)
        crop::Part mHoverPart = crop::Part::None;
        HoverFade mHover;
        artboard::AnimatedProperty mAppear{0.0};   // R-G-1: it fades in with the tab
        bool mAppearWanted = false;
    };
}
}
