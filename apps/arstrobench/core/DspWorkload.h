/*
 *  Arstrobench by arstro — the DigitalSignalProcessing workload (R-DSP).
 *
 *  A 9-voice synth sounding 9 notes at once is rendered into ONE BUFFER PER VOICE (not
 *  timed — R-DSP-3), and only the mix that follows is measured: nine per-voice channel
 *  strips into a master bus of multiband compression, a 6-section parametric EQ and a
 *  two-stage reverb send, over 192000 samples per channel at 48 kHz. The chain itself
 *  lives in DspChain.h.
 *
 *  Per-voice buffers rather than one summed buffer is what makes the strips possible:
 *  a mix processes each voice before it reaches the bus, and collapsing to a sum first
 *  would throw that away.
 *
 *  The library's own VoiceManager caps polyphony at 8, so the bench owns its bank of
 *  9 arstro::Voice objects rather than bending the library to a benchmark's shape.
 *
 *  UI-free: depends only on arstro_dsp.
 */
#pragma once
#include "Workload.h"
#include "base/Sample.h"
#include <string>
#include <vector>

namespace arstro
{
namespace arstrobench
{
    class DspWorkload
    {
    public:
        // Fixed workload (R-SCORE-4).
        static constexpr int kVoices = 9;       ///< R-DSP-1
        static constexpr int kFrames = 192000;  ///< R-DSP-2: four seconds at 48 kHz
        static constexpr int kPasses = 3;       ///< R-SCORE-3
        static constexpr double kSampleRate = 48000.0;
        static constexpr int kChannels = 2;

        /** Defaults are the real workload; the smaller frame counts exist for the unit
         *  tests (R-TEST-1). */
        explicit DspWorkload(int frames = kFrames, int passes = kPasses)
            : mFrames(frames), mPasses(passes) {}

        WorkloadResult run() const;

        /** Render the 9-voice chord into `[voice][channel][frame]`. Public so a test can
         *  assert the synth actually produced signal before the chain is measured. */
        std::vector<std::vector<std::vector<Sample>>> generate() const;

        /** The 9 simultaneous MIDI notes (R-DSP-1) — an extended chord over three octaves. */
        static const int *notes();

        /** One line naming the work this instance will do (shown before and after a run). */
        std::string describe() const;

        int frames() const { return mFrames; }

    private:
        int mFrames, mPasses;
    };
}
}
