/*
 *  solaris-cc ntwb — the sound played INTO THE PAGES that listen (R-SVC-7: `settings set driver=web`).
 *
 *  Solaris's engine plays through an IAudioOut whose blocking `write` is the clock (AudioOut.h). In an
 *  Arstro Remote session the user may be nowhere near the machine's speakers, so `ntwb serve`'s host has
 *  one more driver, "web": a WebAudioOut that keeps that same clock with the machine's steady clock — it
 *  lets the engine render exactly as fast as real time, a buffer ahead — and hands every block to a
 *  WebAudioHub as 16-bit PCM. The adapter (NtwbAdapter, `audio.listen`) drains the hub on its loop and
 *  sends the PCM as blobs on the stream `audio` to each page that listens; the page schedules them with
 *  the Web Audio API (web/js/model/audio.js). Nobody listening: the clock still runs (the transport moves,
 *  the meters move), the sound is dropped.
 *
 *  A flush (a stop, a seek) bumps the hub's GENERATION: the blobs carry it, and a page drops what it has
 *  scheduled from an older one, so a stop is heard as a stop, not as the buffer playing out.
 *
 *  Holds no behaviour of the service: it is a device, as AudioOutPulse is one.
 */
#pragma once
#include "AudioOut.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace arstro
{
namespace solaris_cli
{
    /** What the engine played, for the pages that listen: interleaved int16, its format, its generation. */
    class WebAudioHub
    {
    public:
        struct Chunk
        {
            std::vector<int16_t> pcm;   // interleaved
            int rate = 48000, channels = 2;
            long long gen = 0;
            int frames() const { return channels > 0 ? (int)(pcm.size() / (size_t)channels) : 0; }
        };

        /** A device opened at this format: what was queued belongs to the previous stream. */
        void format(int rate, int channels)
        {
            std::lock_guard<std::mutex> l(mMutex);
            mRate = rate;
            mChannels = std::max(1, channels);
            mPcm.clear();
            ++mGen;
        }
        void push(const float *in, int frames)
        {
            std::lock_guard<std::mutex> l(mMutex);
            const size_t n = (size_t)frames * (size_t)mChannels, at = mPcm.size();
            if (at + n > kMaxQueued) return;   // nobody drained it for seconds: drop, never grow without bound
            mPcm.resize(at + n);
            for (size_t i = 0; i < n; ++i)
            {
                const float v = std::clamp(in[i], -1.0f, 1.0f);
                mPcm[at + i] = (int16_t)std::lrint(v * 32767.0f);
            }
        }
        /** A stop or a seek: drop what is queued; the pages drop what they scheduled from the old generation. */
        void flush()
        {
            std::lock_guard<std::mutex> l(mMutex);
            mPcm.clear();
            ++mGen;
        }
        /** Take what is queued when it is at least `minFrames` (or `any` and non-empty). Adapter thread. */
        bool take(Chunk &out, int minFrames, bool any)
        {
            std::lock_guard<std::mutex> l(mMutex);
            const int frames = mChannels > 0 ? (int)(mPcm.size() / (size_t)mChannels) : 0;
            if (frames <= 0 || (frames < minFrames && !any)) return false;
            out.pcm.clear();
            out.pcm.swap(mPcm);
            out.rate = mRate;
            out.channels = mChannels;
            out.gen = mGen;
            return true;
        }
        void setListening(bool on) { mListening = on; }
        bool listening() const { return mListening; }
        int rate() const { std::lock_guard<std::mutex> l(mMutex); return mRate; }
        int channels() const { std::lock_guard<std::mutex> l(mMutex); return mChannels; }

        static constexpr size_t kMaxQueued = 48000 * 2 * 4;   // 4 s of 48 kHz stereo

    private:
        mutable std::mutex mMutex;
        std::vector<int16_t> mPcm;
        int mRate = 48000, mChannels = 2;
        long long mGen = 0;
        std::atomic<bool> mListening{false};
    };

    /** The "web" driver: a device paced by the steady clock, playing into a WebAudioHub. */
    class WebAudioOut : public solaris::IAudioOut
    {
    public:
        /** What the page holds before it plays (its jitter buffer, web/js/model/audio.js kTargetS). */
        static constexpr double kPageLatencyS = 0.12;
        /** At least this far ahead the engine may render (a device's buffer; PulseAudio's is ~20 ms and up). */
        static constexpr double kMinAheadS = 0.02;

        explicit WebAudioOut(std::shared_ptr<WebAudioHub> hub) : mHub(std::move(hub)) {}

        bool open(const std::string &, int rate, int channels, int bufferFrames, std::string &err) override
        {
            if (!mHub) { err = "no page to play into"; return false; }
            if (rate <= 0 || channels <= 0) { err = "a web stream needs a rate and channels"; return false; }
            mRate = rate;
            mChannels = channels;
            mAheadFrames = std::max<long long>(bufferFrames, (long long)std::lround(rate * kMinAheadS));
            mHub->format(rate, channels);
            restart();
            return true;
        }
        bool write(const float *interleaved, int frames) override
        {
            if (frames <= 0) return true;
            if (mHub->listening()) mHub->push(interleaved, frames);
            mWritten += frames;
            // the back-pressure a device gives: never more than a buffer ahead of real time
            for (;;)
            {
                const long long ahead = mWritten - elapsedFrames();
                if (ahead <= mAheadFrames) break;
                const double s = (double)(ahead - mAheadFrames) / mRate;
                std::this_thread::sleep_for(std::chrono::microseconds((long long)std::max(500.0, s * 1e6)));
            }
            // a stall (the engine could not keep up): restart the clock rather than race to catch up
            if (elapsedFrames() - mWritten > mRate / 10) restart(mWritten);
            return true;
        }
        double latency() override
        {
            const long long ahead = std::max<long long>(0, mWritten - elapsedFrames());
            return (double)ahead / mRate + kPageLatencyS;
        }
        void flush() override
        {
            mHub->flush();
            restart();
        }

    private:
        using Clock = std::chrono::steady_clock;
        long long elapsedFrames() const
        {
            return (long long)(std::chrono::duration<double>(Clock::now() - mStart).count() * mRate) + mStartFrames;
        }
        void restart(long long at = 0)
        {
            mStart = Clock::now();
            mStartFrames = at;
            if (at == 0) mWritten = 0;
        }

        std::shared_ptr<WebAudioHub> mHub;
        int mRate = 48000, mChannels = 2;
        long long mAheadFrames = 960, mWritten = 0, mStartFrames = 0;
        Clock::time_point mStart = Clock::now();
    };
}
}
