#include "PhotoCanvas.h"
#include "TextMetrics.h"
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
    }

    PhotoCanvas::PhotoCanvas()
    {
        clipToBounds = true;
        mImageView = std::make_shared<ImageView>();
        mImageView->setFit(ImageView::Fit::Contain);
        // PhotoCanvas owns pan/zoom so the after + before(split) views move together;
        // make the after view click-through so its drag falls through to us (R-ZOOM-3).
        mImageView->inputTransparent = true;
        addChild(mImageView);

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

        applyMode();
        layout();
    }

    void PhotoCanvas::applyMode()
    {
        const bool split = (mPill->selected() == Split);
        mSplitClip->visible = split;
        mDivider->visible = split;
    }

    void PhotoCanvas::layout()
    {
        const double w = width.value(), h = height.value();
        mImageView->x.set(0.0); mImageView->y.set(0.0);
        mImageView->width.set(w); mImageView->height.set(h);

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
        mMaskOverlay->setFittedRect(mImageView->fittedRect());

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
        mImageView->zoomAbout(factor, localInCanvas);
        mBeforeView->zoomAbout(factor, localInCanvas);
    }

    void PhotoCanvas::resetZoom()
    {
        mImageView->resetView();
        mBeforeView->resetView();
    }

    bool PhotoCanvas::handleGesture(const Gesture &g, const Point &local)
    {
        if (g.type == Gesture::Type::RightClick && onContext) { onContext(g.pos.x, g.pos.y); return true; }

        // Drag-to-pan while zoomed (>1x) — pan both views so the split stays aligned.
        if (mImageView->zoom() > 1.0)
        {
            switch (g.type)
            {
            case Gesture::Type::Down:
            case Gesture::Type::DragStart:
                mPanLast = local;
                return true;
            case Gesture::Type::Drag:
                mImageView->panBy(local.x - mPanLast.x, local.y - mPanLast.y);
                mBeforeView->panBy(local.x - mPanLast.x, local.y - mPanLast.y);
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
