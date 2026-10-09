#include "Auditioner.h"
#include <algorithm>

namespace arstro
{
namespace solaris
{
    Auditioner::~Auditioner() { stop(); }

    bool Auditioner::start(std::unique_ptr<IAudioOut> out, std::shared_ptr<const engine::Pcm> pcm, const std::string &file, double gain, int block)
    {
        stop();
        if (!out || !pcm || pcm->frames <= 0) return false;
        mOut = std::move(out);
        mPcm = std::move(pcm);
        mFile = file;
        mGain = gain;
        mBlock = std::max(32, block);
        mBuf.assign((size_t)mBlock * 2, 0.0f); // sized HERE: the thread never allocates
        mPos.store(0);
        mRun.store(true);
        mThread = std::thread([this] { run(); });
        return true;
    }

    void Auditioner::stop()
    {
        mRun.store(false);
        if (mThread.joinable()) mThread.join();
        if (mOut) mOut->flush();
        mOut.reset();
    }

    double Auditioner::progress() const
    {
        const long long n = mPcm ? mPcm->frames : 0;
        return n > 0 ? std::min(1.0, (double)mPos.load() / (double)n) : 0.0;
    }

    void Auditioner::run()
    {
        const engine::Pcm &p = *mPcm;
        long long pos = 0;
        while (mRun.load() && pos < p.frames)
        {
            const int n = (int)std::min<long long>(mBlock, p.frames - pos);
            for (int i = 0; i < n; ++i)
            {
                mBuf[(size_t)i * 2] = (float)(mGain * p.at(pos + i, 0));
                mBuf[(size_t)i * 2 + 1] = (float)(mGain * p.at(pos + i, 1));
            }
            mOut->write(mBuf.data(), n); // blocks: the device is the clock
            pos += n;
            mPos.store(pos);
        }
        mRun.store(false); // to its end: it stops itself (the service sees it at its next pump)
    }
}
}
