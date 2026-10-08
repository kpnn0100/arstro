/*
 *  solaris_host — the clock device through the sound server (R-PLAY-1, R-DEV-8). The PulseAudio
 *  "simple" API, which PipeWire also serves: a blocking write whose back-pressure is the clock.
 */
#pragma once
#include "AudioOut.h"

struct pa_simple;

namespace arstro
{
namespace solaris_host
{
    class AudioOutPulse : public solaris::IAudioOut
    {
    public:
        ~AudioOutPulse() override;
        bool open(const std::string &device, int rate, int channels, int bufferFrames, std::string &err) override;
        bool write(const float *interleaved, int frames) override;
        double latency() override;
        void flush() override;

    private:
        pa_simple *mPa = nullptr;
        int mChannels = 2;
    };
}
}
