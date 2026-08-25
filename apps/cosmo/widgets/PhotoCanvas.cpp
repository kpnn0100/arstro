#include "PhotoCanvas.h"
#include "TextMetrics.h"
#include "WidgetLog.h"
#include "../Theme.h"
#include <cmath>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace
    {
        constexpr double kPillH = 25.0;
        constexpr double kSegPadX = 11.375;  // px-3.5
        constexpr double kFontPx = 10.0;
        constexpr double kPillRadius = 6.0;   // "rounded like a background" — the highlight hugs this
        // The photo dissolve (R-VIEW-1): 160 ms, and LINEAR — the one place in cosmo that does not
        // take the house EaseOutCubic. A cross-fade is judged by its LARGEST single-frame step, and
        // an ease-out front-loads: at 16 ms of 120 ms, EaseOutCubic is already 35% of the way
        // across, so the first frame carried a third of the change and the rest crawled — which
        // reads as a partial cut, not a fade. Linear spreads the change evenly (10 frames, ~10%
        // each) and is what a video cross-dissolve does, for the same reason. 160 ms is long enough
        // for the steps to be small and short enough that a held render is never far behind the
        // slider (R-VIEW-1a's stated trade).
        constexpr double kPhotoFadeMs = 160.0;
        constexpr Easing kPhotoFadeEase = Easing::Linear;
        constexpr double kModeFadeMs = 180.0;   // Split's clip + seam: cosmo's cross-fade step
        // R-VIEW-3: the seam at rest and under the pointer. Bright enough at rest to read as a
        // deliberate edge rather than a rendering seam, and clearly brighter on hover so it
        // says "grab me" without a cursor the HAL does not have.
        constexpr double kSeamAlpha = 0.55;
        constexpr double kSeamAlphaHover = 0.95;
        // ...and it thickens slightly, because on a dark photo alpha alone is easy to miss.
        constexpr double kSeamW = 1.5;
        constexpr double kSeamWHover = 3.0;
    }

    PhotoCanvas::PhotoCanvas()
    {
        clipToBounds = true;
        // Two stacked after-views, fixed in z-order: only mPhotoTop's opacity moves (R-VIEW-1).
        // PhotoCanvas owns pan/zoom so every view on the stage moves together; both are
        // click-through so a drag falls through to us (R-ZOOM-3, as amended).
        for (auto *slot : {&mPhotoBase, &mPhotoTop})
        {
            *slot = std::make_shared<ImageView>();
            (*slot)->setFit(ImageView::Fit::Contain);
            (*slot)->inputTransparent = true;
            addChild(*slot);
        }
        mPhotoTop->opacity.set(0.0);   // nothing to dissolve from yet

        // Split: a left-half clip holding a full-canvas before-image, so it stays
        // aligned with the edited image behind it.
        mSplitClip = std::make_shared<Segment>();
        mSplitClip->clipToBounds = true;
        mSplitClip->inputTransparent = true;
        mBeforeView = std::make_shared<ImageView>();
        mBeforeView->setFit(ImageView::Fit::Contain);
        mSplitClip->addChild(mBeforeView);
        addChild(mSplitClip);

        mDivider = std::make_shared<RectangleSegment>();
        mDivider->style = {Paint::filled(palette::whiteAlpha(kSeamAlpha)), 0.0};
        mDivider->inputTransparent = true;
        addChild(mDivider);

        // On-photo mask editor. Added BEFORE the pill so the pill stays clickable
        // (topmost child wins on overlap); it is click-through until a mask is active.
        mMaskOverlay = std::make_shared<MaskOverlay>(palette::primary());
        addChild(mMaskOverlay);
        mCropOverlay = std::make_shared<CropOverlay>();
        addChild(mCropOverlay);   // R-CROP-5: above the mask overlay, below the compare pill

        mPill = std::make_shared<SegmentedControl>(std::vector<std::string>{"Before", "Split", "After"});
        mPill->containerBox = {Paint::filledStroked(Color{0x11 / 255.0, 0x11 / 255.0, 0x11 / 255.0, 0.9},
                                                    palette::border(), 1.0),
                               kPillRadius};
        mPill->idleSegBox = {Paint{}, 0.0};
        mPill->activeSegBox = {Paint::filled(palette::primary()), 0.0};  // inner corners square (Split)
        mPill->edgeRadius = kPillRadius;                                 // outer corners hug the tray
        mPill->idleText = {palette::mutedForeground(), kFontPx, font::sans()};
        mPill->activeText = {palette::white(), kFontPx, font::sansMedium()};
        mPill->padding = 0.0;
        mPill->gap = 0.0;
        mPill->setSelectedImmediate(After);  // "after" by default, matching the initial edited view
        mPill->onChange = [this](int idx) { applyMode(); if (onModeChange) onModeChange(idx); };
        addChild(mPill);

        applyMode(/*immediate=*/true);   // the first frame has nothing to fade from
        layout();
    }

    void PhotoCanvas::applyMode(bool immediate)
    {
        // R-VIEW-2 / R-G-1: the split half and its seam FADE. A setter with no `nowMs` cannot
        // start a tween, and the pill's onChange is exactly that, so record the wanted state and
        // let advance() ease toward it.
        mSplitWanted = (mPill->selected() == Split);
        if (!immediate) return;
        const double a = mSplitWanted ? 1.0 : 0.0;
        mSplitClip->opacity.set(a);
        mDivider->opacity.set(a);
        mSplitApplied = mSplitWanted;
    }

    void PhotoCanvas::advance(double nowMs)
    {
        mNowMs = nowMs;   // a gesture handler has no clock of its own (R-VIEW-3's re-centre)
        // R-VIEW-3: the seam's own tween and its hover brighten. `layout()` reads
        // `mSplitPos` every frame, so advancing it here is all the plumbing the eased
        // re-centre needs — and while a drag writes the value directly, `update` on a
        // settled property is a no-op, so the two paths cannot fight.
        const bool seamMoving = mSplitPos.isAnimating();
        mSplitPos.update(nowMs);
        mSeamHover.advance(nowMs);
        if (seamMoving) layout();

        if (mSplitApplied != mSplitWanted)
        {
            const double a = mSplitWanted ? 1.0 : 0.0;
            mSplitClip->opacity.animateTo(a, kModeFadeMs, Easing::EaseOutCubic, nowMs);
            mDivider->opacity.animateTo(a, kModeFadeMs, Easing::EaseOutCubic, nowMs);
            mSplitApplied = mSplitWanted;
            WLOG("photo: split %s over %.0fms", mSplitWanted ? "IN" : "OUT", kModeFadeMs);
        }
        Segment::advance(nowMs);   // every opacity now holds THIS frame's value

        // The render that arrived mid-dissolve goes in the moment the dissolve has settled
        // (R-VIEW-1a): the hidden view's weight is exactly zero, so writing its pixels changes
        // nothing on screen. Doing it here rather than in setPhoto is the whole fix — the frame
        // waits for a safe moment instead of stepping the composite.
        if (mHeldPending && !mPhotoTop->opacity.isAnimating())
        {
            mHeldPending = false;
            showPhoto(mHeld.data(), mHeldW, mHeldH, nowMs);
        }

        // The covered layer is not drawn — a second full-canvas blit every frame is pure cost —
        // but ONLY while the layer above is exactly opaque, and that is decided from THIS frame's
        // alpha, updated just above and read before anything is drawn (R-VIEW-1e). Reading the
        // previous frame's value hid the base for the first frame of every 1->0 dissolve and let
        // the canvas show through the partly-transparent top: one dark frame per render, at about
        // 8 Hz through a drag, which is what "the photo blinks" was.
        mPhotoBase->visible = mPhotoTop->opacity.value() < 0.999;
    }

    ImageView *PhotoCanvas::visibleView() const
    {
        return (mPhotoTop->opacity.value() > 0.5 ? mPhotoTop : mPhotoBase).get();
    }

    bool PhotoCanvas::sameShape(int w1, int h1, int w2, int h2)
    {
        if (w1 <= 0 || h1 <= 0 || w2 <= 0 || h2 <= 0) return false;
        // Cross-multiplied so no division is needed, with half a percent of slack: a preview at a
        // different resolution rounds its dimensions, and two frames of the same photo must still
        // count as the same shape (R-VIEW-1c).
        const double a = (double)w1 * h2, b = (double)w2 * h1;
        return std::fabs(a - b) <= 0.005 * a;
    }

    void PhotoCanvas::setPhoto(const uint8_t *rgba, int w, int h, double nowMs)
    {
        if (!rgba || w <= 0 || h <= 0) return;

        // A dissolve in flight means BOTH views are contributing to the composite, so writing
        // pixels into either of them is a step the eye reads as a blink — a step of exactly the
        // written layer's weight times the difference between two renders, which mid-drag is
        // nearly every render. So the frame WAITS (R-VIEW-1a): held here, applied by advance()
        // the moment the dissolve settles. Newest wins; an older held frame is simply overwritten,
        // the same coalescing RenderService does upstream.
        if (mPhotoTop->opacity.isAnimating())
        {
            mHeld.assign(rgba, rgba + (size_t)w * h * 4);
            mHeldW = w; mHeldH = h;
            mHeldPending = true;
            WLOG("photo: HOLD %dx%d (a=%.2f, dissolve in flight)", w, h, mPhotoTop->opacity.value());
            return;
        }
        showPhoto(rgba, w, h, nowMs);
    }

    void PhotoCanvas::showPhoto(const uint8_t *rgba, int w, int h, double nowMs)
    {
        auto &cur = mTopIsCurrent ? mPhotoTop : mPhotoBase;
        auto &other = mTopIsCurrent ? mPhotoBase : mPhotoTop;

        // Nothing to dissolve FROM (an empty stage, R-VIEW-1b), or a frame of a different SHAPE,
        // which cannot cover what is on screen (a different photo, R-VIEW-1c): both views take it,
        // so no stale pixels can peek around it, and the dissolve rests where it already was.
        if (!cur->hasImage() || !sameShape(cur->imageWidth(), cur->imageHeight(), w, h))
        {
            WLOG("photo: SET %dx%d (%s) -- no dissolve", w, h,
                 cur->hasImage() ? "different shape" : "empty stage");
            mPhotoBase->setImage(rgba, w, h);
            mPhotoTop->setImage(rgba, w, h);
            return;
        }

        // The newest render goes into the view that is currently at weight zero, and the top's
        // opacity eases toward it — so successive renders dissolve in alternating directions and
        // the pixels are only ever written where they cannot be seen.
        other->setImage(rgba, w, h);
        mTopIsCurrent = !mTopIsCurrent;
        mPhotoTop->opacity.animateTo(mTopIsCurrent ? 1.0 : 0.0, kPhotoFadeMs, kPhotoFadeEase, nowMs);
        WLOG("photo: DISSOLVE %dx%d -> %s from a=%.2f over %.0fms", w, h,
             mTopIsCurrent ? "top" : "base", mPhotoTop->opacity.value(), kPhotoFadeMs);
    }

    void PhotoCanvas::clearPhoto()
    {
        mPhotoBase->clearImage();
        mPhotoTop->clearImage();
        mPhotoTop->opacity.set(0.0);
        mTopIsCurrent = false;
        mHeldPending = false;
        mHeld.clear();
    }

    void PhotoCanvas::layout()
    {
        const double w = width.value(), h = height.value();
        for (auto &iv : {mPhotoBase, mPhotoTop})
        {
            iv->x.set(0.0); iv->y.set(0.0);
            iv->width.set(w); iv->height.set(h);
        }

        // Clip covers everything LEFT OF THE SEAM; the before-image inside spans the FULL
        // canvas (same fit as the edited image) so the two stay pixel-aligned whatever the
        // seam is doing. R-VIEW-3 made the seam movable, so this reads the position rather
        // than the 0.5 it used to hardcode — and `layout()` runs every frame, which is what
        // makes both a drag and the eased return to centre land here with no extra plumbing.
        const double seam = mSplitPos.value() * w;
        mSplitClip->x.set(0.0); mSplitClip->y.set(0.0);
        mSplitClip->width.set(seam); mSplitClip->height.set(h);
        mBeforeView->x.set(0.0); mBeforeView->y.set(0.0);
        mBeforeView->width.set(w); mBeforeView->height.set(h);

        // Derived from the EASED hover amount every frame, not set once when the pointer
        // arrived — R-G-1's clause (d): a value computed from an animated one is recomputed
        // from the animated one, or it snaps.
        const double hv = mSeamHover.amount(0);
        const double sw = kSeamW + (kSeamWHover - kSeamW) * hv;
        mDivider->x.set(seam - sw * 0.5); mDivider->y.set(0.0);
        mDivider->width.set(sw); mDivider->height.set(h);
        mDivider->style.paint = Paint::filled(
            palette::whiteAlpha(kSeamAlpha + (kSeamAlphaHover - kSeamAlpha) * hv));

        // The mask overlay fills the canvas; its fitted rect tracks the photo's
        // display area INCLUDING the current zoom/pan (R-MASK-3, R-ZOOM).
        mMaskOverlay->x.set(0.0); mMaskOverlay->y.set(0.0);
        mMaskOverlay->width.set(w); mMaskOverlay->height.set(h);
        mMaskOverlay->setFittedRect(visibleView()->fittedRect());

        // R-CROP-5: the same fitted rect, so the crop box tracks the photo's display area
        // including zoom and pan. A crop is a region of the PHOTO — using the canvas would put
        // the box out in the letterbox.
        mCropOverlay->x.set(0.0); mCropOverlay->y.set(0.0);
        mCropOverlay->width.set(w); mCropOverlay->height.set(h);
        mCropOverlay->setFittedRect(visibleView()->fittedRect());

        double pillW = 0.0;
        for (const char *s : {"Before", "Split", "After"})
            pillW += estimateTextWidth(s, kFontPx) + 2 * kSegPadX;
        mPill->width.set(pillW);
        mPill->height.set(kPillH);
        mPill->x.set((w - pillW) * 0.5);
        mPill->y.set(h - 13.0 /*bottom-4*/ - kPillH);
        mPill->layout();
    }

    void PhotoCanvas::zoomAbout(double factor, const Point &localInCanvas)
    {
        // ImageView fills PhotoCanvas at (0,0), so PhotoCanvas-local == ImageView-local.
        // EVERY view on the stage, not just the pair that is visible now: a view left behind
        // would slide into place under the next dissolve (R-ZOOM-3 as amended).
        for (auto &iv : {mPhotoBase, mPhotoTop, mBeforeView})
            iv->zoomAbout(factor, localInCanvas);
    }

    void PhotoCanvas::resetZoom()
    {
        for (auto &iv : {mPhotoBase, mPhotoTop, mBeforeView})
            iv->resetView();
    }

    Rect PhotoCanvas::photoFittedRect() const { return visibleView()->fittedRect(); }

    bool PhotoCanvas::onSeam(const Point &local) const
    {
        // Only while the seam is actually on screen — grabbing an invisible seam would be a
        // dead press in Before/After mode, and worse, one that swallowed the pan.
        if (!mSplitWanted) return false;
        // The line is 1.5 px; nobody can hit 1.5 px. The pick radius is the SAME number the
        // curve and mask editors use, so "how close counts as on it" is one value across the
        // app rather than three (R-VIEW-3).
        return std::fabs(local.x - seamX()) <= metrics::anchorHitRadius();
    }

    bool PhotoCanvas::handleGesture(const Gesture &g, const Point &local)
    {
        if (g.type == Gesture::Type::RightClick && onContext) { onContext(g.pos.x, g.pos.y); return true; }

        // ── R-VIEW-3: slide the seam ────────────────────────────────────────────────────
        // Checked BEFORE the pan, so a press on the seam moves the seam even while zoomed;
        // a press anywhere else still pans, which is the whole reason this is a hit test and
        // not a mode.
        if (g.type == Gesture::Type::Move)
        {
            // Hover feedback, so the seam looks grabbable without a cursor change (the HAL
            // has none) and without a tooltip.
            if (onSeam(local)) mSeamHover.setHovered(0); else mSeamHover.clear();
        }
        if (g.type == Gesture::Type::DoubleClick && onSeam(local))
        {
            // Back to the middle — and this the app does on the user's behalf, so it EASES
            // (R-G-1). The drag below does not, because there the pointer is the animation.
            mSplitPos.animateTo(0.5, kModeFadeMs, Easing::EaseOutCubic, mNowMs);
            WLOG("photo: seam re-centred over %.0fms", kModeFadeMs);
            return true;
        }
        if ((g.type == Gesture::Type::Down || g.type == Gesture::Type::DragStart) && onSeam(local))
        {
            mSeamDrag = true;
            // D-32's lesson, applied here: keep the pointer-to-seam offset and add it back on
            // every move, so grabbing the seam 11 px off-centre slides it BY the drag instead
            // of teleporting it under the pointer.
            mSeamGrabDX = local.x - seamX();
            mSeamHover.setHovered(0);
            return true;
        }
        if (mSeamDrag)
        {
            switch (g.type)
            {
            case Gesture::Type::Drag:
            case Gesture::Type::DragStart:
            {
                const double w = width.value();
                if (w > 0.0)
                {
                    // Clamped so the seam never leaves the canvas: at 0 or 1 there is nothing
                    // left to compare and, worse, nothing left to grab it by.
                    const double margin = metrics::anchorHitRadius();
                    const double x = std::clamp(local.x - mSeamGrabDX, margin, w - margin);
                    mSplitPos.set(x / w);   // direct: the pointer IS the animation
                    layout();
                }
                return true;
            }
            case Gesture::Type::Up:
            case Gesture::Type::Drop:
                mSeamDrag = false;
                return true;
            default:
                break;
            }
        }

        // Drag-to-pan while zoomed (>1x) — pan every view so the split stays aligned.
        if (mPhotoBase->zoom() > 1.0)
        {
            switch (g.type)
            {
            case Gesture::Type::Down:
            case Gesture::Type::DragStart:
                mPanLast = local;
                return true;
            case Gesture::Type::Drag:
                for (auto &iv : {mPhotoBase, mPhotoTop, mBeforeView})
                    iv->panBy(local.x - mPanLast.x, local.y - mPanLast.y);
                mPanLast = local;
                return true;
            case Gesture::Type::Up:
            case Gesture::Type::Drop:
                return true;
            default:
                break;
            }
        }
        return Segment::handleGesture(g, local);
    }

    void PhotoCanvas::onPaint(IRenderTarget &t) const
    {
        drawRoundedRect(t, Rect{0, 0, width.value(), height.value()}, 0.0, Paint::filled(palette::canvasBg()));
    }
}
}
