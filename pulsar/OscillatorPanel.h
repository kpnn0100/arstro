/*
 *  Pulsar by arstro — OscillatorPanel: one oscillator's UI (shared by OSC1/2/3,
 *  only the name + accent differ). Owns the parameter state and lays out:
 *    - mute square (left of the name)
 *    - waveform selector (ComboBox, above the visualiser)
 *    - WaveDisplay (Serum-style 3D wavetable scape) with a POSITION slider below it
 *    - WARP-mode selector (ComboBox) beneath the slider
 *    - grouped knobs: UNISON {voice (Stepper), detune, blend, stereolize} | OCTAVE,
 *      WARP {warp amount} | PHASE {phase, random} | OUTPUT {pan, level}
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
        static constexpr double kSectionGap = 9.0; // gap between sections inside a panel

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
        double mPhase = 0.0, mRandom = 0.0, mBlend = 0.5, mWarpAmt = 0.0;
        int mVoice = 1, mOctave = 0;

        void syncUnison(); // push voice/detune/blend to the display fan

        // controls
        std::shared_ptr<MuteButton> mMute;
        std::shared_ptr<artboard::ComboBox> mCombo;     // wavetable shape
        std::shared_ptr<artboard::ComboBox> mWarpCombo; // warp mode
        std::shared_ptr<WaveDisplay> mDisplay;
        std::shared_ptr<artboard::Slider> mPosSlider;
        std::shared_ptr<Stepper> mVoiceStepper;
        std::shared_ptr<Stepper> mOctaveStepper;
        std::vector<std::shared_ptr<artboard::Knob>> mKnobs; // detune, blend, stereo, warp, phase, rnd, pan, level

        artboard::KnobStyle mBaseKnob;
        artboard::SliderStyle mBaseSlider;
    };
}
}
