/*
 *  Pulsar by arstro — SubOscPanel: a compact sub-oscillator that sits in the
 *  oscillator row to the right of OSC3. One basic shape (sine/tri/saw/square), an
 *  octave offset (typically −1) and a level. No visualiser — it is a simple
 *  reinforcing voice, so the panel stays small.
 *
 *  Reserved for later: routing the sub into the audio path.
 */
#pragma once
#include "../Artboard/include/artboard/artboard.h"
#include "Stepper.h"
#include <memory>

namespace arstro
{
namespace pulsar
{
    class SubOscPanel : public artboard::Segment
    {
    public:
        SubOscPanel(const artboard::Theme &theme, const artboard::Color &accent);

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        artboard::Color mAccent;
        int mShape = 0;
        double mLevel = 0.5;
        int mOctave = -1;
        bool mOn = true; // power toggle; off dims the panel + disables controls

        std::shared_ptr<artboard::ComboBox> mWave;
        std::shared_ptr<Stepper> mOctaveStepper;
        std::shared_ptr<artboard::Knob> mLevelKnob;
        std::shared_ptr<artboard::ToggleSwitch> mPower;
    };
}
}
