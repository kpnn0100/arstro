/*
 *  Pulsar by arstro — SubOscPanel: a simple sub-oscillator. One basic shape
 *  (no wavetable morph), an octave offset (typically −1), and a level. The chosen
 *  shape springs into the next so switching waves animates smoothly. A wide wave
 *  preview shows two cycles of the current shape with a soft glow.
 *
 *  Reserved for later: routing the sub into the audio path.
 */
#pragma once
#include "../Artboard/include/artboard/artboard.h"
#include "Stepper.h"
#include <memory>
#include <vector>

namespace arstro
{
namespace pulsar
{
    class SubOscPanel : public artboard::Segment
    {
    public:
        SubOscPanel(const artboard::Theme &theme, const artboard::Color &accent);
        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        double subSample(double p01) const; // morphed basic shape at normalized phase

        artboard::Color mAccent;
        double mShapeTarget = 0.0; // 0..3 selected shape index
        double mShapeDisplay = 0.0;
        double mShapeVel = 0.0;
        double mLevel = 0.5;
        int mOctave = -1;
        double mLastMs = -1.0;

        std::shared_ptr<artboard::ComboBox> mWave;
        std::shared_ptr<Stepper> mOctaveStepper;
        std::shared_ptr<artboard::Knob> mLevelKnob;
    };
}
}
