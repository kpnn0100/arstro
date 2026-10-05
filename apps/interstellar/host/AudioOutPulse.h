/*
 *  interstellar/host — AudioOutPulse: `IAudioOut` over PulseAudio's simple API (R-AUD-6), which
 *  PipeWire also serves. A playback stream of stereo float32 with a ~60 ms target buffer: short
 *  enough that a pause is heard to stop, long enough not to drop out while a frame is graded.
 */
#pragma once
#include "../core/AudioOut.h"

struct pa_simple;

namespace arstro
{
namespace interstellar_host
{
    class AudioOutPulse : public interstellar::IAudioOut
    {
    public:
        AudioOutPulse() = default;
        ~AudioOutPulse() override;
        AudioOutPulse(const AudioOutPulse &) = delete;
        AudioOutPulse &operator=(const AudioOutPulse &) = delete;

        bool open(int rate, std::string &err) override;
        bool write(const float *stereo, int frames) override;
        double latency() override;
        void flush() override;

    private:
        pa_simple *mPa = nullptr;
        int mRate = 48000;
    };
}
}
