/*
 *  solaris_core — a FOLLOWER device (R-DEV-1/2/4/5, discussion Round 11): a second output played clocked to the first.
 *
 *  The clock's audio thread renders as ever and `push`es the ports this device carries into a ring — single producer,
 *  single consumer, made before play: no lock, no allocation, never a wait (a full ring drops the block, counted). The
 *  follower's own thread pulls a device block at a time through the DSP library's AdaptiveResampler, its step set by
 *  a FillController holding the ring's fill at a target (REQ-resample-2/3) — so the two crystals never part — and
 *  writes it to its own device, whose back-pressure paces that thread as the clock's paces the audio thread.
 *
 *  The fill the controller reads is continuous: what was pushed, plus the frames the clock has made since its last
 *  push (by a time stamp each push carries), less that push, less what was taken, plus what the resampler holds — a
 *  raw count would be a sawtooth a block deep whose beat no smoother removes (src/resample/README.md).
 *
 *  Primed before it plays: silence until the fill reaches its target (what came past it skipped); an underrun (the
 *  clock stalled) primes it again, its drift kept; far ahead (its device stalled), emptied, it primes again too. The target is the path's delay — the fill counts from the clock's render to this device's read, the
 *  resampler's lookahead inside it — so the device's latency plus target over the rate is when a pushed frame is heard
 *  (R-DEV-4); the service sets it so every port is heard at the same moment. It must hold the lookahead and two
 *  blocks (`minTarget`).
 */
#pragma once
#include "AudioOut.h"
#include "resample/AdaptiveResampler.h"
#include "resample/FillController.h"
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace arstro
{
namespace solaris
{
    class Follower
    {
    public:
        struct Config
        {
            std::string device;      // its id, as the settings name it
            int rate = 48000;        // the song's (the clock's)
            int channels = 2;        // what it was opened with
            int block = 256;         // its device block, frames
            double target = 1024.0;  // the ring's fill held, frames — the path's delay
            double startPpm = 0.0;   // its drift as last measured (a run starts locked)
        };
        /** What it says (R-DEV-5). */
        struct State
        {
            std::string device;
            double latency = 0.0;    // seconds from a push to its being heard: the device's and the target's
            double driftPpm = 0.0;   // its crystal against the clock's: + it runs fast
            double fill = 0.0;       // the ring's fill, smoothed, frames
            long long xruns = -1;    // its device's underruns (−1: the driver cannot count them)
            long long starved = 0;   // blocks it had too little to play (the clock stalled): primed again
            long long dropped = 0;   // pushes it had no room for (its device stalled)
        };
        /** `out` already open on the device. The ring holds four targets and a few blocks. */
        Follower(std::unique_ptr<IAudioOut> out, const Config &c);
        ~Follower();
        void start();
        /** Its thread ends (its device left with what it was given). */
        void stop();
        int channels() const { return mCfg.channels; }
        const std::string &device() const { return mCfg.device; }

        /** AUDIO THREAD: `frames` of its channels, interleaved. Never blocks, never allocates. */
        void push(const float *interleaved, int frames);
        /** Any thread. */
        State state() const;
        /** Seconds from a push to its being heard, at `target` frames: the alignment's sum. */
        double pathLatency(double target) const;
        /** The least target that never starves: the lookahead, the clock's block and its own, twice over. */
        static double minTarget(int clockBlock, int block) { return 16.0 + 2.0 * (clockBlock + block); }
        /** (F6) a new target: the fill travels to it, the step never kicking. */
        void retarget(double target) { mRetarget.store(target, std::memory_order_relaxed); }

        /** One device block — what the thread repeats; a test may step it instead. */
        void pullOnce();
        /** The clock it stamps pushes with, ns (a test may set its own before start). */
        long long (*now)() = steadyNs;
        static long long steadyNs();

    private:
        void run();
        Config mCfg;
        std::unique_ptr<IAudioOut> mOut;
        AdaptiveResampler mRs;
        FillController mCtl;
        std::vector<float> mRing, mIn, mOutBuf;
        long long mCap = 0;                       // frames, a power of two
        std::atomic<long long> mHead{0};          // frames pushed, ever (the producer's)
        std::atomic<long long> mTail{0};          // frames taken, ever (the consumer's)
        // the last push, seqlocked: its end (frames pushed after it), its size and its time stamp
        std::atomic<unsigned> mSeq{0};
        std::atomic<long long> mPushEnd{0}, mPushFrames{0}, mPushNs{0};
        std::atomic<double> mRetarget{-1.0};
        bool mPrimed = false;
        std::atomic<bool> mRun{false};
        std::thread mThread;
        // what it says
        std::atomic<double> mDrift{0.0}, mFill{0.0}, mTargetNow{0.0}; // the target as its ramp has it (F6)
        std::atomic<double> mDevLatency{0.0};     // its device's, read on its thread after each write (a stream's own
                                                  // latency call is not safe beside a write on another thread)
        std::atomic<long long> mStarved{0}, mDropped{0};
    };
}
}
