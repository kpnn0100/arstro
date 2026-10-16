#include "AudioOutPipewire.h"
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
            static_cast<AudioOutPipewire *>(self)->onState((int)state, error);
        }
        void process(void *self) { AudioOutPipewire::onProcess(self); }
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

    AudioOutPipewire::AudioOutPipewire() { pw_init(nullptr, nullptr); }

    AudioOutPipewire::~AudioOutPipewire() { close(); }

    void AudioOutPipewire::onState(int state, const char *error)
    {
        mState.store(state);
        if (state == PW_STREAM_STATE_ERROR)
        {
            mError = error ? error : "the stream failed";
            mAlive.store(false);
        }
    }

    void AudioOutPipewire::onProcess(void *p)
    {
        // the realtime thread: take what the player wrote, silence for the rest (an xrun, counted) — no wait, no lock
        auto *self = static_cast<AudioOutPipewire *>(p);
        pw_buffer *b = pw_stream_dequeue_buffer(self->mStream);
        if (!b) return;
        spa_buffer *buf = b->buffer;
        float *dst = static_cast<float *>(buf->datas[0].data);
        if (!dst) return;
        const uint32_t stride = (uint32_t)(sizeof(float) * (size_t)self->mChannels);
        uint32_t n = buf->datas[0].maxsize / stride;
#if PW_CHECK_VERSION(0, 3, 49)
        if (b->requested) n = std::min(n, (uint32_t)b->requested);
#else
        n = std::min(n, (uint32_t)self->mBuffer); // the frames a cycle wants, as `node.latency` asked
#endif
        self->mRing.read(dst, (int)n);
        buf->datas[0].chunk->offset = 0;
        buf->datas[0].chunk->stride = (int32_t)stride;
        buf->datas[0].chunk->size = n * stride;
        pw_stream_queue_buffer(self->mStream, b);
    }

    bool AudioOutPipewire::open(const std::string &device, int rate, int channels, int bufferFrames, std::string &err)
    {
        close();
        mChannels = std::clamp(channels, 1, 8);
        mRate = rate;
        mBuffer = std::max(32, bufferFrames);
        mRing.reset(mChannels, std::max(2 * mBuffer, 128)); // two buffers between the player and the server
        mError.clear();
        mState.store(PW_STREAM_STATE_UNCONNECTED);
        mLoop = pw_thread_loop_new("solaris-out", nullptr);
        if (!mLoop) { err = "PipeWire: no thread loop"; return false; }
        char latency[32];
        std::snprintf(latency, sizeof latency, "%d/%d", mBuffer, rate);
        pw_properties *props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback", PW_KEY_MEDIA_ROLE, "Music",
                                                 PW_KEY_APP_NAME, "Solaris", PW_KEY_NODE_NAME, "solaris", PW_KEY_NODE_LATENCY, latency, nullptr);
        if (!device.empty())
        {
            pw_properties_set(props, PW_KEY_TARGET_OBJECT, device.c_str());
            pw_properties_set(props, PW_KEY_NODE_TARGET, device.c_str()); // the older key, for an older session manager
        }
        mStream = pw_stream_new_simple(pw_thread_loop_get_loop(mLoop), "Solaris", props, &kEvents, this);
        if (!mStream) { err = "PipeWire: cannot make a stream (is PipeWire running?)"; close(); return false; }
        uint8_t pod[1024];
        spa_pod_builder b = SPA_POD_BUILDER_INIT(pod, sizeof pod);
        spa_audio_info_raw info{};
        info.format = SPA_AUDIO_FORMAT_F32;
        info.channels = (uint32_t)mChannels;
        info.rate = (uint32_t)rate;
        const spa_pod *params[1] = {spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info)};
        mAlive.store(true);
        if (pw_thread_loop_start(mLoop) < 0) { err = "PipeWire: the thread loop did not start"; close(); return false; }
        pw_thread_loop_lock(mLoop);
        const int r = pw_stream_connect(mStream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                                        (pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS), params, 1);
        pw_thread_loop_unlock(mLoop);
        if (r < 0) { err = "PipeWire: the stream did not connect"; close(); return false; }
        // linked and running within two seconds, or it is no clock
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < until)
        {
            const int s = mState.load();
            if (s == PW_STREAM_STATE_STREAMING) return true;
            if (s == PW_STREAM_STATE_ERROR) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        err = "PipeWire: " + (mError.empty() ? std::string("the stream linked to no output within 2 s") : mError);
        close();
        return false;
    }

    bool AudioOutPipewire::write(const float *interleaved, int frames)
    {
        return mAlive.load() && mRing.write(interleaved, frames, &mAlive);
    }

    double AudioOutPipewire::latency()
    {
        // what waits in the ring, and what the server holds after it
        double held = (double)mRing.filled();
        if (mStream)
        {
            pw_time t{};
            if (pw_stream_get_time(mStream, &t) == 0 && t.rate.denom > 0)
                held += (double)t.delay * (double)mRate * (double)t.rate.num / (double)t.rate.denom;
        }
        return held / std::max(1, mRate);
    }

    void AudioOutPipewire::flush() { mRing.clear(); }

    void AudioOutPipewire::close()
    {
        mAlive.store(false);
        if (mLoop) pw_thread_loop_stop(mLoop);
        if (mStream) pw_stream_destroy(mStream);
        mStream = nullptr;
        if (mLoop) pw_thread_loop_destroy(mLoop);
        mLoop = nullptr;
    }
}
}
