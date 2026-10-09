#include "Player.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace solaris
{
    Player::~Player()
    {
        stop();
        collect();
    }

    bool Player::start(std::unique_ptr<IAudioOut> out, std::unique_ptr<engine::Engine> eng, std::vector<Route> routes,
                       int channels, int block, long long from)
    {
        stop();
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
        const long long r = mRendered.load() - (long long)std::llround(mLatency.load() * mRate);
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
            mEng->prepare(mPb, mBlock); // the port count may have changed: within capacity it does not allocate
            if (!mRetired.push(old)) delete old; // the service is not collecting: better a free here than a leak
            break;
        }
        }
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
            if (clickOn) click(pos, n);
            else if (mClickWas && mClick) mClick->reset(); // off: no tail waits to resume
            mClickWas = clickOn;
            mOut->write(mInterleaved.data(), n); // blocks: the device is the clock
            mLatency.store(mOut->latency(), std::memory_order_relaxed);
            if (b > a && mEng->position() >= b) mEng->seek(a);
            mRendered.store(mEng->position());
        }
    }
}
}
