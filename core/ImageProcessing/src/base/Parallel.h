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
#include <cstdint>
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

        /**
         *  ── Why this is a LIST of batches and not one batch ──
         *
         *  `parallelFor` is called from several threads at once, and that is not incidental:
         *  the render worker runs the pipeline while the export path renders full-resolution
         *  frames and a project load converts and downscales freshly decoded images, and
         *  every one of those paths goes through `parallelFor`. There are 19 call sites.
         *
         *  The old implementation was safe against that by accident — it spawned its own
         *  threads per call, so callers could not interfere. The first pooled version was
         *  not: one shared batch descriptor, so two concurrent callers clobbered each
         *  other's body pointer, chunk count and claim index. That segfaulted about one
         *  `ctest` run in three (D-47), and a generation-tagged claim index — which fixes the
         *  *sequential* hazard of a worker waking after its batch ended — does nothing for it.
         *
         *  So each `run()` owns its batch **on its own stack** and publishes a pointer to it.
         *  Workers pick up any live batch that still has chunks. Lifetime is the whole trick,
         *  and it is one invariant: a worker may only touch a batch while it is counted in
         *  `active`, `active` is only incremented under the lock and only for a batch that is
         *  still in `mLive`, and `run()` removes its batch from `mLive` and then waits for
         *  `active == 0` before returning. So no worker can enter a batch after it is
         *  unpublished, and no batch can die with a worker inside it.
         */
        struct Batch
        {
            const std::function<void(int, int)> *body = nullptr;
            int count = 0;
            int chunks = 0;
            std::atomic<int> next{0};
            std::atomic<int> remaining{0};
            int active = 0;   // workers currently inside; guarded by Pool::mMu
        };

        class Pool
        {
        public:
            ~Pool() { stop(); }

            void resize(int n)
            {
                if (n < 1) n = 1;
                {
                    std::lock_guard<std::mutex> lk(mMu);
                    if ((int)mWorkers.size() == n - 1) return;
                    // Never restart the pool while a batch is in flight: a worker joined
                    // mid-batch would leave `remaining` short and the caller spinning forever.
                    if (!mLive.empty()) { mWantWorkers = n - 1; return; }
                }
                restart(n - 1);
            }

            /** Run `body(begin,end)` over `chunks` chunks covering [0,count). Blocks until
             *  every chunk has been run. The caller participates, so it is never idle. */
            void run(const std::function<void(int, int)> &body, int count, int chunks)
            {
                Batch b;
                b.body = &body;
                b.count = count;
                b.chunks = chunks;
                b.remaining.store(chunks, std::memory_order_relaxed);
                int pendingResize = 0;
                {
                    std::lock_guard<std::mutex> lk(mMu);
                    mLive.push_back(&b);
                    ++mSeq;
                }
                mCv.notify_all();
                claim(&b);                        // the caller is a worker too
                // Chunks are short, so a spin-then-yield beats a second condition variable
                // for the work itself.
                while (b.remaining.load(std::memory_order_acquire) > 0)
                    std::this_thread::yield();
                {
                    std::unique_lock<std::mutex> lk(mMu);
                    for (std::size_t i = 0; i < mLive.size(); ++i)
                        if (mLive[i] == &b) { mLive.erase(mLive.begin() + i); break; }
                    // Unpublished, so nobody new can enter; wait out whoever is still inside.
                    mIdleCv.wait(lk, [&b] { return b.active == 0; });
                    if (mWantWorkers >= 0 && mLive.empty())
                    { pendingResize = mWantWorkers + 1; mWantWorkers = -1; }
                }
                if (pendingResize > 0) restart(pendingResize - 1);
            }

            void stop()
            {
                {
                    std::lock_guard<std::mutex> lk(mMu);
                    mStop = true;
                }
                mCv.notify_all();
                for (auto &t : mWorkers)
                    if (t.joinable()) t.join();
                mWorkers.clear();
            }

        private:
            void restart(int workers)
            {
                stop();
                std::lock_guard<std::mutex> lk(mMu);
                mStop = false;
                for (int i = 0; i < workers; ++i)
                    mWorkers.emplace_back([this] { workerLoop(); });
            }

            /** Run chunks of `b` until it is exhausted. Caller must have ensured `b` stays
             *  alive for the duration — the owner by construction, a worker via `active`. */
            void claim(Batch *b)
            {
                if (!b->body || b->chunks <= 0) return;
                const bool wasInside = inParallel();
                inParallel() = true;
                for (;;)
                {
                    const int idx = b->next.fetch_add(1, std::memory_order_relaxed);
                    if (idx >= b->chunks) break;
                    // Boundaries from the chunk INDEX, so the split is identical however the
                    // chunks are distributed — the byte-identical serial-vs-parallel guarantee
                    // in the header rests on the SPLIT being deterministic, not the schedule.
                    const long long lo = (long long)idx * b->count / b->chunks;
                    const long long hi = (long long)(idx + 1) * b->count / b->chunks;
                    if (hi > lo) (*b->body)((int)lo, (int)hi);
                    b->remaining.fetch_sub(1, std::memory_order_release);
                }
                inParallel() = wasInside;
            }

            void workerLoop()
            {
                unsigned long long seen = 0;
                for (;;)
                {
                    Batch *mine = nullptr;
                    {
                        std::unique_lock<std::mutex> lk(mMu);
                        mCv.wait(lk, [&] { return mStop || mSeq != seen; });
                        if (mStop) return;
                        seen = mSeq;
                        for (Batch *b : mLive)
                            if (b->next.load(std::memory_order_relaxed) < b->chunks)
                            { mine = b; ++b->active; break; }
                    }
                    while (mine)
                    {
                        claim(mine);
                        std::unique_lock<std::mutex> lk(mMu);
                        --mine->active;
                        if (mine->active == 0) { lk.unlock(); mIdleCv.notify_all(); lk.lock(); }
                        // Another caller's batch may still have chunks; take it rather than
                        // going back to sleep, or a concurrent load would run alone.
                        mine = nullptr;
                        seen = mSeq;
                        for (Batch *b : mLive)
                            if (b->next.load(std::memory_order_relaxed) < b->chunks)
                            { mine = b; ++b->active; break; }
                    }
                }
            }

            std::mutex mMu;
            std::condition_variable mCv;      // "there is a new batch"
            std::condition_variable mIdleCv;  // "a worker left a batch"
            std::vector<std::thread> mWorkers;
            std::vector<Batch *> mLive;       // published batches, guarded by mMu
            bool mStop = false;
            unsigned long long mSeq = 0;
            int mWantWorkers = -1;            // deferred resize, applied when nothing is live
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
