/*
 *  solaris_core — Player: the engine on a thread, writing to the clock device (R-PLAY-1, R-PLAY-2).
 *
 *  The thread owns the engine while it plays. The service talks to it ONLY through a lock-free
 *  queue of `Live` messages, drained between blocks: a parameter, a gain, a pan, a mute, a seek —
 *  or a whole new engine (a structural edit), which the thread swaps in at the current position,
 *  handing the old one back through a second queue to be destroyed on the service's thread
 *  (`collect`). Position and meters come back through atomics. So the audio thread never takes a
 *  lock, never allocates (everything is sized before `start`), never touches a file.
 */
#pragma once
#include "AudioOut.h"
#include "Engine.h"
#include "base/LockFreeQueue.h"
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace arstro
{
namespace solaris
{
    class Player
    {
    public:
        struct Live
        {
            enum Kind { StripGain, StripPan, StripSilent, MasterGain, DeviceParam, DeviceBypass, Seek, Swap } kind = Seek;
            int strip = 0, device = 0;         // strip −1 = the master's rack
            double value = 0;
            char name[48] = {0};               // a device parameter's registry name
            engine::Engine *engine = nullptr;  // Swap: ownership passes to the player
            long long sample = 0;              // Seek
        };
        /** Where a port's channels land in the clock device's interleaved stream. */
        struct Route
        {
            int port = 0;
            int channel = 0;                   // the device channel the port's first channel goes to
        };

        static constexpr int kMaxStrips = 256; // meters beyond this are not reported

        ~Player();
        bool start(std::unique_ptr<IAudioOut> out, std::unique_ptr<engine::Engine> eng, std::vector<Route> routes,
                   int channels, int block, long long from);
        void stop();
        bool running() const { return mRun.load(); }

        bool send(const Live &m) { return mIn.push(m); }
        void setLoop(long long from, long long to) { mLoopA.store(from); mLoopB.store(to); }
        /** The sample the listener hears now: rendered minus the device's latency. */
        long long heard() const;
        long long rendered() const { return mRendered.load(); }
        double latency() const { return mLatency.load(); }
        /** Destroy engines the thread handed back (call on the service's thread). */
        void collect();
        /** Peaks of the last block: strip i at [2i, 2i+1], the master at [2·kMaxStrips, +1]. */
        float peak(int index) const { return mPeaks[(size_t)index].load(std::memory_order_relaxed); }

    private:
        void run();
        void apply(const Live &m);

        std::thread mThread;
        std::atomic<bool> mRun{false};
        std::unique_ptr<IAudioOut> mOut;
        engine::Engine *mEng = nullptr;
        std::vector<Route> mRoutes;
        int mChannels = 2, mBlock = 256, mRate = 48000;
        engine::PortBuffers mPb;
        std::vector<float> mInterleaved;
        LockFreeQueue<Live> mIn{1024};
        LockFreeQueue<engine::Engine *> mRetired{64};
        std::atomic<long long> mRendered{0}, mLoopA{0}, mLoopB{0};
        std::atomic<double> mLatency{0};
        std::unique_ptr<std::atomic<float>[]> mPeaks{new std::atomic<float>[2 * kMaxStrips + 2]};
    };
}
}
