#include "AudioInPipewire.h"
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

namespace arstro
{
namespace solaris_host
{
    namespace
    {
        void stateChanged(void *self, enum pw_stream_state, enum pw_stream_state state, const char *error)
        {
            static_cast<AudioInPipewire *>(self)->onState((int)state, error);
        }
        void process(void *self) { AudioInPipewire::onProcess(self); }
        pw_stream_events makeEvents()
        {
            pw_stream_events e{};
            e.version = PW_VERSION_STREAM_EVENTS;
            e.state_changed = stateChanged;
            e.process = process;
            return e;
        }
        const pw_stream_events kEvents = makeEvents();
    }

    AudioInPipewire::AudioInPipewire() { pw_init(nullptr, nullptr); }
    AudioInPipewire::~AudioInPipewire() { close(); }

    void AudioInPipewire::onState(int state, const char *error)
    {
        mState.store(state);
        if (state == PW_STREAM_STATE_ERROR) mError = error ? error : "the stream failed";
    }

    void AudioInPipewire::onProcess(void *p)
    {
        // the realtime thread: what was captured into the ring — what does not fit dropped, counted; no wait, no lock
        auto *self = static_cast<AudioInPipewire *>(p);
        pw_buffer *b = pw_stream_dequeue_buffer(self->mStream);
        if (!b) return;
        spa_buffer *buf = b->buffer;
        const float *src = static_cast<const float *>(buf->datas[0].data);
        if (src && buf->datas[0].chunk)
        {
            const uint32_t stride = (uint32_t)(sizeof(float) * (size_t)self->mChannels);
            const uint32_t offset = std::min(buf->datas[0].chunk->offset, buf->datas[0].maxsize);
            const uint32_t size = std::min(buf->datas[0].chunk->size, buf->datas[0].maxsize - offset);
            self->mRing.push(reinterpret_cast<const float *>(reinterpret_cast<const char *>(src) + offset), (int)(size / stride));
            self->mStamp.mark((int)(size / stride)); // R-DEV-2: when it landed
        }
        pw_time t{};
        if (pw_stream_get_time(self->mStream, &t) == 0 && t.rate.denom > 0)
            self->mServerDelay.store((double)t.delay * (double)t.rate.num / (double)t.rate.denom);
        pw_stream_queue_buffer(self->mStream, b);
    }

    bool AudioInPipewire::open(const std::string &device, int rate, int channels, int bufferFrames, std::string &err)
    {
        close();
        mChannels = std::clamp(channels, 1, 2);
        mRate = rate;
        mBuffer = std::max(32, bufferFrames);
        mRing.reset(mChannels, std::max(8 * mBuffer, rate / 2));
        mError.clear();
        mMissed.store(0);
        mState.store(PW_STREAM_STATE_UNCONNECTED);
        mLoop = pw_thread_loop_new("solaris-in", nullptr);
        if (!mLoop) { err = "PipeWire: no thread loop"; return false; }
        char latency[32];
        std::snprintf(latency, sizeof latency, "%d/%d", mBuffer, rate);
        pw_properties *props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE, "Production",
                                                 PW_KEY_APP_NAME, "Solaris", PW_KEY_NODE_NAME, "solaris-in", PW_KEY_NODE_LATENCY, latency, nullptr);
        if (!device.empty())
        {
            pw_properties_set(props, PW_KEY_TARGET_OBJECT, device.c_str());
            pw_properties_set(props, PW_KEY_NODE_TARGET, device.c_str());
        }
        mStream = pw_stream_new_simple(pw_thread_loop_get_loop(mLoop), "Solaris input", props, &kEvents, this);
        if (!mStream) { err = "PipeWire: cannot make a stream (is PipeWire running?)"; close(); return false; }
        uint8_t pod[1024];
        spa_pod_builder b = SPA_POD_BUILDER_INIT(pod, sizeof pod);
        spa_audio_info_raw info{};
        info.format = SPA_AUDIO_FORMAT_F32;
        info.channels = (uint32_t)mChannels;
        info.rate = (uint32_t)rate;
        const spa_pod *params[1] = {spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info)};
        if (pw_thread_loop_start(mLoop) < 0) { err = "PipeWire: the thread loop did not start"; close(); return false; }
        pw_thread_loop_lock(mLoop);
        const int r = pw_stream_connect(mStream, PW_DIRECTION_INPUT, PW_ID_ANY,
                                        (pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS), params, 1);
        pw_thread_loop_unlock(mLoop);
        if (r < 0) { err = "PipeWire: the stream did not connect"; close(); return false; }
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < until)
        {
            const int s = mState.load();
            if (s == PW_STREAM_STATE_STREAMING) return true;
            if (s == PW_STREAM_STATE_ERROR) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        err = "PipeWire: " + (mError.empty() ? std::string("the stream linked to no input within 2 s") : mError);
        close();
        return false;
    }

    int AudioInPipewire::read(float *interleaved, int frames)
    {
        const int got = mRing.read(interleaved, frames);
        if (got < frames && mRing.xruns() > 0) mMissed.fetch_add(frames - got);
        return got;
    }

    double AudioInPipewire::latency() { return mServerDelay.load() + (double)mRing.filled() / std::max(1, mRate); }

    void AudioInPipewire::close()
    {
        if (mLoop) pw_thread_loop_stop(mLoop);
        if (mStream) pw_stream_destroy(mStream);
        mStream = nullptr;
        if (mLoop) pw_thread_loop_destroy(mLoop);
        mLoop = nullptr;
    }
}
}
