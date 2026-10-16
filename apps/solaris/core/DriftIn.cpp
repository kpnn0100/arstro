#include "DriftIn.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace arstro
{
namespace solaris
{
    namespace
    {
        FillParams paramsOf(int rate, double target)
        {
            FillParams p;
            p.target = target;
            p.rate = rate;
            return p;
        }
    }

    long long DriftIn::steadyNs()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    DriftIn::DriftIn(std::unique_ptr<IAudioIn> in, const std::string &device, int rate, int channels, int block, double target, double startPpm)
        : mIn(std::move(in)), mDevice(device), mRate(std::max(1, rate)), mChannels(std::max(1, channels)), mBlock(std::max(1, block)),
          mRs(std::max(1, channels), std::max(1, block), 1.01), mCtl(paramsOf(rate, target))
    {
        mTmp.assign((size_t)((2 * mBlock + 64) * mChannels), 0.0f);
        mCtl.reset(target, -startPpm); // (F5) the controller's consumer is the clock: the input's drift the other way
        mDrift.store(startPpm);
        mLatency.store(mIn->latency());
    }

    void DriftIn::discard(long long frames)
    {
        const int chunk = (int)(mTmp.size() / (size_t)mChannels);
        while (frames > 0)
        {
            const int n = (int)std::min<long long>(chunk, frames);
            mIn->read(mTmp.data(), n);
            frames -= n;
        }
    }

    int DriftIn::read(float *x, int n)
    {
        n = std::min(std::max(0, n), mBlock);
        long long frames = 0, ns = 0;
        int last = 0;
        if (!mIn->held(frames, ns, last))
        {
            // nothing has landed yet, or a capture that cannot say what it holds: read as it is
            mPrimed = false;
            const int got = mIn->read(x, n);
            mLatency.store(mIn->latency(), std::memory_order_relaxed);
            return got;
        }
        // its device's own latency: what it reports less its ring, read beside what the ring holds
        const double device = std::max(0.0, mIn->latency() - (double)frames / mRate);
        const double since = std::min((double)last, std::max(0.0, (double)(now() - ns) * 1e-9 * mRate));
        double fill = (double)(frames - last) + since + mRs.buffered();
        auto silent = [&] {
            std::fill(x, x + (size_t)n * mChannels, 0.0f);
            return 0;
        };
        if (!mPrimed)
        {
            if (fill < mCtl.target()) return silent(); // what lands waits in the capture's ring until there is enough
            // from exactly the target: what came past it while it waited skipped, to the nearest frame
            const long long skip = std::min(frames, (long long)std::llround(fill - mCtl.target()));
            discard(skip);
            frames -= skip;
            fill -= (double)skip;
            mPrimed = true;
            mCtl.reset(fill, mCtl.driftPpm());
        }
        else if (fill > mCtl.target() + 8.0 * mBlock)
        {
            // far ahead — the clock stalled while its device went on: emptied and primed again
            discard(frames);
            mPrimed = false;
            return silent();
        }
        const double step = 1.0 + mCtl.update(fill, n);
        const int need = mRs.inputNeeded(n, step);
        if (frames < need || (size_t)need * mChannels > mTmp.size())
        {
            // its device fell behind: silence, primed again
            mStarved.fetch_add(1, std::memory_order_relaxed);
            mStarvedFrames.fetch_add(n, std::memory_order_relaxed);
            mPrimed = false;
            return silent();
        }
        mIn->read(mTmp.data(), need);
        mRs.process(mTmp.data(), need, x, n, step);
        mDrift.store(-mCtl.driftPpm(), std::memory_order_relaxed);
        // its device's latency, plus the target: a sound reaching it is read this much later
        mLatency.store(device + mCtl.target() / mRate, std::memory_order_relaxed);
        return n;
    }

    DriftIn::State DriftIn::state() const
    {
        State s;
        s.device = mDevice;
        s.latency = mLatency.load(std::memory_order_relaxed);
        s.driftPpm = mDrift.load(std::memory_order_relaxed);
        s.starved = mStarved.load(std::memory_order_relaxed);
        return s;
    }
}
}
