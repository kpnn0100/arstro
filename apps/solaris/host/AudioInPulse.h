/*
 *  solaris_host — a capture device through the sound server (R-REC-1, R-DEV-8): the PulseAudio "simple" API, which
 *  PipeWire also serves. A thread of its own reads (blocking) and pushes into an AudioRing, never waiting on it; the
 *  player takes a block from the ring each cycle and never waits either — what has not arrived is silence, counted.
 *  The latency is taken on the read thread (pa_simple is one thread's) and handed over through an atomic.
 */
#pragma once
#include "AudioIn.h"
#include "AudioRing.h"
#include <atomic>
#include <thread>
#include <vector>

struct pa_simple;

namespace arstro
{
namespace solaris_host
{
    class AudioInPulse : public solaris::IAudioIn
    {
    public:
        ~AudioInPulse() override;
        bool open(const std::string &device, int rate, int channels, int bufferFrames, std::string &err) override;
        int read(float *interleaved, int frames) override;
        double latency() override;
        long long missed() const override { return mMissed.load() + mRing.dropped(); }

    private:
        void close();
        pa_simple *mPa = nullptr;
        AudioRing mRing;
        std::thread mThread;
        std::atomic<bool> mRun{false};
        std::atomic<double> mServerLatency{0};
        std::atomic<long long> mMissed{0};
        int mChannels = 2, mRate = 48000, mBuffer = 256;
    };
}
}
