#include "DspWorkload.h"
#include "DspChain.h"
#include "base/AudioConfig.h"
#include "synth/Voice.h"
#include <chrono>
#include <cmath>
#include <string>

namespace arstro
{
namespace arstrobench
{
    namespace
    {
        // C2 G2 C3 E3 G3 B3 D4 F#4 A4 — an extended chord spread over three octaves, so
        // the 9 voices cover the spectrum instead of stacking near one frequency.
        const int kNotes[DspWorkload::kVoices] = {36, 43, 48, 52, 55, 59, 62, 66, 69};

        /** Set the global audio configuration the DSP library reads from its statics.
         *  Done once per run, outside the timed region. */
        void configureAudio()
        {
            AudioConfig &cfg = AudioConfig::instance();
            cfg.setSampleRate(DspWorkload::kSampleRate);
            cfg.setChannelCount(DspWorkload::kChannels);
            cfg.setBufferSize(512);
        }

        double foldChecksum(const std::vector<std::vector<Sample>> &buffers)
        {
            double acc = 0.0;
            for (const auto &buf : buffers)
                for (size_t i = 0; i < buf.size(); i += 97)
                    acc += std::fabs(buf[i]);
            return acc;
        }
    }

    const int *DspWorkload::notes() { return kNotes; }

    std::string DspWorkload::describe() const
    {
        // The stage count comes from the chain itself rather than a literal, so the line
        // cannot drift from what is actually run.
        const MixChain probe(kVoices, 1, kChannels);
        return std::to_string(kVoices) + " voices  ·  " + std::to_string(mFrames) +
               " samples  ·  " + std::to_string(probe.stageCount()) + "-stage mix";
    }

    std::vector<std::vector<std::vector<Sample>>> DspWorkload::generate() const
    {
        configureAudio();

        // 9 voices, each on its own note, with per-voice waveform / detune / velocity
        // variation so no two produce the same signal (R-DSP-1).
        std::vector<Voice> voices((size_t)kVoices);
        for (int v = 0; v < kVoices; ++v)
        {
            Voice &voice = voices[(size_t)v];
            for (int o = 0; o < Voice::kOscCount; ++o)
            {
                Oscillator &osc = voice.osc(o);
                osc.setWaveform(o == 0 ? (v % 2 ? Oscillator::Saw : Oscillator::Square)
                                       : Oscillator::Triangle);
                osc.setVoiceCount(3);                       // unison, so a voice is not one partial
                osc.setDetuneCents(6.0 + v * 0.8);
                osc.setStereoSpreadCents(4.0 + v * 0.5);
                osc.setAttackMs(5.0 + v);
                osc.setDecayMs(120.0);
                osc.setSustain(0.7);
                osc.setReleaseMs(400.0);
                voice.setOscTuneSemitones(o, o == 0 ? 0.0 : -12.0);
                voice.setOscLevel(o, o == 0 ? 0.62 : 0.38);
            }
            voice.noteOn(kNotes[v], 0.55 + 0.04 * v);
        }

        // One buffer PER VOICE per channel: the mix processes each voice through its own
        // channel strip before summing, so summing here would destroy the thing measured.
        std::vector<std::vector<std::vector<Sample>>> buffers(
            (size_t)kVoices,
            std::vector<std::vector<Sample>>((size_t)kChannels,
                                             std::vector<Sample>((size_t)mFrames, 0.0)));
        for (int v = 0; v < kVoices; ++v)
            for (int ch = 0; ch < kChannels; ++ch)
                voices[(size_t)v].renderBlock(buffers[(size_t)v][(size_t)ch].data(), mFrames, ch);
        return buffers;
    }

    WorkloadResult DspWorkload::run() const
    {
        if (mFrames <= 0 || mPasses <= 0)
            return WorkloadResult{};

        // ── generation: outside the clock (R-DSP-3) ──
        const std::vector<std::vector<std::vector<Sample>>> dry = generate();

        double best = 0.0, checksum = 0.0;
        for (int pass = 0; pass < mPasses; ++pass)
        {
            // Fresh chain state and a fresh copy of the dry signal every pass (R-DSP-4).
            // Both are built before the clock starts, and MixChain's constructor pre-
            // allocates every scratch buffer, so the timed region allocates nothing.
            MixChain chain(kVoices, mFrames, kChannels);
            std::vector<std::vector<std::vector<Sample>>> wet = dry;

            // ── measurement: strips -> bus -> multiband -> EQ -> reverb -> glue ──
            const auto t0 = std::chrono::steady_clock::now();
            const std::vector<std::vector<Sample>> &out = chain.process(wet);
            const auto t1 = std::chrono::steady_clock::now();

            const double secs = std::chrono::duration<double>(t1 - t0).count();
            if (pass == 0 || secs < best) best = secs;
            checksum = foldChecksum(out);  // after the clock stops
        }

        WorkloadResult r = WorkloadResult::fromSeconds(best);
        r.checksum = checksum;
        r.detail = describe();
        return r;
    }
}
}
