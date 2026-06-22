#include "ColorMixerPanel.h"
#include "../Chrome.h"

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    ColorMixerPanel::ColorMixerPanel(const Theme &theme, const Color &accent) : mAccent(accent)
    {
        width.set(238.0);
        height.set(150.0);

        mBandSel = std::make_shared<ComboBox>(theme.combo);
        mBandSel->setOptions({"Red", "Orange", "Yellow", "Green", "Aqua", "Blue", "Purple", "Magenta"});
        mBandSel->setSelectedIndex(0);
        mBandSel->x.set(14.0);
        mBandSel->y.set(40.0);
        mBandSel->width.set(210.0);
        mBandSel->height.set(24.0);
        mBandSel->onChange = [this](int b) { mBand = b; loadBand(); };
        addChild(mBandSel);

        auto makeKnob = [&](const char *label, double x) {
            auto k = std::make_shared<Knob>(theme.knob);
            k->label = label;
            k->setRange(-100.0, 100.0);
            k->setValue(0.0);
            k->setDefault(0.0);
            k->width.set(44.0);
            k->height.set(44.0);
            k->x.set(x);
            k->y.set(78.0);
            k->onChange = [this](double) { emitBand(); };
            addChild(k);
            return k;
        };
        mHue = makeKnob("hue", 22.0);
        mSat = makeKnob("sat", 96.0);
        mLum = makeKnob("lum", 170.0);
    }

    void ColorMixerPanel::loadBand()
    {
        mHue->setValue(mValues[mBand][0]);
        mSat->setValue(mValues[mBand][1]);
        mLum->setValue(mValues[mBand][2]);
    }

    void ColorMixerPanel::emitBand()
    {
        mValues[mBand][0] = mHue->value();
        mValues[mBand][1] = mSat->value();
        mValues[mBand][2] = mLum->value();
        if (onChange)
            onChange(mBand, mHue->value(), mSat->value(), mLum->value());
    }

    void ColorMixerPanel::setValues(const std::array<std::array<double, 3>, 8> &values)
    {
        mValues = values;
        loadBand();
    }

    void ColorMixerPanel::onPaint(IRenderTarget &t) const
    {
        drawPanelChrome(t, width.value(), height.value(), mAccent, "MIXER");
    }
}
}
