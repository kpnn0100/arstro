#include "AudioOutPulse.h"
#include <pulse/error.h>
#include <pulse/simple.h>

namespace arstro
{
namespace interstellar_host
{
    AudioOutPulse::~AudioOutPulse()
    {
        if (mPa)
        {
            pa_simple_flush(mPa, nullptr);
            pa_simple_free(mPa);
        }
    }

    bool AudioOutPulse::open(int rate, std::string &err)
    {
        mRate = rate > 0 ? rate : 48000;
        pa_sample_spec ss;
        ss.format = PA_SAMPLE_FLOAT32NE;
        ss.channels = 2;
        ss.rate = (uint32_t)mRate;
        pa_buffer_attr attr;
        attr.maxlength = (uint32_t)-1;
        attr.tlength = (uint32_t)(mRate * 0.06) * 2 * sizeof(float);   // the target buffer: ~60 ms
        attr.prebuf = (uint32_t)-1;
        attr.minreq = (uint32_t)-1;
        attr.fragsize = (uint32_t)-1;
        int e = 0;
        mPa = pa_simple_new(nullptr, "Interstellar", PA_STREAM_PLAYBACK, nullptr, "playback", &ss, nullptr, &attr, &e);
        if (!mPa)
        {
            err = std::string("no sound server: ") + pa_strerror(e);
            return false;
        }
        return true;
    }

    bool AudioOutPulse::write(const float *stereo, int frames)
    {
        if (!mPa || frames <= 0) return false;
        int e = 0;
        return pa_simple_write(mPa, stereo, (size_t)frames * 2 * sizeof(float), &e) >= 0;
    }

    double AudioOutPulse::latency()
    {
        if (!mPa) return 0.0;
        int e = 0;
        const pa_usec_t us = pa_simple_get_latency(mPa, &e);
        return us == (pa_usec_t)-1 ? 0.0 : us / 1e6;
    }

    void AudioOutPulse::flush()
    {
        if (mPa) pa_simple_flush(mPa, nullptr);
    }
}
}
