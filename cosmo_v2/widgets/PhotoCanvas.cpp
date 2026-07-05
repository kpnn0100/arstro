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
        constexpr double kPillH = 23.0;
        constexpr double kSegPadX = 11.375;  // px-3.5
        constexpr double kFontPx = 10.0;
    }

    PhotoCanvas::PhotoCanvas()
    {
        clipToBounds = true;
        mImageView = std::make_shared<ImageView>();
        mImageView->setFit(ImageView::Fit::Contain);
        addChild(mImageView);

        mPill = std::make_shared<SegmentedControl>(std::vector<std::string>{"Before", "After"});
        mPill->containerBox = {Paint::filledStroked(Color{0x11 / 255.0, 0x11 / 255.0, 0x11 / 255.0, 0.9},
                                                     palette::border(), 1.0),
                                radius::pill()};
        mPill->idleSegBox = {Paint{}, radius::pill()};
        mPill->activeSegBox = {Paint::filled(palette::primary()), radius::pill()};
        mPill->idleText = {palette::mutedForeground(), kFontPx, font::sans()};
        mPill->activeText = {palette::white(), kFontPx, font::sans()};
        mPill->padding = 0.0;
        mPill->gap = 0.0;
        mPill->setSelected(1);  // "after" by default, matching App.tsx's initial state
        mPill->onChange = [this](int idx) { if (onBeforeAfterChange) onBeforeAfterChange(idx == 1); };
        addChild(mPill);

        layout();
    }

    void PhotoCanvas::layout()
    {
        const double w = width.value(), h = height.value();
        mImageView->x.set(0.0); mImageView->y.set(0.0);
        mImageView->width.set(w); mImageView->height.set(h);

        const double beforeW = estimateTextWidth("Before", kFontPx) + 2 * kSegPadX;
        const double afterW = estimateTextWidth("After", kFontPx) + 2 * kSegPadX;
        const double pillW = beforeW + afterW;
        mPill->width.set(pillW);
        mPill->height.set(kPillH);
        mPill->x.set((w - pillW) * 0.5);
        mPill->y.set(h - 13.0 /*bottom-4*/ - kPillH);
        mPill->layout();
    }

    void PhotoCanvas::onPaint(IRenderTarget &t) const
    {
        drawRoundedRect(t, Rect{0, 0, width.value(), height.value()}, 0.0, Paint::filled(palette::canvasBg()));
    }
}
}
