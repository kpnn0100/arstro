#include "TransformPanel.h"
#include "../Chrome.h"
#include "../CosmoTheme.h"

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    namespace
    {
        constexpr double kHeaderH = 34.0;
        constexpr double kPad = 12.0;
        constexpr double kLabelW = 64.0;
        constexpr double kSliderH = 14.0;
        constexpr int kRows = 7;
        const char *kLabels[kRows] = {"straighten", "rotate", "aspect", "crop x", "crop y", "crop w", "crop h"};
        const double kAspectRatios[5] = {0.0, 1.0, 1.5, 4.0 / 3.0, 16.0 / 9.0};
    }

    TransformPanel::TransformPanel(const Theme &theme, const Color &accent) : mAccent(accent)
    {
        width.set(300.0);
        height.set(220.0);

        mRotate = std::make_shared<Slider>(theme.slider);
        mRotate->setRange(-45.0, 45.0); mRotate->setValue(0.0); mRotate->setDefault(0.0);
        mRotate->setClickJumps(false);
        mRotate->onChange = [this](double v) { if (onRotate) onRotate(v); };
        addChild(mRotate);

        mCCW = std::make_shared<Button>("CCW", theme.button);
        mCCW->onClick = [this] { mQuarter = (mQuarter + 3) & 3; if (onQuarterTurns) onQuarterTurns(mQuarter); };
        addChild(mCCW);
        mCW = std::make_shared<Button>("CW", theme.button);
        mCW->onClick = [this] { mQuarter = (mQuarter + 1) & 3; if (onQuarterTurns) onQuarterTurns(mQuarter); };
        addChild(mCW);

        mAspectSel = std::make_shared<ComboBox>(theme.combo);
        mAspectSel->setOptions({"Free", "1:1", "3:2", "4:3", "16:9"});
        mAspectSel->setSelectedIndex(0);
        mAspectSel->onChange = [this](int i) {
            if (onAspect) onAspect(kAspectRatios[i >= 0 && i < 5 ? i : 0]);
        };
        addChild(mAspectSel);

        auto crop = [&](double def) {
            auto s = std::make_shared<Slider>(theme.slider);
            s->setRange(0.0, 1.0); s->setValue(def); s->setDefault(def);
            s->setClickJumps(false);
            s->onChange = [this](double) { emitCrop(); };
            addChild(s);
            return s;
        };
        mX = crop(0.0); mY = crop(0.0); mW = crop(1.0); mH = crop(1.0);
    }

    void TransformPanel::layout(double w, double h)
    {
        width.set(w);
        height.set(h);
        const double usable = h - kHeaderH - kPad;
        const double rowH = usable / kRows;
        auto rowTop = [&](int r) { return kHeaderH + r * rowH; };
        for (int r = 0; r < kRows; ++r) mRowY[r] = rowTop(r) + rowH * 0.5 + 3.0;

        const double cx = kLabelW, cw = w - kLabelW - kPad;
        auto placeSlider = [&](std::shared_ptr<Slider> &s, int r) {
            s->x.set(cx); s->y.set(rowTop(r) + (rowH - kSliderH) * 0.5);
            s->width.set(cw > 20 ? cw : 20); s->height.set(kSliderH);
        };
        placeSlider(mRotate, 0);
        // rotate buttons share row 1
        const double bh = rowH > 28 ? 22.0 : rowH - 4.0;
        const double bw = (cw - 8.0) * 0.5;
        const double by = rowTop(1) + (rowH - bh) * 0.5;
        mCCW->x.set(cx); mCCW->y.set(by); mCCW->width.set(bw > 20 ? bw : 20); mCCW->height.set(bh);
        mCW->x.set(cx + bw + 8.0); mCW->y.set(by); mCW->width.set(bw > 20 ? bw : 20); mCW->height.set(bh);
        const double ch = rowH > 30 ? 24.0 : rowH - 4.0;
        mAspectSel->x.set(cx); mAspectSel->y.set(rowTop(2) + (rowH - ch) * 0.5);
        mAspectSel->width.set(cw > 20 ? cw : 20); mAspectSel->height.set(ch);
        placeSlider(mX, 3); placeSlider(mY, 4); placeSlider(mW, 5); placeSlider(mH, 6);
    }

    void TransformPanel::emitCrop()
    {
        if (onCrop) onCrop(mX->value(), mY->value(), mW->value(), mH->value());
    }

    void TransformPanel::setState(const State &s)
    {
        mRotate->setValue(s.rotation);
        mQuarter = s.quarter;
        mX->setValue(s.cropX); mY->setValue(s.cropY);
        mW->setValue(s.cropW); mH->setValue(s.cropH);
    }

    void TransformPanel::onPaint(IRenderTarget &t) const
    {
        drawPanelChrome(t, width.value(), height.value(), "TRANSFORM");
        t.setFill(palette::muted());
        for (int r = 0; r < kRows; ++r)
            t.drawText(kLabels[r], 10.0, mRowY[r], 10.0);
    }
}
}
