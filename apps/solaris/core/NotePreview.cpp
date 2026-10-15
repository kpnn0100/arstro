#include "NotePreview.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace solaris
{
    NotePreview::~NotePreview()
    {
        stop();
        collect();
        std::unique_ptr<Voice> unheard(mIncoming.exchange(nullptr));
    }

    void NotePreview::start(std::unique_ptr<IAudioOut> out, int block)
    {
        stop();
        collect();
        mOut = std::move(out);
        mBlock = std::max(32, block);
        mMix.assign((size_t)mBlock * 2, 0.0f); // sized HERE: the thread never allocates
        mRun.store(true);
        mThread = std::thread([this] { run(); });
    }

    void NotePreview::hear(std::unique_ptr<Voice> v)
    {
        // a preview the thread has not picked up yet is taken over before it was heard: freed here
        std::unique_ptr<Voice> unheard(mIncoming.exchange(v.release()));
        if (unheard) mFinished.fetch_add(1);
    }

    void NotePreview::stop()
    {
        mRun.store(false);
        if (mThread.joinable()) mThread.join();
        if (mOut) mOut->flush();
        mOut.reset();
        // a preview handed over and not picked up stays: the next thread hears it (`hear` then `start`)
    }

    void NotePreview::collect()
    {
        for (auto &slot : mRetired) std::unique_ptr<Voice> done(slot.exchange(nullptr));
    }

    void NotePreview::retire(Voice *v)
    {
        mFinished.fetch_add(1);
        for (auto &slot : mRetired)
        {
            Voice *empty = nullptr;
            if (slot.compare_exchange_strong(empty, v)) return;
        }
        // every slot full (the service has not pumped for 32 previews): keep rendering silence into nothing
        // rather than free here — the voice is lost to the heap until exit, never freed on this thread
    }

    void NotePreview::run()
    {
        const float quiet = (float)std::pow(10.0, kQuietDb / 20.0);
        Voice *cur = nullptr, *old = nullptr;
        int fade = 0, idle = 0;
        auto renderInto = [&](Voice &v, int n, float g0, float dg) {
            v.engine->render(n, v.out);
            float peak = 0.0f;
            for (const auto &port : v.out.ports)
            {
                if (port.empty()) continue;
                const auto &L = port[0];
                const auto &R = port.size() > 1 ? port[1] : port[0];
                for (int i = 0; i < n; ++i)
                {
                    const float g = g0 + dg * (float)i;
                    mMix[(size_t)i * 2] += g * L[(size_t)i];
                    mMix[(size_t)i * 2 + 1] += g * R[(size_t)i];
                    peak = std::max(peak, std::max(std::fabs(L[(size_t)i]), std::fabs(R[(size_t)i])));
                }
            }
            v.pos += n;
            return peak;
        };
        while (mRun.load())
        {
            if (Voice *in = mIncoming.exchange(nullptr))
            {
                if (old) retire(old);               // a third preview before the second's fade ended
                old = cur;                          // the one ringing now fades out under the new one
                fade = 0;
                cur = in;
            }
            mSounding.store(cur != nullptr);
            const int n = mBlock;
            std::fill(mMix.begin(), mMix.end(), 0.0f);
            if (cur)
            {
                const float peak = renderInto(*cur, n, 1.0f, 0.0f);
                cur->quiet = (cur->pos >= cur->noteFrames && peak < quiet) ? cur->quiet + n : 0;
                if (cur->quiet >= kQuietFrames || cur->pos >= cur->maxFrames) { retire(cur); cur = nullptr; }
            }
            if (old)
            {
                const float g0 = 1.0f - (float)fade / kFadeFrames, dg = -1.0f / kFadeFrames;
                renderInto(*old, std::min(n, kFadeFrames - fade), g0, dg);
                fade += n;
                if (fade >= kFadeFrames) { retire(old); old = nullptr; }
            }
            mOut->write(mMix.data(), n); // blocks: the device is the clock
            idle = (cur || old) ? 0 : idle + n;
            if (idle >= kIdleFrames)
            {
                // a second of nothing: the stream closes — unless a preview arrived as it did (the service
                // hands one over, THEN looks whether a thread runs: one of the two sees the other)
                mRun.store(false);
                if (!mIncoming.load()) break;
                mRun.store(true);
                idle = 0;
            }
        }
        if (cur) retire(cur);
        if (old) retire(old);
        mSounding.store(false);
    }
}
}
