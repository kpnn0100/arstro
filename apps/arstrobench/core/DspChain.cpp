#include "DspChain.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace arstrobench
{
    namespace
    {
        /** out += gain * filtered(out) — the shelf/peak idiom: filter a COPY and sum it
         *  back, so the band can boost as well as cut. `scratch` is the copy. */
        template <typename F>
        void sumFiltered(Sample *buf, int frames, int channel, Sample *scratch, Sample gain, F &&filter)
        {
            std::copy(buf, buf + frames, scratch);
            filter(scratch, frames, channel);
            for (int i = 0; i < frames; ++i) buf[i] += gain * scratch[i];
        }
    }

    // ── VoiceStrip ──────────────────────────────────────────────────────────────────
    VoiceStrip::VoiceStrip(Sample tiltHz, Sample driveAmount)
        : mRumbleCut(2, 60.0), mLowGain(0.35), mMidGain(-0.28), mHighGain(0.22)
    {
        mComp.setThresholdDb(-20.0);
        mComp.setRatio(3.0);
        mComp.setAttackMs(6.0);
        mComp.setReleaseMs(90.0);
        mComp.setMakeupGain(1.4);

        // The three EQ bands are tilted per voice, so no two strips are the same work in
        // the same place — a bass voice and a lead voice are not EQ'd identically.
        mLowShelf.setCutoffFrequency(160.0);
        mMidBottom.setCutoffFrequency(tiltHz);
        mMidTop.setCutoffFrequency(tiltHz * 3.0);
        mHighShelf.setCutoffFrequency(6500.0);

        mSaturation.setDrive(driveAmount);
        mSaturation.setToneHz(5200.0);
        mSaturation.setLevel(0.85);
    }

    void VoiceStrip::process(Sample *buf, int frames, int channel, Sample *scratch)
    {
        mRumbleCut.process(buf, frames, channel);
        mComp.processBlock(buf, frames, channel);
        sumFiltered(buf, frames, channel, scratch, mLowGain,
                    [this](Sample *b, int n, int c) { mLowShelf.processBlock(b, n, c); });
        sumFiltered(buf, frames, channel, scratch, mMidGain,
                    [this](Sample *b, int n, int c) {
                        mMidTop.processBlock(b, n, c);      // band-pass: top then bottom
                        mMidBottom.processBlock(b, n, c);
                    });
        sumFiltered(buf, frames, channel, scratch, mHighGain,
                    [this](Sample *b, int n, int c) { mHighShelf.processBlock(b, n, c); });
        mSaturation.processBlock(buf, frames, channel);
    }

    // ── MultibandCompressor ─────────────────────────────────────────────────────────
    MultibandCompressor::MultibandCompressor()
    {
        const Sample crossover[kBands - 1] = {200.0, 1200.0, 5000.0};
        for (int i = 0; i < kBands - 1; ++i)
        {
            mLp[i] = LowCascade(2, crossover[i]);   // 2nd-order crossover halves
            mHp[i] = HighCascade(2, crossover[i]);
        }
        // Per-band dynamics: the low band is held hardest, the air band barely touched —
        // the reason a multiband exists at all.
        const Sample threshold[kBands] = {-24.0, -20.0, -18.0, -14.0};
        const Sample ratio[kBands] = {5.0, 3.5, 2.5, 2.0};
        const Sample attack[kBands] = {14.0, 8.0, 4.0, 2.0};
        const Sample release[kBands] = {180.0, 120.0, 80.0, 50.0};
        const Sample drive[kBands] = {1.6, 1.3, 1.15, 1.05};
        for (int b = 0; b < kBands; ++b)
        {
            mComp[b].setThresholdDb(threshold[b]);
            mComp[b].setRatio(ratio[b]);
            mComp[b].setAttackMs(attack[b]);
            mComp[b].setReleaseMs(release[b]);
            mComp[b].setMakeupGain(1.25);
            mSat[b].setDrive(drive[b]);
            mSat[b].setToneHz(8000.0);
            mSat[b].setLevel(0.9);
        }
    }

    void MultibandCompressor::process(Sample *buf, int frames, int channel,
                                      std::vector<Sample> bandScratch[kBands])
    {
        // Split: band 0 is everything below the first crossover, band kBands-1 everything
        // above the last, and each middle band is bounded on both sides.
        for (int b = 0; b < kBands; ++b)
        {
            Sample *band = bandScratch[b].data();
            std::copy(buf, buf + frames, band);
            if (b > 0) mHp[b - 1].process(band, frames, channel);
            if (b < kBands - 1) mLp[b].process(band, frames, channel);
            mComp[b].processBlock(band, frames, channel);
            mSat[b].processBlock(band, frames, channel);
        }
        // Sum in FIXED band order: float addition is not associative, so a fixed order is
        // what keeps the checksum reproducible.
        for (int i = 0; i < frames; ++i)
        {
            Sample sum = 0.0;
            for (int b = 0; b < kBands; ++b) sum += bandScratch[b][(size_t)i];
            buf[i] = sum * 0.25;
        }
    }

    // ── ParametricEq ────────────────────────────────────────────────────────────────
    ParametricEq::ParametricEq()
        : mHighPass(2, 35.0), mLowPass(2, 17000.0),
          mLowGain(0.28), mPeak1Gain(-0.22), mPeak2Gain(0.30), mHighGain(0.18)
    {
        mLowShelf.setCutoffFrequency(140.0);
        mPeak1Bottom.setCutoffFrequency(400.0);
        mPeak1Top.setCutoffFrequency(1100.0);
        mPeak2Bottom.setCutoffFrequency(2200.0);
        mPeak2Top.setCutoffFrequency(5600.0);
        mHighShelf.setCutoffFrequency(8000.0);
    }

    void ParametricEq::process(Sample *buf, int frames, int channel, Sample *scratch)
    {
        mHighPass.process(buf, frames, channel);   // section 1: low cut
        sumFiltered(buf, frames, channel, scratch, mLowGain,   // section 2: low shelf
                    [this](Sample *b, int n, int c) { mLowShelf.processBlock(b, n, c); });
        sumFiltered(buf, frames, channel, scratch, mPeak1Gain, // section 3: peak (mud cut)
                    [this](Sample *b, int n, int c) {
                        mPeak1Top.processBlock(b, n, c);
                        mPeak1Bottom.processBlock(b, n, c);
                    });
        sumFiltered(buf, frames, channel, scratch, mPeak2Gain, // section 4: peak (presence)
                    [this](Sample *b, int n, int c) {
                        mPeak2Top.processBlock(b, n, c);
                        mPeak2Bottom.processBlock(b, n, c);
                    });
        sumFiltered(buf, frames, channel, scratch, mHighGain,  // section 5: high shelf (air)
                    [this](Sample *b, int n, int c) { mHighShelf.processBlock(b, n, c); });
        mLowPass.process(buf, frames, channel);    // section 6: high cut
    }

    // ── ReverbSection ───────────────────────────────────────────────────────────────
    ReverbSection::ReverbSection() : mDry(0.68), mWetLevel(0.32)
    {
        // Early reflections: short, dense, nearly no tail — the room in FRONT of the tail.
        mEarly.setDelayInMs(12.0);
        mEarly.setDecayInMs(280.0);
        mEarly.setDiffusion(4);
        mEarly.setMix(1.0);
        mEarly.setWidth(0.55);
        // The tail: long and wide, fed by the early reflections rather than the dry signal.
        mTail.setDelayInMs(46.0);
        mTail.setDecayInMs(2600.0);
        mTail.setDiffusion(4);
        mTail.setMix(1.0);
        mTail.setWidth(0.85);

        mDamping.setCutoffFrequency(6200.0);  // a real room loses its highs first
        mWiden.setRateHz(0.35);
        mWiden.setDepthMs(3.0);
        mWiden.setBaseDelayMs(9.0);
        mWiden.setMix(0.45);
    }

    void ReverbSection::process(Sample *buf, int frames, int channel, Sample *wet)
    {
        std::copy(buf, buf + frames, wet);
        mEarly.processBlock(wet, frames, channel);
        mTail.processBlock(wet, frames, channel);
        mDamping.processBlock(wet, frames, channel);
        mWiden.processBlock(wet, frames, channel);
        for (int i = 0; i < frames; ++i) buf[i] = mDry * buf[i] + mWetLevel * wet[i];
    }

    // ── MixChain ────────────────────────────────────────────────────────────────────
    MixChain::MixChain(int voices, int frames, int channels)
        : mVoices(voices), mFrames(frames), mChannels(channels)
    {
        for (int v = 0; v < mVoices; ++v)
        {
            // Each strip is tilted differently across the nine voices, spanning the range
            // a real arrangement covers rather than nine copies of one setting.
            const Sample tilt = 220.0 * std::pow(2.0, v * 0.42);
            const Sample drive = 1.1 + 0.12 * v;
            mStrips.push_back(std::make_unique<VoiceStrip>(tilt, drive));
        }
        mGlue.setThresholdDb(-12.0);
        mGlue.setRatio(2.0);
        mGlue.setAttackMs(20.0);
        mGlue.setReleaseMs(200.0);
        mGlue.setMakeupGain(1.1);

        // Everything the process pass touches is allocated HERE, so the timed region does
        // no allocation and measures signal processing rather than the allocator.
        mBus.assign((size_t)mChannels, std::vector<Sample>((size_t)mFrames, 0.0));
        mScratch.assign((size_t)mFrames, 0.0);
        mWet.assign((size_t)mFrames, 0.0);
        for (int b = 0; b < MultibandCompressor::kBands; ++b)
            mBandScratch[b].assign((size_t)mFrames, 0.0);
    }

    int MixChain::stageCount() const
    {
        // per voice: rumble cut + comp + 3 EQ bands + saturation = 6
        // master: 4 multiband bands + 6 EQ sections + 4 reverb-section stages + glue = 15
        return mVoices * 6 + 15;
    }

    const std::vector<std::vector<Sample>> &MixChain::process(
        std::vector<std::vector<std::vector<Sample>>> &voiceBuffers)
    {
        for (int ch = 0; ch < mChannels; ++ch)
        {
            Sample *bus = mBus[(size_t)ch].data();
            std::fill(bus, bus + mFrames, 0.0);

            // ── per-voice channel strips, summed to the bus ──
            for (int v = 0; v < mVoices; ++v)
            {
                Sample *voice = voiceBuffers[(size_t)v][(size_t)ch].data();
                mStrips[(size_t)v]->process(voice, mFrames, ch, mScratch.data());
                for (int i = 0; i < mFrames; ++i) bus[i] += voice[i];
            }
            const Sample norm = 1.0 / mVoices;
            for (int i = 0; i < mFrames; ++i) bus[i] *= norm;

            // ── master bus ──
            mMultiband.process(bus, mFrames, ch, mBandScratch);
            mEq.process(bus, mFrames, ch, mScratch.data());
            mReverb.process(bus, mFrames, ch, mWet.data());
            mGlue.processBlock(bus, mFrames, ch);
        }
        return mBus;
    }
}
}
