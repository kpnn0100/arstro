/*
 *  Arstro example — Scope: a platform-free app linking Arstro DSP + Artboard.
 *
 *  It owns a SynthEngine (audio) and an Artboard scene (visuals). The synth's
 *  output is drawn as a live waveform via Artboard primitives; an animated
 *  playhead sweeps across. The app touches ONLY Artboard's IRenderTarget and the
 *  DSP engine — never a platform API — so any adapter (web today) can render it.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "../../DigitalSignalProcessing/src/synth_dsp.h"
#include <memory>
#include <vector>

namespace arstro
{
namespace examples
{
    class ScopeApp
    {
    public:
        ScopeApp(double width, double height);

        // ---- audio (Web Audio / native both call this) ----
        // Renders `frames` interleaved stereo float frames AND captures channel 0
        // into the scope ring for drawing.
        void renderAudio(float *interleaved, int frames);
        void setSampleRate(double sr) { AudioConfig::instance().setSampleRate(sr); }
        void noteOn(int midi, double vel) { mSynth.noteOn(midi, vel); }
        void noteOff(int midi) { mSynth.noteOff(midi); }

        // ---- visuals ----
        void render(artboard::IRenderTarget &target, double nowMs);

        int waveformPoints() const { return (int)mRing.size(); }

    private:
        double mW, mH;
        arstro::SynthEngine mSynth;
        std::vector<float> mRing; // recent channel-0 samples, length = sample columns
        artboard::Artboard mBoard;
        std::shared_ptr<artboard::Polyline> mWave;
        std::shared_ptr<artboard::Text> mTitle;
        std::shared_ptr<artboard::Line> mPlayhead;
        artboard::AnimatedProperty mSweep; // 0..1 playhead position, looping
    };
}
}
