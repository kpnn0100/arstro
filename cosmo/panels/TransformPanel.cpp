#include "TransformPanel.h"
#include "../Chrome.h"

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    TransformPanel::TransformPanel(const Theme &theme, const Color &accent) : mAccent(accent)
    {
        width.set(300.0);
        height.set(200.0);

        mRotate = std::make_shared<Knob>(theme.knob);
        mRotate->label = "straighten"; mRotate->setRange(-45.0, 45.0); mRotate->setValue(0.0); mRotate->setDefault(0.0);
        mRotate->width.set(44.0); mRotate->height.set(44.0); mRotate->x.set(22.0); mRotate->y.set(46.0);
        mRotate->onChange = [this](double v) { if (onRotate) onRotate(v); };
        addChild(mRotate);

        mQuarter = std::make_shared<ComboBox>(theme.combo);
        mQuarter->setOptions({"0°", "90°", "180°", "270°"});
        mQuarter->setSelectedIndex(0);
        mQuarter->x.set(110.0); mQuarter->y.set(52.0);
        mQuarter->width.set(120.0); mQuarter->height.set(24.0);
        mQuarter->onChange = [this](int i) { if (onQuarterTurns) onQuarterTurns(i); };
        addChild(mQuarter);

        auto crop = [&](double def, const char *label, double x) {
            auto k = std::make_shared<Knob>(theme.knob);
            k->label = label; k->setRange(0.0, 1.0); k->setValue(def); k->setDefault(def);
            k->width.set(40.0); k->height.set(40.0); k->x.set(x); k->y.set(130.0);
            k->onChange = [this](double) { emitCrop(); };
            addChild(k);
            return k;
        };
        mX = crop(0.0, "crop x", 20);
        mY = crop(0.0, "crop y", 90);
        mW = crop(1.0, "crop w", 160);
        mH = crop(1.0, "crop h", 230);
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
        t.setFill(Color{1, 1, 1, 0.45});
        t.drawText("crop", 14.0, 118.0, 10.0);
    }
}
}
