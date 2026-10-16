/*
 *  solaris_core — an INPUT on another device, held to the clock (R-DEV-1/2, discussion Round 11): the output
 *  follower's loop the other way round. The host's capture fills its ring on its device's crystal; the audio thread,
 *  on the clock's, reads it through the DSP library's AdaptiveResampler, the step from a FillController holding what
 *  the ring holds at a target (REQ-resample-2/3). The fill is continuous — what the capture holds, less its last
 *  block, plus what its device has made since that block landed (`IAudioIn::held`), plus what the resampler holds.
 *
 *  It IS an input (the player reads it as any other): primed before it gives sound (silence until the fill reaches
 *  its target, what came past it skipped); starved (its device stalled) or far ahead (the clock stalled), silent and
 *  primed again. Its latency is its device's plus the target — the recording's alignment counts it (R-DEV-4).
 *  A capture that cannot say what it holds (`held` false) is read as it is.
 */
#pragma once
#include "AudioIn.h"
#include "resample/AdaptiveResampler.h"
#include "resample/FillController.h"
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris
{
    class DriftIn : public IAudioIn
    {
    public:
        struct State
        {
            std::string device;
            double latency = 0.0;  // seconds from a sound at its input to the clock reading it
            double driftPpm = 0.0; // its crystal against the clock's: + it runs fast
            long long starved = 0; // blocks its device had too little for (primed again)
        };
        /** `in` already open on `device`; read in blocks of up to `block`, held at `target` frames. */
        DriftIn(std::unique_ptr<IAudioIn> in, const std::string &device, int rate, int channels, int block, double target, double startPpm = 0.0);
        bool open(const std::string &, int, int, int, std::string &) override { return true; } // opened before
        /** The audio thread: `frames` read on the clock — never blocks, never allocates. */
        int read(float *interleaved, int frames) override;
        double latency() override { return mLatency.load(std::memory_order_relaxed); }
        long long missed() const override { return mIn->missed() + mStarvedFrames.load(std::memory_order_relaxed); }
        State state() const;
        /** The clock it reads `now` by, ns (a test may set its own). */
        long long (*now)() = steadyNs;
        static long long steadyNs();

    private:
        void discard(long long frames);
        std::unique_ptr<IAudioIn> mIn;
        std::string mDevice;
        int mRate, mChannels, mBlock;
        AdaptiveResampler mRs;
        FillController mCtl;
        std::vector<float> mTmp;
        bool mPrimed = false;
        std::atomic<double> mLatency{0.0}, mDrift{0.0};
        std::atomic<long long> mStarved{0}, mStarvedFrames{0};
    };
}
}
