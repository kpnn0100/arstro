#include "PhotoCanvas.h"
#include "TextMetrics.h"
#include "WidgetLog.h"
#include "../Theme.h"

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
        // The photo dissolve (R-VIEW-1). 120 ms is cosmo's shortest motion step, and it has to be
        // short here for a reason beyond feel: preview renders land faster than that while a slider
        // is being dragged, so a longer dissolve would spend the whole drag interrupted and never
        // arrive. Short enough to converge between renders, long enough to read as one movement.
        constexpr double kPhotoFadeMs = 120.0;
        constexpr double kModeFadeMs = 180.0;   // Split's clip + seam: cosmo's cross-fade step
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
        mDivider->style = {Paint::filled(palette::whiteAlpha(0.55)), 0.0};
        mDivider->inputTransparent = true;
        addChild(mDivider);

        // On-photo mask editor. Added BEFORE the pill so the pill stays clickable
        // (topmost child wins on overlap); it is click-through until a mask is active.
        mMaskOverlay = std::make_shared<MaskOverlay>(palette::primary());
        addChild(mMaskOverlay);

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
        // Once the dissolve has fully arrived on the top view, the base underneath is covered by
        // an identical rect — same image size, same fit, same zoom — so drawing it is pure cost
        // (a second full-canvas image blit every frame). Skipping it there is the one `visible`
        // flip in this file that is provably invisible, and it is undone on the very next frame
        // the top is not opaque, before anything is drawn.
        mPhotoBase->visible = mPhotoTop->opacity.value() < 0.999;

        if (mSplitApplied != mSplitWanted)
        {
            const double a = mSplitWanted ? 1.0 : 0.0;
            mSplitClip->opacity.animateTo(a, kModeFadeMs, Easing::EaseOutCubic, nowMs);
            mDivider->opacity.animateTo(a, kModeFadeMs, Easing::EaseOutCubic, nowMs);
            mSplitApplied = mSplitWanted;
            WLOG("photo: split %s over %.0fms", mSplitWanted ? "IN" : "OUT", kModeFadeMs);
        }
        Segment::advance(nowMs);   // updates every opacity, including the dissolve's
    }

    ImageView *PhotoCanvas::visibleView() const
    {
        return (mPhotoTop->opacity.value() > 0.5 ? mPhotoTop : mPhotoBase).get();
    }

    void PhotoCanvas::setPhoto(const uint8_t *rgba, int w, int h, double nowMs)
    {
        if (!rgba || w <= 0 || h <= 0) return;
        auto &cur = mTopIsCurrent ? mPhotoTop : mPhotoBase;
        auto &other = mTopIsCurrent ? mPhotoBase : mPhotoTop;

        // Nothing to dissolve FROM (an empty stage, R-VIEW-1b), or a frame of a different pixel
        // size that cannot cover what is on screen (a different photo, R-VIEW-1c): both views take
        // it, so no stale pixels can peek around a differently-shaped frame, and the dissolve rests
        // wherever it already was.
        if (!cur->hasImage() || cur->imageWidth() != w || cur->imageHeight() != h)
        {
            WLOG("photo: SET %dx%d (%s) -- no dissolve", w, h,
                 cur->hasImage() ? "different frame size" : "empty stage");
            mPhotoBase->setImage(rgba, w, h);
            mPhotoTop->setImage(rgba, w, h);
            return;
        }

        // The newest render goes into the hidden view and the top's opacity eases toward it. When a
        // render lands mid-dissolve this REVERSES it rather than restarting: `animateTo` starts
        // from the current eased value, so opacity stays continuous and always converges on the
        // newest frame (R-VIEW-1a).
        other->setImage(rgba, w, h);
        const bool interrupted = mPhotoTop->opacity.isAnimating();
        mTopIsCurrent = !mTopIsCurrent;
        mPhotoTop->opacity.animateTo(mTopIsCurrent ? 1.0 : 0.0, kPhotoFadeMs, Easing::EaseOutCubic, nowMs);
        WLOG("photo: DISSOLVE %dx%d -> %s from a=%.2f over %.0fms%s", w, h,
             mTopIsCurrent ? "top" : "base", mPhotoTop->opacity.value(), kPhotoFadeMs,
             interrupted ? " (reversing a dissolve already in flight)" : "");
    }

    void PhotoCanvas::clearPhoto()
    {
        mPhotoBase->clearImage();
        mPhotoTop->clearImage();
        mPhotoTop->opacity.set(0.0);
        mTopIsCurrent = false;
    }

    void PhotoCanvas::layout()
    {
        const double w = width.value(), h = height.value();
        for (auto &iv : {mPhotoBase, mPhotoTop})
        {
            iv->x.set(0.0); iv->y.set(0.0);
            iv->width.set(w); iv->height.set(h);
        }

        // Clip covers the left half; the before-image inside spans the FULL canvas
        // (same fit as the edited image) so the two stay pixel-aligned.
        mSplitClip->x.set(0.0); mSplitClip->y.set(0.0);
        mSplitClip->width.set(w * 0.5); mSplitClip->height.set(h);
        mBeforeView->x.set(0.0); mBeforeView->y.set(0.0);
        mBeforeView->width.set(w); mBeforeView->height.set(h);

        mDivider->x.set(w * 0.5 - 0.75); mDivider->y.set(0.0);
        mDivider->width.set(1.5); mDivider->height.set(h);

        // The mask overlay fills the canvas; its fitted rect tracks the photo's
        // display area INCLUDING the current zoom/pan (R-MASK-3, R-ZOOM).
        mMaskOverlay->x.set(0.0); mMaskOverlay->y.set(0.0);
        mMaskOverlay->width.set(w); mMaskOverlay->height.set(h);
        mMaskOverlay->setFittedRect(visibleView()->fittedRect());

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

    bool PhotoCanvas::handleGesture(const Gesture &g, const Point &local)
    {
        if (g.type == Gesture::Type::RightClick && onContext) { onContext(g.pos.x, g.pos.y); return true; }

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
