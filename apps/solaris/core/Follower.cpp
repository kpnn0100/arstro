#include "Follower.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace arstro
{
namespace solaris
{
    namespace
    {
        FillParams paramsOf(const Follower::Config &c)
        {
            FillParams p;
            p.target = c.target;
            p.rate = c.rate;
            return p;
        }
    }

    long long Follower::steadyNs()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    Follower::Follower(std::unique_ptr<IAudioOut> out, const Config &c)
        : mCfg(c), mOut(std::move(out)), mRs(std::max(1, c.channels), std::max(1, c.block), 1.01), mCtl(paramsOf(c))
    {
        mCfg.channels = std::max(1, mCfg.channels);
        mCfg.block = std::max(1, mCfg.block);
        // four targets and a few blocks: room for a target moved later and a clock that pushes in bursts
        const long long want = (long long)(4.0 * std::max(1.0, mCfg.target)) + 8LL * mCfg.block;
        mCap = 1;
        while (mCap < want) mCap <<= 1;
        mRing.assign((size_t)(mCap * mCfg.channels), 0.0f);
        mIn.assign((size_t)((2 * mCfg.block + 64) * mCfg.channels), 0.0f);
        mOutBuf.assign((size_t)(mCfg.block * mCfg.channels), 0.0f);
        mCtl.reset(mCfg.target, mCfg.startPpm);
        mDrift.store(mCfg.startPpm);
        mTargetNow.store(mCfg.target);
        mDevLatency.store(mOut->latency());
    }

    Follower::~Follower() { stop(); }

    void Follower::start()
    {
        if (mRun.exchange(true)) return;
        mThread = std::thread([this] { run(); });
    }

    void Follower::stop()
    {
        if (!mRun.exchange(false)) return;
        if (mThread.joinable()) mThread.join();
        if (mOut) mOut->flush();
    }

    void Follower::push(const float *x, int frames)
    {
        if (frames <= 0) return;
        const long long head = mHead.load(std::memory_order_relaxed), tail = mTail.load(std::memory_order_acquire);
        if (head - tail + frames > mCap) // its device stalled: the block is lost, and counted
        {
            mDropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        const int C = mCfg.channels;
        for (int i = 0; i < frames; ++i)
            std::memcpy(&mRing[(size_t)(((head + i) & (mCap - 1)) * C)], &x[(size_t)i * C], sizeof(float) * (size_t)C);
        // the push and its time stamp, seqlocked: the consumer reads the three together or not at all
        const long long ns = now();
        mSeq.fetch_add(1, std::memory_order_acq_rel);
        mPushEnd.store(head + frames, std::memory_order_relaxed);
        mPushFrames.store(frames, std::memory_order_relaxed);
        mPushNs.store(ns, std::memory_order_relaxed);
        mSeq.fetch_add(1, std::memory_order_release);
        mHead.store(head + frames, std::memory_order_release);
    }

    void Follower::pullOnce()
    {
        const int N = mCfg.block, C = mCfg.channels;
        if (const double t = mRetarget.exchange(-1.0, std::memory_order_relaxed); t >= 0.0) mCtl.setTarget(t);
        // the last push, read whole
        long long end = 0, frames = 0, ns = 0;
        for (;;)
        {
            const unsigned a = mSeq.load(std::memory_order_acquire);
            if (a & 1u) continue;
            end = mPushEnd.load(std::memory_order_relaxed);
            frames = mPushFrames.load(std::memory_order_relaxed);
            ns = mPushNs.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (mSeq.load(std::memory_order_relaxed) == a) break;
        }
        const long long head = mHead.load(std::memory_order_acquire);
        long long tail = mTail.load(std::memory_order_relaxed);
        // the fill, continuous: up to the last push's start, plus what the clock has made of it since — at the rate
        const double since = frames > 0 ? std::min((double)frames, std::max(0.0, (double)(now() - ns) * 1e-9 * mCfg.rate)) : 0.0;
        double fill = (double)(end - frames - tail) + since + mRs.buffered();
        if (!mPrimed)
        {
            // silence until the fill reaches its target — then it plays from exactly there: what came past it while it
            // waited (up to a block — it looks once a block) is skipped, never played late; held from there, its drift kept
            if (fill >= mCtl.target())
            {
                const long long skip = std::min(head - tail, (long long)std::llround(fill - mCtl.target())); // to the nearest frame
                tail += skip;
                fill -= (double)skip;
                mTail.store(tail, std::memory_order_release);
                mPrimed = true;
                mCtl.reset(fill, mCtl.driftPpm());
            }
            else
            {
                std::fill(mOutBuf.begin(), mOutBuf.end(), 0.0f);
                mOut->write(mOutBuf.data(), N);
                mDevLatency.store(mOut->latency(), std::memory_order_relaxed);
                return;
            }
        }
        if (fill > mCtl.target() + 8.0 * N)
        {
            // far ahead — its device stalled while the clock went on (the pushes it had no room for already broke the
            // sound): emptied and primed again, on time within a target's wait — not drained over minutes at the loop's
            // most, and not cut to the target now (the last push is stale: where the next lands in its block is unknown)
            mTail.store(head, std::memory_order_release);
            mPrimed = false;
            std::fill(mOutBuf.begin(), mOutBuf.end(), 0.0f);
            mOut->write(mOutBuf.data(), N);
            mDevLatency.store(mOut->latency(), std::memory_order_relaxed);
            return;
        }
        const long long have = head - tail;
        const double step = 1.0 + mCtl.update(fill, N);
        mTargetNow.store(mCtl.target(), std::memory_order_relaxed); // what `state` reads: the controller is this thread's
        const int need = mRs.inputNeeded(N, step);
        if (have < need || (size_t)need * C > mIn.size())
        {
            // the clock fell behind (a stall): silence, and primed again from here
            mStarved.fetch_add(1, std::memory_order_relaxed);
            mPrimed = false;
            std::fill(mOutBuf.begin(), mOutBuf.end(), 0.0f);
            mOut->write(mOutBuf.data(), N);
            mDevLatency.store(mOut->latency(), std::memory_order_relaxed);
            return;
        }
        for (int i = 0; i < need; ++i)
            std::memcpy(&mIn[(size_t)i * C], &mRing[(size_t)(((tail + i) & (mCap - 1)) * C)], sizeof(float) * (size_t)C);
        mTail.store(tail + need, std::memory_order_release);
        mRs.process(mIn.data(), need, mOutBuf.data(), N, step);
        mDrift.store(mCtl.driftPpm(), std::memory_order_relaxed);
        mFill.store(mCtl.smoothedFill(), std::memory_order_relaxed);
        mOut->write(mOutBuf.data(), N); // blocks: its device is its clock
        mDevLatency.store(mOut->latency(), std::memory_order_relaxed);
    }

    void Follower::run()
    {
        while (mRun.load(std::memory_order_relaxed)) pullOnce();
    }

    double Follower::pathLatency(double target) const
    {
        // the fill IS the delay from the clock's render to this read (it counts what the resampler holds, (A5))
        return mDevLatency.load(std::memory_order_relaxed) + target / std::max(1, mCfg.rate);
    }

    Follower::State Follower::state() const
    {
        State s;
        s.device = mCfg.device;
        s.latency = pathLatency(mTargetNow.load(std::memory_order_relaxed));
        s.driftPpm = mDrift.load(std::memory_order_relaxed);
        s.fill = mFill.load(std::memory_order_relaxed);
        s.xruns = mOut->xruns();
        s.starved = mStarved.load(std::memory_order_relaxed);
        s.dropped = mDropped.load(std::memory_order_relaxed);
        return s;
    }
}
}
