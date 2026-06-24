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
        const char *kLabels[6] = {"straighten", "rotate", "crop x", "crop y", "crop w", "crop h"};
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
        const double rowH = usable / 6.0;
        auto rowTop = [&](int r) { return kHeaderH + r * rowH; };
        for (int r = 0; r < 6; ++r) mRowY[r] = rowTop(r) + rowH * 0.5 + 3.0;

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
        placeSlider(mX, 2); placeSlider(mY, 3); placeSlider(mW, 4); placeSlider(mH, 5);
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
        drawPanelChrome(t, width.value(), height.value(), mAccent, "TRANSFORM");
        t.setFill(palette::muted());
        for (int r = 0; r < 6; ++r)
            t.drawText(kLabels[r], 10.0, mRowY[r], 10.0);
    }
}
}
