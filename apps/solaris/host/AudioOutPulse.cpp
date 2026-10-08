#include "AudioOutPulse.h"
#include <pulse/error.h>
#include <pulse/simple.h>

namespace arstro
{
namespace solaris_host
{
    AudioOutPulse::~AudioOutPulse()
    {
        if (mPa)
        {
            pa_simple_drain(mPa, nullptr);
            pa_simple_free(mPa);
        }
    }

    bool AudioOutPulse::open(const std::string &device, int rate, int channels, int bufferFrames, std::string &err)
    {
        mChannels = channels;
        pa_sample_spec ss;
        ss.format = PA_SAMPLE_FLOAT32NE;
        ss.channels = (uint8_t)channels;
        ss.rate = (uint32_t)rate; // the server resamples when the device runs at another rate (R-DEV-7)
        pa_buffer_attr attr;
        const uint32_t bytes = (uint32_t)bufferFrames * (uint32_t)channels * sizeof(float);
        attr.maxlength = (uint32_t)-1;
        attr.tlength = 2 * bytes;  // two buffers queued: the latency the Settings dialog quotes, ×2
        attr.prebuf = (uint32_t)-1;
        attr.minreq = bytes;
        attr.fragsize = (uint32_t)-1;
        int e = 0;
        mPa = pa_simple_new(nullptr, "Solaris", PA_STREAM_PLAYBACK, device.empty() ? nullptr : device.c_str(), "playback",
                            &ss, nullptr, &attr, &e);
        if (!mPa)
        {
            err = std::string("cannot open ") + (device.empty() ? "the default output" : device) + ": " + pa_strerror(e);
            return false;
        }
        return true;
    }

    bool AudioOutPulse::write(const float *interleaved, int frames)
    {
        if (!mPa || frames <= 0) return false;
        int e = 0;
        return pa_simple_write(mPa, interleaved, (size_t)frames * (size_t)mChannels * sizeof(float), &e) >= 0;
    }

    double AudioOutPulse::latency()
    {
        if (!mPa) return 0.0;
        int e = 0;
        const pa_usec_t us = pa_simple_get_latency(mPa, &e);
        return us == (pa_usec_t)-1 ? 0.0 : (double)us / 1e6;
    }

    void AudioOutPulse::flush()
    {
        if (mPa) pa_simple_flush(mPa, nullptr);
    }
}
}
