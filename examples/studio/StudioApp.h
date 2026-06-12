/*
 *  Arstro Studio — an advanced example linking Arstro DSP + Artboard.
 *
 *  A full synth dashboard drawn entirely with Artboard primitives (rounded
 *  panels with borders, an oscilloscope polyline, an animated spectrum, an ADSR
 *  envelope spline, rotary knobs with arc tracks, and a highlightable keyboard),
 *  driven by the DSP SynthEngine. Platform-free: it touches only Artboard's
 *  IRenderTarget and the DSP engine.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "../../DigitalSignalProcessing/src/synth_dsp.h"
#include <array>
#include <memory>
#include <vector>

namespace arstro
{
namespace examples
{
    class StudioApp
    {
    public:
        StudioApp(double width, double height);

        void setSampleRate(double sr) { AudioConfig::instance().setSampleRate(sr); }
        void renderAudio(float *interleaved, int frames); // stereo float + capture for scope/spectrum
        void noteOn(int midi, double vel);
        void noteOff(int midi);

        void render(artboard::IRenderTarget &target, double nowMs);

        // exposed for the native render/verify test
        int spectrumBands() const { return kBands; }

    private:
        static constexpr int kBands = 28;
        static constexpr int kKeys = 12;

        void buildStaticScene();
        void updateAudioDriven();
        double knobAngle(double value01) const;

        double mW, mH;
        arstro::SynthEngine mSynth;
        std::vector<float> mRing;           // recent ch0 samples (scope)
        std::array<double, kBands> mSpec{}; // smoothed band magnitudes (peak-hold decay)
        double mNowMs = 0;

        // persistent drawables (mutated per frame)
        artboard::Artboard mBoard;
        std::shared_ptr<artboard::Polyline> mWave;
        std::vector<std::shared_ptr<artboard::Rectangle>> mBars;
        std::array<std::shared_ptr<artboard::Rectangle>, kKeys> mKeyRects;
        std::array<artboard::AnimatedProperty, kKeys> mKeyGlow; // 0..1 press glow
        std::array<artboard::AnimatedProperty, 4> mKnob;        // intro-animated indicator value
        std::array<std::shared_ptr<artboard::Line>, 4> mKnobInd;
        std::array<double, 4> mKnobTarget{{0.3, 0.6, 0.25, 0.5}};
        bool mIntroStarted = false;
    };
}
}
