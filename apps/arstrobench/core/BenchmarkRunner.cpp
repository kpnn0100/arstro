#include "BenchmarkRunner.h"

namespace arstro
{
namespace arstrobench
{
    BenchmarkRunner::~BenchmarkRunner() { joinWorker(); }

    void BenchmarkRunner::joinWorker()
    {
        if (mThread.joinable()) mThread.join();
    }

    void BenchmarkRunner::setWorkloads(const ImageWorkload &image, const DspWorkload &dsp)
    {
        if (running()) return;
        joinWorker();
        mImageWorkload = image;
        mDspWorkload = dsp;
    }

    void BenchmarkRunner::start()
    {
        if (running()) return;
        joinWorker();  // reap the previous run's thread before starting another

        {   // A re-run starts from a clean slate: the previous scores are gone, not stale.
            std::lock_guard<std::mutex> lock(mMutex);
            mSnapshot = Snapshot{};
        }
        mStage.store(Stage::Image);

        mThread = std::thread([this] {
            const WorkloadResult image = mImageWorkload.run();
            {
                std::lock_guard<std::mutex> lock(mMutex);
                mSnapshot.image = image;
                mSnapshot.total = image.score;
            }
            mStage.store(Stage::Dsp);

            const WorkloadResult dsp = mDspWorkload.run();
            {
                std::lock_guard<std::mutex> lock(mMutex);
                mSnapshot.dsp = dsp;
                mSnapshot.total = image.score + dsp.score;
            }
            mStage.store(Stage::Done);
        });
    }

    BenchmarkRunner::Snapshot BenchmarkRunner::snapshot() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mSnapshot;
    }
}
}
