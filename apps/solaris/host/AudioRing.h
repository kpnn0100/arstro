/*
 *  solaris_host — AudioRing: the frames between the player's thread and a sound server's realtime callback
 *  (R-DEV-9). One writer — the player: `write` waits for room, and that back-pressure is the clock, as Pulse's
 *  blocking write is — and one reader — the server's callback: `read` never waits, never locks, never
 *  allocates; what is missing is SILENCE and one xrun, counted once the writer has begun (R-DEV-5).
 *  Interleaved float; sized once by `reset`. Header-only: the host's tests drive it with no sound server.
 */
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

namespace arstro
{
namespace solaris_host
{
    class AudioRing
    {
    public:
        /** Sized for `capacityFrames` of `channels` (allocates; neither side running). */
        void reset(int channels, int capacityFrames)
        {
            mChannels = std::max(1, channels);
            mCap = (size_t)std::max(1, capacityFrames) + 1; // one slot empty: full and empty differ
            mData.assign(mCap * (size_t)mChannels, 0.0f);
            mHead.store(0);
            mTail.store(0);
            mXruns.store(0);
            mPrimed.store(false);
            mDrop.store(false);
        }
        /** Frames waiting to be heard. */
        int filled() const
        {
            const size_t h = mHead.load(std::memory_order_acquire), t = mTail.load(std::memory_order_acquire);
            return (int)((h + mCap - t) % mCap);
        }
        int capacity() const { return (int)mCap - 1; }
        long long xruns() const { return mXruns.load(std::memory_order_relaxed); }

        /** The writer: `frames` in, waiting (1 ms naps) for room; false when `alive` goes false while waiting. */
        bool write(const float *x, int frames, const std::atomic<bool> *alive = nullptr)
        {
            int done = 0;
            while (done < frames)
            {
                const int room = capacity() - filled();
                if (room <= 0)
                {
                    if (alive && !alive->load(std::memory_order_relaxed)) return false;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                const int n = std::min(room, frames - done);
                size_t h = mHead.load(std::memory_order_relaxed);
                for (int i = 0; i < n; ++i)
                {
                    std::memcpy(&mData[h * (size_t)mChannels], x + (size_t)(done + i) * (size_t)mChannels, sizeof(float) * (size_t)mChannels);
                    h = (h + 1) % mCap;
                }
                mHead.store(h, std::memory_order_release);
                mPrimed.store(true, std::memory_order_release);
                done += n;
            }
            return true;
        }
        /** The reader (realtime): `frames` out — what is there, then silence; a short read once primed is an xrun.
         *  Returns the frames that were there. */
        int read(float *out, int frames)
        {
            size_t t = mTail.load(std::memory_order_relaxed);
            if (mDrop.exchange(false, std::memory_order_acq_rel)) t = mHead.load(std::memory_order_acquire); // a flush
            const size_t h = mHead.load(std::memory_order_acquire);
            const int have = (int)((h + mCap - t) % mCap), n = std::min(have, frames);
            for (int i = 0; i < n; ++i)
            {
                std::memcpy(out + (size_t)i * (size_t)mChannels, &mData[t * (size_t)mChannels], sizeof(float) * (size_t)mChannels);
                t = (t + 1) % mCap;
            }
            if (n < frames)
            {
                std::memset(out + (size_t)n * (size_t)mChannels, 0, sizeof(float) * (size_t)(frames - n) * (size_t)mChannels);
                if (mPrimed.load(std::memory_order_acquire)) mXruns.fetch_add(1, std::memory_order_relaxed);
            }
            mTail.store(t, std::memory_order_release);
            return n;
        }
        /** The writer: drop what is queued (the reader takes it at its next read); no xrun until written again. */
        void clear()
        {
            mPrimed.store(false, std::memory_order_release);
            mDrop.store(true, std::memory_order_release);
        }

    private:
        int mChannels = 2;
        size_t mCap = 1;
        std::vector<float> mData;
        std::atomic<size_t> mHead{0}, mTail{0};
        std::atomic<long long> mXruns{0};
        std::atomic<bool> mPrimed{false}, mDrop{false};
    };
}
}
