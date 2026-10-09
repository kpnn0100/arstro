/*
 *  solaris_core — Auditioner: a sample heard from the browser, outside the song (R-EDM-9, R-BROWSE-2).
 *
 *  Its own output stream and thread, so a sound previews whether the song plays or not; the decoded
 *  file at the audition level, to its end, then it stops itself. Never in a render: a render does not
 *  know it exists. Like the Player it never allocates or locks on its thread (the block is sized in
 *  `start`), and the service reads where it is through an atomic.
 */
#pragma once
#include "AudioOut.h"
#include "MixGraph.h"
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace arstro
{
namespace solaris
{
    class Auditioner
    {
    public:
        ~Auditioner();
        /** Play `pcm` (decoded at `rate`) from its start at `gain` (linear), through an opened `out`. */
        bool start(std::unique_ptr<IAudioOut> out, std::shared_ptr<const engine::Pcm> pcm, const std::string &file, double gain, int block);
        void stop();
        bool playing() const { return mRun.load(); }
        const std::string &file() const { return mFile; }
        /** 0 … 1 of the file handed to the device. */
        double progress() const;

    private:
        void run();
        std::thread mThread;
        std::atomic<bool> mRun{false};
        std::unique_ptr<IAudioOut> mOut;
        std::shared_ptr<const engine::Pcm> mPcm;
        std::string mFile;
        double mGain = 1.0;
        int mBlock = 256;
        std::vector<float> mBuf;
        std::atomic<long long> mPos{0};
    };
}
}
