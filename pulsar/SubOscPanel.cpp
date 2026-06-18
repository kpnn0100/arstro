#include "SubOscPanel.h"
#include "Chrome.h"

namespace arstro
{
namespace pulsar
{
    using namespace artboard;

    SubOscPanel::SubOscPanel(const Theme &theme, const Color &accent) : mAccent(accent)
    {
        width.set(261.0);
        height.set(150.0);

        mWave = std::make_shared<ComboBox>(theme.combo);
        mWave->setOptions({"SINE", "TRI", "SAW", "SQUARE"});
        mWave->setSelectedIndex(0);
        mWave->x.set(14.0); mWave->y.set(34.0);
        mWave->width.set(233.0); mWave->height.set(26.0);
        mWave->onChange = [this](int idx) { mShape = idx; };
        addChild(mWave);

        mOctaveStepper = std::make_shared<Stepper>();
        mOctaveStepper->setColor(accent);
        mOctaveStepper->setRange(-2, 1);
        mOctaveStepper->setValue(mOctave);
        mOctaveStepper->setLabel("oct");
        mOctaveStepper->x.set(20.0); mOctaveStepper->y.set(72.0);
        mOctaveStepper->width.set(96.0); mOctaveStepper->height.set(54.0);
        mOctaveStepper->onChange = [this](int v) { mOctave = v; };
        addChild(mOctaveStepper);

        mLevelKnob = std::make_shared<Knob>(theme.knob);
        mLevelKnob->label = "level";
        mLevelKnob->setRange(0.0, 1.0);
        mLevelKnob->setValue(mLevel);
        mLevelKnob->setDefault(mLevel);
        mLevelKnob->x.set(160.0); mLevelKnob->y.set(74.0);
        mLevelKnob->width.set(58.0); mLevelKnob->height.set(48.0);
        mLevelKnob->onChange = [this](double v) { mLevel = v; };
        addChild(mLevelKnob);
    }

    void SubOscPanel::onPaint(IRenderTarget &t) const
    {
        drawPanelChrome(t, width.value(), height.value(), mAccent, "SUB");
    }
}
}
