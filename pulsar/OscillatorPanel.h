/*
 *  Pulsar by arstro — OscillatorPanel: one oscillator's UI (shared by OSC1/2/3,
 *  only the name + accent differ). Owns the parameter state and lays out:
 *    - mute square (left of the name)
 *    - waveform selector (ComboBox, above the visualiser)
 *    - WaveDisplay (2D shapes + 3D morph) with a POSITION slider beneath it (timeline)
 *    - grouped knobs: UNISON {voice (Stepper), detune, stereolize},
 *      OUTPUT {pan, level}, PHASE {phase, random}
 *
 *  Muting eases the whole panel accent (border, name, knobs, wave) to a dim colour.
 *  Reserved for later: per-parameter macro/LFO, and audio output.
 */
#pragma once
#include "../Artboard/include/artboard/artboard.h"
#include "WaveDisplay.h"
#include "MuteButton.h"
#include "Stepper.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace pulsar
{
    class OscillatorPanel : public artboard::Segment
    {
    public:
        OscillatorPanel(std::string name, const artboard::Theme &theme, const artboard::Color &accent);

        const std::string &name() const { return mName; }
        bool muted() const { return mMuted; }

        void advance(double nowMs) override; // eases the mute fade + restyles children

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        std::string mName;
        artboard::Color mAccent;
        artboard::Color mDimAccent;       // accent eased toward dim when muted
        bool mMuted = false;
        double mMuteFade = 1.0;            // 1 = audio on, 0 = muted
        double mLastMs = -1.0;

        // params
        double mPosition = 0.5, mDetune = 0.2, mStereo = 0.0, mPan = 0.5, mLevel = 0.8;
        double mPhase = 0.0, mRandom = 0.0;
        int mVoice = 1;

        // controls
        std::shared_ptr<MuteButton> mMute;
        std::shared_ptr<artboard::ComboBox> mCombo;
        std::shared_ptr<WaveDisplay> mDisplay;
        std::shared_ptr<artboard::Slider> mPosSlider;
        std::shared_ptr<Stepper> mVoiceStepper;
        std::vector<std::shared_ptr<artboard::Knob>> mKnobs; // detune, stereo, pan, level, phase, random

        artboard::KnobStyle mBaseKnob;
        artboard::SliderStyle mBaseSlider;
    };
}
}
