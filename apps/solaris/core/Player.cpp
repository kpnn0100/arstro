#include "Player.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace solaris
{
    Player::Player() : mSlots(new LiveSlot[kLiveSlots]) {}

    Player::~Player()
    {
        stop();
        collect();
    }

    bool Player::start(std::unique_ptr<IAudioOut> out, std::unique_ptr<engine::Engine> eng, std::vector<Route> routes,
                       int channels, int block, long long from, int generation)
    {
        stop();
        mGeneration = generation;
        mOutFrames = 0;
        mSlotsWritten.store(0);
        mOut = std::move(out);
        mEng = eng.release();
        mRoutes = std::move(routes);
        mChannels = std::max(1, channels);
        mBlock = std::max(32, block);
        mRate = mEng->graph().sampleRate;
        // Everything the thread will touch is sized HERE (R-PLAY-2).
        mEng->prepare(mPb, mBlock);
        mInterleaved.assign((size_t)mBlock * (size_t)mChannels, 0.0f);
        mClick = DeviceRegistry::create("drums"); // the metronome's own kit, made here, not on the audio thread
        mClickL.assign((size_t)mBlock, 0.0);
        mClickR.assign((size_t)mBlock, 0.0);
        mClickWas = false;
        for (int i = 0; i < 2 * kMaxStrips + 2; ++i) mPeaks[(size_t)i].store(0.0f);
        mEng->seek(from);
        mGraphLatency.store(mEng->outputLatency());
        mRendered.store(from);
        mLatency.store(mOut->latency());
        mRun.store(true);
        mThread = std::thread([this] { run(); });
        return true;
    }

    void Player::stop()
    {
        if (mThread.joinable())
        {
            mRun.store(false);
            mThread.join();
        }
        if (mOut) mOut->flush();
        mOut.reset();
        // anything still queued (a swap the thread never took) is the service's to free
        Live m;
        while (mIn.pop(m))
            if (m.kind == Live::Swap) delete m.engine;
        delete mEng;
        mEng = nullptr;
    }

    long long Player::heard() const
    {
        const long long r = mRendered.load() - (long long)std::llround(mLatency.load() * mRate) - mGraphLatency.load();
        return std::max(0LL, r);
    }

    void Player::collect()
    {
        engine::Engine *e = nullptr;
        while (mRetired.pop(e)) delete e;
    }

    void Player::apply(const Live &m)
    {
        switch (m.kind)
        {
        case Live::StripGain: mEng->setStripGain(m.strip, m.value); break;
        case Live::StripPan: mEng->setStripPan(m.strip, m.value); break;
        case Live::StripSilent: mEng->setStripSilent(m.strip, m.value != 0); break;
        case Live::MasterGain: mEng->setMasterGain(m.value); break;
        case Live::DeviceBypass: mEng->setDeviceBypass(m.strip, m.device, m.value != 0); break;
        case Live::DeviceParam:
            if (m.strip < 0) mEng->setMasterDeviceParam(m.device, m.name, m.value);
            else mEng->setDeviceParam(m.strip, m.device, m.name, m.value);
            break;
        case Live::Seek: mEng->seek(m.sample); break;
        case Live::Swap:
        {
            // A structural edit: the new engine continues from here; the old one goes back to be freed.
            m.engine->seek(mEng->position());
            engine::Engine *old = mEng;
            mEng = m.engine;
            mGeneration = m.generation;
            mGraphLatency.store(mEng->outputLatency(), std::memory_order_relaxed);
            mEng->prepare(mPb, mBlock); // the port count may have changed: within capacity it does not allocate
            if (!mRetired.push(old)) delete old; // the service is not collecting: better a free here than a leak
            break;
        }
        }
    }

    void Player::keepLive(long long out)
    {
        // R-MIX-16: stores only — no allocation, no lock (R-PLAY-2)
        const auto &v = mEng->bindValues();
        const int n = (int)std::min(v.size(), (size_t)kMaxBinds);
        const long long k = mSlotsWritten.load(std::memory_order_relaxed);
        LiveSlot &s = mSlots[(size_t)(k % kLiveSlots)];
        const unsigned q = s.seq.load(std::memory_order_relaxed);
        s.seq.store(q + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        for (int i = 0; i < n; ++i) s.value[i].store(v[(size_t)i], std::memory_order_relaxed);
        s.count.store(n, std::memory_order_relaxed);
        s.generation.store(mGeneration, std::memory_order_relaxed);
        s.out.store(out, std::memory_order_relaxed);
        s.seq.store(q + 2, std::memory_order_release);
        mSlotsWritten.store(k + 1, std::memory_order_release);
    }

    bool Player::liveValues(int generation, std::vector<double> &out) const
    {
        const long long n = mSlotsWritten.load(std::memory_order_acquire);
        if (n <= 0) return false;
        const long long lag = (long long)std::llround(mLatency.load(std::memory_order_relaxed) * mRate) +
                              mGraphLatency.load(std::memory_order_relaxed); // R-MIX-17: the engine's own lag too
        // newest first: the first slot of this engine at or before what is heard; else the oldest kept of it.
        // The slot the thread may be rewriting next is never read.
        long long target = 0, pick = -1;
        bool first = true;
        for (long long k = n - 1; k >= 0 && k > n - kLiveSlots; --k)
        {
            const LiveSlot &s = mSlots[(size_t)(k % kLiveSlots)];
            const unsigned q = s.seq.load(std::memory_order_acquire);
            const long long at = s.out.load(std::memory_order_relaxed);
            const int gen = s.generation.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            if ((q & 1u) || s.seq.load(std::memory_order_relaxed) != q) continue;
            if (first) { target = at - lag; first = false; }
            if (gen != generation) continue; // an engine the service has since replaced
            pick = k;
            if (at <= target) break;
        }
        if (pick < 0) return false;
        const LiveSlot &s = mSlots[(size_t)(pick % kLiveSlots)];
        const unsigned q = s.seq.load(std::memory_order_acquire);
        const int count = s.count.load(std::memory_order_relaxed), gen = s.generation.load(std::memory_order_relaxed);
        if (gen != generation) return false;
        std::vector<double> v((size_t)std::max(0, count));
        for (int i = 0; i < count; ++i) v[(size_t)i] = s.value[i].load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if ((q & 1u) || s.seq.load(std::memory_order_relaxed) != q) return false; // overwritten while read: keep the last
        out.swap(v);
        return true;
    }

    void Player::click(long long pos, int n)
    {
        // every beat in [pos, pos + n) on its exact sample — the bar's first accented (R-TIME-4)
        const double spb = mSpb.load(std::memory_order_relaxed);
        const int bpb = mBpb.load(std::memory_order_relaxed);
        if (!mClick || spb <= 0.0) return;
        std::fill(mClickL.begin(), mClickL.begin() + n, 0.0);
        std::fill(mClickR.begin(), mClickR.begin() + n, 0.0);
        int cur = 0;
        auto run = [&](int to) {
            if (to <= cur) return;
            Sample *io[2] = {mClickL.data() + cur, mClickR.data() + cur};
            mClick->process(io, 2, to - cur);
            cur = to;
        };
        for (long long k = (long long)std::ceil((double)pos / spb - 1e-9);; ++k)
        {
            const long long at = std::llround((double)k * spb);
            if (at >= pos + n) break;
            if (at < pos) continue;
            run((int)(at - pos));
            const bool bar = k % bpb == 0;
            mClick->noteOn(bar ? kClickBar : kClickBeat, bar ? 127 : 96);
        }
        run(n);
        const double g = mClickGain.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            mInterleaved[(size_t)i * mChannels] += (float)(g * mClickL[(size_t)i]);
            if (mChannels > 1) mInterleaved[(size_t)i * mChannels + 1] += (float)(g * mClickR[(size_t)i]);
        }
    }

    void Player::run()
    {
        while (mRun.load())
        {
            Live m;
            while (mIn.pop(m)) apply(m);
            const long long pos = mEng->position(), a = mLoopA.load(), b = mLoopB.load();
            int n = mBlock;
            if (b > a && pos < b && pos + n > b) n = (int)(b - pos); // stop exactly at the loop's end
            mEng->render(n, mPb);
            std::fill(mInterleaved.begin(), mInterleaved.begin() + (size_t)n * mChannels, 0.0f);
            for (const Route &r : mRoutes)
            {
                if (r.port < 0 || r.port >= (int)mPb.ports.size()) continue;
                const auto &ch = mPb.ports[(size_t)r.port];
                for (size_t c = 0; c < ch.size(); ++c)
                {
                    const int dst = r.channel + (int)c;
                    if (dst >= mChannels) break;
                    for (int i = 0; i < n; ++i) mInterleaved[(size_t)i * mChannels + dst] += ch[c][(size_t)i];
                }
            }
            const auto &meters = mEng->stripMeters();
            for (size_t s = 0; s < meters.size() && s < (size_t)kMaxStrips; ++s)
            {
                mPeaks[2 * s].store(meters[s].peak[0], std::memory_order_relaxed);
                mPeaks[2 * s + 1].store(meters[s].peak[1], std::memory_order_relaxed);
            }
            mPeaks[2 * kMaxStrips].store(mEng->masterMeter().peak[0], std::memory_order_relaxed);
            mPeaks[2 * kMaxStrips + 1].store(mEng->masterMeter().peak[1], std::memory_order_relaxed);
            const bool clickOn = mClickOn.load(std::memory_order_relaxed);
            if (clickOn) click(pos - mGraphLatency.load(std::memory_order_relaxed), n); // on the beat the music is on (R-MIX-17)
            else if (mClickWas && mClick) mClick->reset(); // off: no tail waits to resume
            mClickWas = clickOn;
            mOut->write(mInterleaved.data(), n); // blocks: the device is the clock
            mLatency.store(mOut->latency(), std::memory_order_relaxed);
            mOutFrames += n;
            keepLive(mOutFrames);
            if (b > a && mEng->position() >= b) mEng->seek(a);
            mRendered.store(mEng->position());
        }
    }
}
}
