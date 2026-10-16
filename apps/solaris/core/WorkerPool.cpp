#include "WorkerPool.h"
#include <algorithm>
#include <cstdlib>
#include <string>
#if defined(__SSE__) || defined(__x86_64__)
#include <xmmintrin.h>
#define SOLARIS_CSR 1
#endif

namespace arstro
{
namespace solaris
{
    namespace
    {
        unsigned readCsr()
        {
#ifdef SOLARIS_CSR
            return _mm_getcsr();
#else
            return 0;
#endif
        }
        void writeCsr(unsigned v)
        {
#ifdef SOLARIS_CSR
            _mm_setcsr(v);
#else
            (void)v;
#endif
        }
    }

    int WorkerPool::threadsFor(const std::string &setting)
    {
        if (setting == "auto" || setting.empty())
        {
            const int cores = (int)std::thread::hardware_concurrency();
            return std::clamp(cores - 1, 1, 8);
        }
        return std::clamp(std::atoi(setting.c_str()), 1, 16);
    }

    WorkerPool::WorkerPool(int threads)
    {
        for (int i = 1; i < std::max(1, threads); ++i) mWorkers.emplace_back([this] { loop(); });
    }

    WorkerPool::~WorkerPool()
    {
        {
            std::lock_guard<std::mutex> l(mMx);
            mQuit = true;
        }
        mCv.notify_all();
        for (auto &w : mWorkers) w.join();
    }

    void WorkerPool::work(Job &j)
    {
        for (;;)
        {
            const int i = j.next.fetch_add(1, std::memory_order_relaxed);
            if (i >= j.count) return;
            j.fn(j.ctx, i);
            j.done.fetch_add(1, std::memory_order_release);
        }
    }

    void WorkerPool::loop()
    {
        unsigned long long seen = 0;
        std::unique_lock<std::mutex> l(mMx);
        for (;;)
        {
            mCv.wait(l, [&] { return mQuit || mGen.load(std::memory_order_acquire) != seen; });
            if (mQuit) return;
            seen = mGen.load(std::memory_order_acquire);
            l.unlock();
            mInside.fetch_add(1, std::memory_order_seq_cst); // before the job is read: the caller sees us
            if (Job *j = mJob.load(std::memory_order_acquire))
            {
                writeCsr(j->csr);
                work(*j);
            }
            mInside.fetch_sub(1, std::memory_order_release);
            l.lock();
        }
    }

    void WorkerPool::run(int count, void (*fn)(void *, int), void *ctx)
    {
        // alone: no worker, one item, or a worker still leaving an old job (never wait to START)
        if (mWorkers.empty() || count < 2 || mInside.load(std::memory_order_seq_cst) != 0)
        {
            for (int i = 0; i < count; ++i) fn(ctx, i);
            return;
        }
        // the buffer no worker can be reading (the published one is the other), filled, then published
        Job &j = mJobs[(size_t)(mFlip ^= 1)];
        j.fn = fn;
        j.ctx = ctx;
        j.count = count;
        j.csr = readCsr();
        j.next.store(0, std::memory_order_relaxed);
        j.done.store(0, std::memory_order_relaxed);
        mJob.store(&j, std::memory_order_release);
        mGen.fetch_add(1, std::memory_order_release);
        mCv.notify_all(); // without the mutex: a worker that misses it costs this piece its help, nothing else
        work(j);
        while (j.done.load(std::memory_order_acquire) < count) std::this_thread::yield();
    }
}
}
