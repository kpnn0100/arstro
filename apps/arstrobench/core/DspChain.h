/*
 *  Arstrobench by arstro — the measured signal chain (R-DSP-2).
 *
 *  Not a token comp/EQ/reverb triple: this is the shape a real mix takes, at three
 *  levels — a per-voice channel strip on each of the nine voices, a master bus, and a
 *  reverb send. That is what "comp, EQ, reverb" means once it is done properly, and it
 *  is what makes the measurement representative of a live instrument rather than of one
 *  filter in a loop.
 *
 *      per voice (x9):  high-pass -> compressor -> 3-band EQ -> saturation
 *      sum -> bus:      4-band multiband compressor
 *                       6-section parametric EQ
 *                       reverb send: early-reflection reverb + tail reverb + damping + chorus
 *                       bus glue compressor
 *
 *  Everything here is COMPOSED from arstro_dsp's own primitives (Compressor, the one-pole
 *  LowPass/HighPass, Overdrive, Chorus, Reverb). The library is not bent into a
 *  benchmark's shape: shelves and peaking bands are built the way an EQ actually builds
 *  them (a filtered copy summed back at a gain), and crossovers are cascaded one-poles
 *  for a steeper slope.
 *
 *  Single-threaded by design — a real audio thread is one core, so this measures
 *  single-core throughput while the image workload measures the whole machine. Two
 *  different, deliberately complementary facts about the computer.
 *
 *  UI-free: depends only on arstro_dsp.
 */
#pragma once
#include "base/Sample.h"
#include "effects/Chorus.h"
#include "effects/Compressor.h"
#include "effects/Overdrive.h"
#include "equalizer/HighPassFilter.h"
#include "equalizer/LowPassFilter.h"
#include "reverb/Reverb.h"
#include <memory>
#include <vector>

namespace arstro
{
namespace arstrobench
{
    /** A cascade of N identical one-pole sections — the library ships one-pole LP/HP, and
     *  a crossover or a cut needs a steeper slope than 6 dB/octave to be worth the name. */
    template <typename Filter>
    class Cascade
    {
    public:
        explicit Cascade(int order = 2, Sample cutoffHz = 1000.0)
        {
            for (int i = 0; i < order; ++i)
            {
                mSections.push_back(std::make_unique<Filter>());
                mSections.back()->setCutoffFrequency(cutoffHz);
            }
        }
        void process(Sample *buf, int frames, int channel)
        {
            for (auto &s : mSections) s->processBlock(buf, frames, channel);
        }
        int order() const { return (int)mSections.size(); }

    private:
        std::vector<std::unique_ptr<Filter>> mSections;
    };

    using LowCascade = Cascade<LowPassFilter>;
    using HighCascade = Cascade<HighPassFilter>;

    /** One voice's channel strip: rumble cut, compression, a 3-band EQ (low shelf, mid
     *  peak, high shelf) and gentle saturation — what every voice in a mix goes through
     *  before it reaches the bus. */
    class VoiceStrip
    {
    public:
        VoiceStrip(Sample tiltHz, Sample driveAmount);
        /** In place, for one channel. `scratch` is caller-owned working space of at least
         *  `frames` samples, so the strip allocates nothing per call. */
        void process(Sample *buf, int frames, int channel, Sample *scratch);

    private:
        HighCascade mRumbleCut;
        Compressor mComp;
        LowPassFilter mLowShelf;    // low shelf = signal + gain * lowpass(signal)
        LowPassFilter mMidTop;      // mid peak  = signal + gain * bandpass(signal)
        HighPassFilter mMidBottom;
        HighPassFilter mHighShelf;  // high shelf = signal + gain * highpass(signal)
        Overdrive mSaturation;
        Sample mLowGain, mMidGain, mHighGain;
    };

    /** 4-band multiband compressor: split the bus into bands with 2nd-order crossovers,
     *  compress each band on its own detector, saturate it, and sum. The reason a mix bus
     *  uses one instead of a single wideband compressor is that a bass note should not
     *  duck the cymbals — and it is four times the detector work. */
    class MultibandCompressor
    {
    public:
        static constexpr int kBands = 4;
        MultibandCompressor();
        void process(Sample *buf, int frames, int channel, std::vector<Sample> bandScratch[kBands]);

    private:
        // Crossovers at 200 / 1200 / 5000 Hz, each 2nd order (two cascaded one-poles).
        LowCascade mLp[kBands - 1];
        HighCascade mHp[kBands - 1];
        Compressor mComp[kBands];
        Overdrive mSat[kBands];
    };

    /** A 6-section parametric EQ: high-pass, low shelf, two peaking bands, high shelf,
     *  low-pass. Built the way an EQ is actually built — a filtered copy summed back at a
     *  gain — rather than by chaining cuts, so a band can boost as well as cut. */
    class ParametricEq
    {
    public:
        ParametricEq();
        void process(Sample *buf, int frames, int channel, Sample *scratch);

    private:
        HighCascade mHighPass;
        LowCascade mLowPass;
        LowPassFilter mLowShelf;
        LowPassFilter mPeak1Top, mPeak2Top;
        HighPassFilter mPeak1Bottom, mPeak2Bottom;
        HighPassFilter mHighShelf;
        Sample mLowGain, mPeak1Gain, mPeak2Gain, mHighGain;
    };

    /** The reverb send: a short dense early-reflection reverb feeding a long tail reverb,
     *  the wet path damped and widened by a chorus, then mixed back. One Reverb alone
     *  gives a tail with no room in front of it. */
    class ReverbSection
    {
    public:
        ReverbSection();
        void process(Sample *buf, int frames, int channel, Sample *wet);

    private:
        Reverb mEarly, mTail;
        LowPassFilter mDamping;
        Chorus mWiden;
        Sample mDry, mWetLevel;
    };

    /** The whole chain. Construction (which allocates every delay line and scratch buffer)
     *  is deliberately separate from process(), so only the processing is ever timed. */
    class MixChain
    {
    public:
        MixChain(int voices, int frames, int channels);

        /** Process `voiceBuffers[voice][channel]` through the strips, sum to the bus, run
         *  the master chain, and leave the result in the returned bus buffers.
         *  Allocation-free: everything it needs was built in the constructor. */
        const std::vector<std::vector<Sample>> &process(
            std::vector<std::vector<std::vector<Sample>>> &voiceBuffers);

        /** Stages counted for the card's detail line — per-voice strips plus master. */
        int stageCount() const;

    private:
        int mVoices, mFrames, mChannels;
        std::vector<std::unique_ptr<VoiceStrip>> mStrips;   // one per voice
        MultibandCompressor mMultiband;
        ParametricEq mEq;
        ReverbSection mReverb;
        Compressor mGlue;
        std::vector<std::vector<Sample>> mBus;              // [channel][frame]
        std::vector<Sample> mScratch, mWet;
        std::vector<Sample> mBandScratch[MultibandCompressor::kBands];
    };
}
}
