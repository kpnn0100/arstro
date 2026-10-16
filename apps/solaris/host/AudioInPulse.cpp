#include "AudioInPulse.h"
#include <pulse/error.h>
#include <pulse/simple.h>
#include <algorithm>

namespace arstro
{
namespace solaris_host
{
    AudioInPulse::~AudioInPulse() { close(); }

    void AudioInPulse::close()
    {
        mRun.store(false);
        if (mThread.joinable()) mThread.join(); // a read returns within a fragment
        if (mPa) pa_simple_free(mPa);
        mPa = nullptr;
    }

    bool AudioInPulse::open(const std::string &device, int rate, int channels, int bufferFrames, std::string &err)
    {
        close();
        mChannels = std::clamp(channels, 1, 2);
        mRate = rate;
        mBuffer = std::max(32, bufferFrames);
        pa_sample_spec ss;
        ss.format = PA_SAMPLE_FLOAT32NE;
        ss.channels = (uint8_t)mChannels;
        ss.rate = (uint32_t)rate; // the server resamples a device at another rate (R-DEV-7)
        pa_buffer_attr attr;
        attr.maxlength = (uint32_t)-1;
        attr.tlength = (uint32_t)-1;
        attr.prebuf = (uint32_t)-1;
        attr.minreq = (uint32_t)-1;
        attr.fragsize = (uint32_t)mBuffer * (uint32_t)mChannels * sizeof(float); // a fragment a buffer: what a read waits for
        int e = 0;
        mPa = pa_simple_new(nullptr, "Solaris", PA_STREAM_RECORD, device.empty() ? nullptr : device.c_str(), "record", &ss, nullptr, &attr, &e);
        if (!mPa)
        {
            err = std::string("cannot open ") + (device.empty() ? "the default input" : device) + ": " + pa_strerror(e);
            return false;
        }
        mRing.reset(mChannels, std::max(8 * mBuffer, rate / 2)); // half a second between the server and the player
        mMissed.store(0);
        mRun.store(true);
        mThread = std::thread([this] {
            std::vector<float> buf((size_t)mBuffer * (size_t)mChannels);
            while (mRun.load())
            {
                int e2 = 0;
                if (pa_simple_read(mPa, buf.data(), buf.size() * sizeof(float), &e2) < 0) break;
                mRing.push(buf.data(), mBuffer);
                mStamp.mark(mBuffer); // R-DEV-2: when it landed
                const pa_usec_t us = pa_simple_get_latency(mPa, &e2);
                if (us != (pa_usec_t)-1) mServerLatency.store((double)us / 1e6);
            }
        });
        return true;
    }

    int AudioInPulse::read(float *interleaved, int frames)
    {
        const int got = mRing.read(interleaved, frames); // what is missing is silence
        if (got < frames && mRing.xruns() > 0) mMissed.fetch_add(frames - got);
        return got;
    }

    double AudioInPulse::latency()
    {
        // what the server still holds, and what waits in the ring for the player
        return mServerLatency.load() + (double)mRing.filled() / std::max(1, mRate);
    }
}
}
