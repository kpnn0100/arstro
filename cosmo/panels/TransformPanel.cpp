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
        constexpr double kCtrlH = 16.0;
    }

    TransformPanel::TransformPanel(const Theme &theme, const Color &accent) : mAccent(accent)
    {
        width.set(300.0);
        height.set(200.0);

        mRotate = std::make_shared<Slider>(theme.slider);
        mRotate->setRange(-45.0, 45.0); mRotate->setValue(0.0); mRotate->setDefault(0.0);
        mRotate->onChange = [this](double v) { if (onRotate) onRotate(v); };
        addChild(mRotate);

        mQuarter = std::make_shared<ComboBox>(theme.combo);
        mQuarter->setOptions({"0", "90", "180", "270"});
        mQuarter->setSelectedIndex(0);
        mQuarter->onChange = [this](int i) { if (onQuarterTurns) onQuarterTurns(i); };
        addChild(mQuarter);

        auto crop = [&](double def) {
            auto s = std::make_shared<Slider>(theme.slider);
            s->setRange(0.0, 1.0); s->setValue(def); s->setDefault(def);
            s->onChange = [this](double) { emitCrop(); };
            addChild(s);
            return s;
        };
        mX = crop(0.0); mY = crop(0.0); mW = crop(1.0); mH = crop(1.0);

        mRows = {
            {mRotate, "straighten", true},
            {mQuarter, "rotate", false},
            {mX, "crop x", true}, {mY, "crop y", true}, {mW, "crop w", true}, {mH, "crop h", true},
        };
    }

    void TransformPanel::layout(double w, double h)
    {
        width.set(w);
        height.set(h);
        const int rows = (int)mRows.size();
        const double usable = h - kHeaderH - kPad;
        const double rowH = rows > 0 ? usable / rows : usable;
        for (int i = 0; i < rows; ++i)
        {
            const double top = kHeaderH + i * rowH;
            Row &r = mRows[i];
            r.baseY = top + rowH * 0.5 + 3.0;
            const double cx = kLabelW;
            const double cw = w - cx - kPad;
            r.ctrl->x.set(cx);
            r.ctrl->width.set(cw > 20 ? cw : 20);
            const double ch = r.labeled ? kCtrlH : (rowH > 26 ? 22.0 : rowH - 2.0);
            r.ctrl->y.set(top + (rowH - ch) * 0.5);
            r.ctrl->height.set(ch);
        }
    }

    void TransformPanel::emitCrop()
    {
        if (onCrop) onCrop(mX->value(), mY->value(), mW->value(), mH->value());
    }

    void TransformPanel::setState(const State &s)
    {
        mRotate->setValue(s.rotation);
        mQuarter->setSelectedIndex(s.quarter);
        mX->setValue(s.cropX); mY->setValue(s.cropY);
        mW->setValue(s.cropW); mH->setValue(s.cropH);
    }

    void TransformPanel::onPaint(IRenderTarget &t) const
    {
        drawPanelChrome(t, width.value(), height.value(), mAccent, "TRANSFORM");
        t.setFill(palette::muted());
        for (const auto &r : mRows)
            if (r.labeled)
                t.drawText(r.label, 10.0, r.baseY, 10.0);
    }
}
}
