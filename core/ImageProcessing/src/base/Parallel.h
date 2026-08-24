/*
 *  Arstro ImageProcessing Library
 *
 *  Parallel: data-parallel hardware acceleration for the pixel pipeline. par::parallelFor
 *  splits a row range across CPU cores so per-pixel/per-row work (the engine's hot loops)
 *  uses all available hardware. Pure compute, no UI. Enabled when ARSTRO_ENABLE_THREADS is
 *  defined (native, with -pthread); otherwise it runs serially (e.g. the single-threaded
 *  web build). Thread count is settable (0 = auto = hardware_concurrency).
 *
 *  Correctness contract: the callback for a chunk [begin,end) must touch only its own rows
 *  of the OUTPUT and read-only shared state — every parallelized loop here is row-
 *  independent, so serial and parallel results are byte-identical.
 *
 *  ── Why there is a pool, and why chunks are small (R-PREVIEW-2, T1) ──
 *
 *  This used to spawn fresh std::threads on every call and hand each one an EQUAL slice of
 *  the range. Both halves of that were wrong for the hardware cosmo is being ported to:
 *
 *    * a preview render makes ~13 parallelFor calls, so a render paid ~13 x (threads-1)
 *      thread creations and joins — measurable even on a desktop, and far worse on a small
 *      board where clone() is slower and the scheduler has fewer cores to place them on;
 *    * equal slices assume equal cores. On a big.LITTLE SoC (RK3588: 4xA76 + 4xA55; the
 *      Allwinner A733 class: a couple of A76-class cores plus little ones) an A55 chunk
 *      takes 2-3x as long as an A76 chunk of the same size, and parallelFor BLOCKS until
 *      every chunk is done — so every parallel pass in the library ran at little-core
 *      speed. That is a standing 2-3x loss on exactly the machines that can least afford
 *      it, and no amount of arithmetic tuning elsewhere can recover it.
 *
 *  So: one persistent pool of worker threads, and the range is cut into MANY MORE chunks
 *  than there are threads, claimed by an atomic counter. A fast core simply claims more
 *  chunks than a slow one, which is work-stealing in its simplest correct form and needs
 *  no per-core knowledge, no affinity, and no configuration. The calling thread claims
 *  chunks too, so it is never idle waiting on workers.
 *
 *  The pool is sized on demand and resized when setThreads() changes the count. It is
 *  intentionally NOT reentrant: a parallelFor called from inside a parallelFor runs its
 *  body serially on the calling thread rather than deadlocking against a pool that is
 *  already fully claimed. The library has no nested parallel loops today, and this makes
 *  adding one a performance question rather than a hang.
 */
#pragma once
#include <algorithm>
#ifdef ARSTRO_ENABLE_THREADS
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#endif

namespace arstro
{
namespace par
{
    inline int &threadsRef() { static int n = 0; return n; }  // 0 = auto

    inline int threads()
    {
        const int n = threadsRef();
        if (n > 0) return n;
#ifdef ARSTRO_ENABLE_THREADS
        const unsigned hc = std::thread::hardware_concurrency();
        return hc ? (int)hc : 4;
#else
        return 1;
#endif
    }

#ifdef ARSTRO_ENABLE_THREADS
    namespace detail
    {
        /** How many chunks to cut the range into, per thread. Bigger = better balance on
         *  asymmetric cores, but more atomic claims and shorter runs of sequential memory
         *  access. Eight is enough to absorb a 3x core-speed spread (the slowest core ends
         *  up holding at most one chunk that nobody else could take) while keeping a chunk
         *  of a 1600 px preview at ~16 rows, which is still a long sequential stream. */
        constexpr int kChunksPerThread = 8;

        /** True while this thread is already inside a parallelFor body. */
        inline bool &inParallel()
        {
            static thread_local bool v = false;
            return v;
        }

        class Pool
        {
        public:
            ~Pool() { stop(); }

            /** Grow or shrink to `n` workers (n counts the CALLING thread, so n-1 are
             *  spawned). Called only from run(), i.e. from one thread at a time in
             *  practice; the mutex keeps it honest anyway. */
            void resize(int n)
            {
                if (n < 1) n = 1;
                std::unique_lock<std::mutex> lk(mMu);
                if ((int)mWorkers.size() == n - 1) return;
                // Shrinking or growing both restart the pool: it happens on a settings
                // change, not per frame, so simplicity beats cleverness here.
                lk.unlock();
                stop();
                lk.lock();
                mStop = false;
                mGeneration = 0;
                for (int i = 0; i < n - 1; ++i)
                    mWorkers.emplace_back([this] { workerLoop(); });
            }

            /** Run `body(begin,end)` over `chunks` chunks covering [0,count). Blocks until
             *  every chunk is done. The caller participates. */
            void run(const std::function<void(int, int)> &body, int count, int chunks)
            {
                {
                    std::lock_guard<std::mutex> lk(mMu);
                    mBody = &body;
                    mCount = count;
                    mChunks = chunks;
                    mNext.store(0, std::memory_order_relaxed);
                    mRemaining.store(chunks, std::memory_order_relaxed);
                    ++mGeneration;
                }
                mCv.notify_all();
                claimUntilDone();                  // the caller is a worker too
                // Wait for any chunk still in flight on a worker. Chunks are short, so a
                // spin-then-yield beats a second condition variable here.
                while (mRemaining.load(std::memory_order_acquire) > 0)
                    std::this_thread::yield();
                std::lock_guard<std::mutex> lk(mMu);
                mBody = nullptr;
            }

            void stop()
            {
                {
                    std::lock_guard<std::mutex> lk(mMu);
                    if (mWorkers.empty()) { mStop = true; return; }
                    mStop = true;
                }
                mCv.notify_all();
                for (auto &t : mWorkers)
                    if (t.joinable()) t.join();
                mWorkers.clear();
            }

        private:
            /** Claim and run chunks until the current batch is exhausted. */
            void claimUntilDone()
            {
                const int count = mCount, chunks = mChunks;
                const std::function<void(int, int)> *body = mBody;
                if (!body || chunks <= 0) return;
                inParallel() = true;
                for (;;)
                {
                    const int k = mNext.fetch_add(1, std::memory_order_relaxed);
                    if (k >= chunks) break;
                    // Chunk boundaries from the index, so they are identical however the
                    // chunks are distributed — the byte-identical guarantee in the contract
                    // above depends on the SPLIT being deterministic, not the schedule.
                    const long long b = (long long)k * count / chunks;
                    const long long e = (long long)(k + 1) * count / chunks;
                    if (e > b) (*body)((int)b, (int)e);
                    mRemaining.fetch_sub(1, std::memory_order_release);
                }
                inParallel() = false;
            }

            void workerLoop()
            {
                unsigned long long seen = 0;
                for (;;)
                {
                    {
                        std::unique_lock<std::mutex> lk(mMu);
                        mCv.wait(lk, [&] { return mStop || mGeneration != seen; });
                        if (mStop) return;
                        seen = mGeneration;
                    }
                    claimUntilDone();
                }
            }

            std::mutex mMu;
            std::condition_variable mCv;
            std::vector<std::thread> mWorkers;
            bool mStop = false;
            unsigned long long mGeneration = 0;
            const std::function<void(int, int)> *mBody = nullptr;
            int mCount = 0;
            int mChunks = 0;
            std::atomic<int> mNext{0};
            std::atomic<int> mRemaining{0};
        };

        inline Pool &pool()
        {
            static Pool p;
            return p;
        }
    }
#endif

    inline void setThreads(int n)
    {
        threadsRef() = n < 0 ? 0 : n;
#ifdef ARSTRO_ENABLE_THREADS
        // Resize eagerly rather than on the next parallelFor: a settings change is a rare
        // event on a thread that is allowed to block, whereas the next parallelFor is on
        // the render worker with a frame to produce.
        detail::pool().resize(threads());
#endif
    }

    /** Run fn(begin,end) over contiguous chunks covering [0,count); blocks until done. */
    template <class Fn>
    inline void parallelFor(int count, Fn fn)
    {
        if (count <= 0) return;
#ifdef ARSTRO_ENABLE_THREADS
        const int t = threads();
        // Serial when there is one thread, when the range is too small to be worth
        // splitting, or when we are ALREADY inside a parallelFor (see the header: nested
        // parallelism runs serially rather than deadlocking on an exhausted pool).
        if (t <= 1 || count < 2 * t || detail::inParallel()) { fn(0, count); return; }
        detail::Pool &p = detail::pool();
        p.resize(t);
        // More chunks than threads, so a fast core takes more of them than a slow one.
        // Never more chunks than rows, or empty chunks would spin the atomic for nothing.
        int chunks = t * detail::kChunksPerThread;
        if (chunks > count) chunks = count;
        const std::function<void(int, int)> body = [&fn](int b, int e) { fn(b, e); };
        p.run(body, count, chunks);
#else
        fn(0, count);
#endif
    }
}
}
