/*
 *  solaris_host — a capture device through PipeWire's own API (R-REC-1, R-DEV-9): small buffers, as the output's.
 *  PipeWire's realtime callback pushes what it captured into an AudioRing and never waits (what does not fit is
 *  dropped and counted); the player takes a block each cycle and never waits either. `open` fails, naming why, when
 *  PipeWire is not there or links nowhere within two seconds — the service then records through PulseAudio.
 */
#pragma once
#include "AudioIn.h"
#include "AudioRing.h"
#include <atomic>
#include <string>

struct pw_thread_loop;
struct pw_stream;

namespace arstro
{
namespace solaris_host
{
    class AudioInPipewire : public solaris::IAudioIn
    {
    public:
        AudioInPipewire();
        ~AudioInPipewire() override;
        bool open(const std::string &device, int rate, int channels, int bufferFrames, std::string &err) override;
        int read(float *interleaved, int frames) override;
        double latency() override;
        long long missed() const override { return mMissed.load() + mRing.dropped(); }

        static void onProcess(void *self); // PipeWire's realtime thread
        void onState(int state, const char *error);

    private:
        void close();
        pw_thread_loop *mLoop = nullptr;
        pw_stream *mStream = nullptr;
        AudioRing mRing;
        int mChannels = 2, mRate = 48000, mBuffer = 256;
        std::atomic<int> mState{0};
        std::atomic<long long> mMissed{0};
        std::atomic<double> mServerDelay{0};
        std::string mError;
    };
}
}
