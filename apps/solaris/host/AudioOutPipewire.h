/*
 *  solaris_host — the clock device through PipeWire's own API (R-DEV-9): small buffers (`node.latency` asks for
 *  the setting's frames — 64 and up) and every underrun counted (R-DEV-5). The player writes into an AudioRing and
 *  waits for room — the back-pressure that is the clock, as Pulse's blocking write is; PipeWire's realtime callback
 *  takes from it and never waits. `open` fails, naming why, when PipeWire is not there or links nowhere within two
 *  seconds — the service then plays through PulseAudio (the fallback).
 */
#pragma once
#include "AudioOut.h"
#include "AudioRing.h"
#include <atomic>
#include <string>

struct pw_thread_loop;
struct pw_stream;

namespace arstro
{
namespace solaris_host
{
    class AudioOutPipewire : public solaris::IAudioOut
    {
    public:
        AudioOutPipewire();
        ~AudioOutPipewire() override;
        bool open(const std::string &device, int rate, int channels, int bufferFrames, std::string &err) override;
        bool write(const float *interleaved, int frames) override;
        double latency() override;
        void flush() override;
        long long xruns() const override { return mRing.xruns(); }

        static void onProcess(void *self);       // PipeWire's realtime thread
        void onState(int state, const char *error);

    private:
        void close();
        pw_thread_loop *mLoop = nullptr;
        pw_stream *mStream = nullptr;
        AudioRing mRing;
        int mChannels = 2, mRate = 48000, mBuffer = 256;
        std::atomic<bool> mAlive{false};
        std::atomic<int> mState{0};               // pw_stream_state
        std::string mError;
    };
}
}
