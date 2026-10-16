#pragma once
/*
 *  solaris_core — WorkerPool: the engine's IParallel on threads (R-PERF-2). The caller (the audio thread, or a render's)
 *  takes items too; it never waits to START — a worker still leaving an old job and it works alone — and waits only
 *  for the items it handed out. No allocation and no lock on the caller's side: a job is two buffers published by an
 *  atomic pointer, items claimed by an atomic counter; workers sleep on a condition variable the caller notifies
 *  without its mutex (a worker that misses a wake-up costs that piece its help, never its correctness). Each worker
 *  takes the caller's floating-point mode, so a sum is the same bits on any thread.
 */
#include "Engine.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace arstro
{
namespace solaris
{
    class WorkerPool : public engine::IParallel
    {
    public:
        /** `threads` in all, the caller one of them (1: no worker — everything on the caller). */
        explicit WorkerPool(int threads);
        ~WorkerPool() override;
        void run(int count, void (*fn)(void *ctx, int i), void *ctx) override;
        int threads() const { return (int)mWorkers.size() + 1; }
        /** `auto` (the cores but one, at most 8) or a number 1 … 16 → how many threads. */
        static int threadsFor(const std::string &setting);

    private:
        struct Job
        {
            void (*fn)(void *, int) = nullptr;
            void *ctx = nullptr;
            int count = 0;
            unsigned csr = 0;
            std::atomic<int> next{0}, done{0};
        };
        void work(Job &j);
        void loop();

        std::vector<std::thread> mWorkers;
        std::array<Job, 2> mJobs;
        int mFlip = 0;
        std::atomic<Job *> mJob{nullptr};
        std::atomic<unsigned long long> mGen{0};
        std::atomic<int> mInside{0};
        std::mutex mMx;
        std::condition_variable mCv;
        bool mQuit = false;
    };
}
}
