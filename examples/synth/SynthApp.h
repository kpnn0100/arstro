/*
 *  Arstro Basic Synth — a playable synth built entirely from the Artboard widget
 *  set. A one-octave keyboard drives the DSP SynthEngine; the sound is visualised
 *  with LineGraphs (oscilloscope + spectrum) and a ProgressBar level meter; the
 *  controls (Knob, ComboBox, ToggleSwitch) live in a TabView, with a ScrollView of
 *  help text — a "flex" of the whole framework. Everything animates via the
 *  Artboard animation system.
 *
 *  Platform-free: it touches only Artboard's Segment/IRenderTarget and the DSP
 *  engine, so the same SynthApp runs under the web (Canvas2D) and native (Cairo)
 *  adapters. Note/param input goes through the engine's lock-free queue, so the UI
 *  thread may drive it while a separate audio thread calls renderAudio().
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "../../DigitalSignalProcessing/src/synth_dsp.h"
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace arstro
{
namespace examples
{
    // A one-octave keyboard widget: pointer down/up plays notes; keys glow when
    // played (from pointer OR from the host's QWERTY keys via glow()).
    class KeyboardSegment : public artboard::Segment
    {
    public:
        static constexpr int kKeys = 13; // C..C
        int baseMidi = 60;
        std::function<void(int)> onNoteOn;
        std::function<void(int)> onNoteOff;

        KeyboardSegment();
        void glow(int midi, bool on);          // drive the press highlight
        void advance(double nowMs) override;    // tick key-glow animations

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &localPoint) override;

    private:
        int keyIndexAt(const artboard::Point &local) const;
        std::array<artboard::AnimatedProperty, kKeys> mGlow;
        double mNowMs = 0.0;
        int mMouseNote = -1;
    };

    class SynthApp
    {
    public:
        SynthApp(double width, double height);

        void setSampleRate(double sr) { AudioConfig::instance().setSampleRate(sr); }
        void renderAudio(float *interleaved, int frames); // audio thread
        void noteOn(int midi, double vel);                // UI thread
        void noteOff(int midi);
        void pointer(int kind, double x, double y, int button, double timeMs);
        void render(artboard::IRenderTarget &target, double nowMs);

    private:
        static constexpr int kBands = 32;
        static constexpr int kScopePoints = 256;

        void buildUi();
        void applyPreset(int index);

        double mW, mH;
        arstro::SynthEngine mSynth;
        double mNowMs = 0.0;
        bool mIntroStarted = false;

        // oscilloscope ring (audio thread writes, UI thread snapshots)
        std::vector<float> mRing;
        size_t mRingHead = 0;
        std::mutex mScopeMutex;
        double mLevel = 0.0;
        std::array<double, kBands> mSpec{};

        artboard::Theme mTheme;
        std::shared_ptr<artboard::Segment> mRoot;
        std::shared_ptr<artboard::LineGraph> mScope;
        std::shared_ptr<artboard::LineGraph> mSpectrum;
        std::shared_ptr<artboard::ProgressBar> mMeter;
        std::shared_ptr<KeyboardSegment> mKeyboard;
        artboard::AnimatedProperty mReveal;
        artboard::Animator mAnimator;

        artboard::GestureRecognizer mRecognizer;
    };
}
}
