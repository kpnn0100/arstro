#include "ThreadBudget.h"
#include "base/Parallel.h"
#include <thread>

namespace arstro
{
namespace cosmo
{
    namespace
    {
        int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
    }

    ThreadBudget::ThreadBudget(int percent, int cores)
    {
        if (cores <= 0)
        {
            const unsigned hc = std::thread::hardware_concurrency();
            cores = hc ? (int)hc : 4;   // an unknowable core count is not a reason to run serial
        }
        mCores = cores;
        setPercent(percent);
    }

    void ThreadBudget::setPercent(int percent)
    {
        // Out of range is a corrupt setting, not a request for the whole machine — same
        // rule as AppSettings::load(), kept here so a budget built from anywhere agrees.
        mPercent = (percent < 1 || percent > 100) ? 50 : percent;
        apply();
    }

    void ThreadBudget::setExplicitEngineThreads(int n)
    {
        mExplicit = n < 0 ? 0 : n;
        apply();
    }

    int ThreadBudget::total() const
    {
        const int n = (mCores * mPercent + 50) / 100;   // round to nearest: 50% of 3 is 2, not 1
        return clampi(n, 1, mCores);                    // R-CPU-1: never zero, never more than the machine
    }

    int ThreadBudget::decodeWorkers() const
    {
        // What is left after the engine keeps its floor, capped where the cap binds first
        // (R-CPU-5). On a 1-thread budget the load still gets its one worker and the
        // engine still gets its one — the two floors are the only place the sum may
        // exceed the total, and only by a single thread on a machine that small.
        return clampi(total() - kEngineFloor, 1, kMaxDecodeWorkers);
    }

    int ThreadBudget::engineThreads() const
    {
        if (mExplicit > 0) return mExplicit;   // R-CPU-2b: a deliberate override outranks the budget
        return clampi(total() - mReserved.load(), 1, mCores);
    }

    int ThreadBudget::beginLoad()
    {
        const int w = decodeWorkers();
        mReserved.store(w);
        apply();          // the engine gives up what the load just took
        resetPeak();
        return w;
    }

    void ThreadBudget::endLoad()
    {
        mReserved.store(0);
        apply();          // idle again: the engine gets the whole budget back
    }

    void ThreadBudget::apply() const { par::setThreads(engineThreads()); }

    void ThreadBudget::producerEnter()
    {
        const int now = mActive.fetch_add(1) + 1;
        // Monotonic max without a lock: only ever raise the mark, and re-read on failure.
        int seen = mPeakDecode.load();
        while (now > seen && !mPeakDecode.compare_exchange_weak(seen, now)) {}
    }

    void ThreadBudget::producerExit() { mActive.fetch_sub(1); }
}
}
