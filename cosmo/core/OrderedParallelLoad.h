/*
 *  Arstro cosmo_core — OrderedParallelLoad: produce N items on a pool of threads,
 *  consume them strictly in index order, with bounded work in flight.
 *
 *  This is the shape a project load needs (R-LOADPERF-1/1a). Decoding is CPU-bound
 *  and independent per image, so it wants a pool; but a `.cosmoproj` identifies a
 *  node's parent by ENTRY INDEX, so results must still be applied in order or the
 *  group tree gets reparented. And a decoded frame is ~100 MB, so producers that run
 *  arbitrarily far ahead of the consumer would decode a whole catalog into RAM.
 *
 *  Deadlock-freedom (the part worth being careful about): workers claim indices with
 *  a monotonic counter, so if a worker holds index `i` then every index below `i` has
 *  already been claimed. The index the consumer needs next is therefore always either
 *  already produced or held by some worker — and the worker holding it is EXEMPT from
 *  both the window and the byte cap. Progress can never stall against the pipeline's
 *  own back-pressure, whatever the memory situation.
 *
 *  UI-free and GTK-free by design: it lives in cosmo_core so it is unit-testable
 *  without a display, and the host's loader is a thin binding over it.
 */
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace arstro
{
namespace cosmo
{
    template <class T>
    class OrderedParallelLoad
    {
    public:
        /** `produce(i)` runs on a worker and returns item i; `weigh(item)` reports the
         *  bytes it holds, so the in-flight cap can be expressed in memory rather than
         *  item count (items vary enormously — a group entry costs nothing, a 24 MP
         *  frame costs ~100 MB). */
        using Produce = std::function<T(std::size_t)>;
        using Weigh = std::function<std::size_t(const T &)>;

        OrderedParallelLoad() = default;
        OrderedParallelLoad(const OrderedParallelLoad &) = delete;
        OrderedParallelLoad &operator=(const OrderedParallelLoad &) = delete;
        ~OrderedParallelLoad() { stop(); }

        /** Start `workers` threads producing `count` items. Safe to call once. */
        void start(std::size_t count, int workers, std::size_t window,
                   std::size_t maxInFlightBytes, Produce produce, Weigh weigh)
        {
            mCount = count;
            mProduce = std::move(produce);
            mWeigh = std::move(weigh);
            mWindow = window < 1 ? 1 : window;
            mMaxBytes = maxInFlightBytes;
            mItems.resize(count);
            mDone.assign(count, 0);
            mWeights.assign(count, 0);
            if (count == 0) return;
            int n = workers < 1 ? 1 : workers;
            if ((std::size_t)n > count) n = (int)count;
            mWorkers.reserve((std::size_t)n);
            for (int k = 0; k < n; ++k) mWorkers.emplace_back([this] { run(); });
        }

        /** Take the next item IN ORDER if it is ready. Call from the one consumer. */
        bool tryConsume(T &out)
        {
            std::size_t freed = 0;
            {
                std::lock_guard<std::mutex> lk(mMu);
                if (mConsumed >= mCount || !mDone[mConsumed]) return false;
                out = std::move(mItems[mConsumed]);
                freed = mWeights[mConsumed];
                mInFlight -= freed < mInFlight ? freed : mInFlight;
                ++mConsumed;
            }
            mCv.notify_all();   // a slot (and its bytes) just freed up
            return true;
        }

        std::size_t consumed() const
        {
            std::lock_guard<std::mutex> lk(mMu);
            return mConsumed;
        }
        std::size_t count() const { return mCount; }
        bool finished() const { return consumed() >= mCount; }

        /** Ask the workers to bail out and join them. Idempotent. */
        void stop()
        {
            mStop.store(true);
            mCv.notify_all();
            for (auto &w : mWorkers)
                if (w.joinable()) w.join();
            mWorkers.clear();
        }

    private:
        void run()
        {
            for (;;)
            {
                const std::size_t i = mClaimed.fetch_add(1);
                if (i >= mCount || mStop.load()) return;
                {
                    std::unique_lock<std::mutex> lk(mMu);
                    mCv.wait(lk, [this, i] {
                        // `i == mConsumed` is the exemption that keeps this deadlock-free:
                        // the item the consumer needs next always gets produced.
                        return mStop.load() || i == mConsumed ||
                               (i < mConsumed + mWindow && mInFlight < mMaxBytes);
                    });
                    if (mStop.load()) return;
                }
                T item = mProduce(i);
                const std::size_t bytes = mWeigh ? mWeigh(item) : 0;
                {
                    std::lock_guard<std::mutex> lk(mMu);
                    mItems[i] = std::move(item);
                    mWeights[i] = bytes;
                    mDone[i] = 1;
                    mInFlight += bytes;
                }
            }
        }

        std::size_t mCount = 0;
        Produce mProduce;
        Weigh mWeigh;
        std::size_t mWindow = 1;
        std::size_t mMaxBytes = 0;

        mutable std::mutex mMu;
        std::condition_variable mCv;
        std::vector<T> mItems;
        std::vector<char> mDone;
        std::size_t mConsumed = 0;
        std::size_t mInFlight = 0;
        std::atomic<std::size_t> mClaimed{0};
        std::atomic<bool> mStop{false};
        std::vector<std::thread> mWorkers;
        // Per-index byte weight, so tryConsume subtracts exactly what the producer added.
        std::vector<std::size_t> mWeights;
    };
}
}
