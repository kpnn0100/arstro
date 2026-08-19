/*
 *  Arstrobench by arstro — runs the two workloads on a worker thread (R-G-4).
 *
 *  The render loop must keep animating at frame rate while a run is in flight, so the
 *  measurement never happens on the UI thread. The UI polls: a cheap atomic for the
 *  stage (read every frame) and a mutex-guarded snapshot for the results (read only
 *  when the stage changed).
 *
 *  UI-free: no Artboard, no GTK.
 */
#pragma once
#include "DspWorkload.h"
#include "ImageWorkload.h"
#include "Workload.h"
#include <atomic>
#include <mutex>
#include <thread>

namespace arstro
{
namespace arstrobench
{
    class BenchmarkRunner
    {
    public:
        enum class Stage { Idle, Image, Dsp, Done };

        /** What the UI reads: both results plus the headline total (R-SCORE-5). */
        struct Snapshot
        {
            WorkloadResult image;
            WorkloadResult dsp;
            double total = 0.0;
        };

        BenchmarkRunner() = default;
        ~BenchmarkRunner();
        BenchmarkRunner(const BenchmarkRunner &) = delete;
        BenchmarkRunner &operator=(const BenchmarkRunner &) = delete;

        /** Begin a run on a worker thread. Ignored while one is already in flight. */
        void start();
        /** True from start() until the worker has published its last result. */
        bool running() const { return mStage.load() == Stage::Image || mStage.load() == Stage::Dsp; }
        Stage stage() const { return mStage.load(); }
        /** A consistent copy of whatever has been measured so far. Never blocks the
         *  worker for longer than the copy itself. */
        Snapshot snapshot() const;

        /** Injection seam for the tests (R-TEST-1): smaller workloads so the suite is
         *  fast. Ignored while a run is in flight. */
        void setWorkloads(const ImageWorkload &image, const DspWorkload &dsp);

    private:
        void joinWorker();

        ImageWorkload mImageWorkload;
        DspWorkload mDspWorkload;
        std::atomic<Stage> mStage{Stage::Idle};
        mutable std::mutex mMutex;
        Snapshot mSnapshot;
        std::thread mThread;
    };
}
}
