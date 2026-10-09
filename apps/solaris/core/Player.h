/*
 *  solaris_core — Player: the engine on a thread, writing to the clock device (R-PLAY-1, R-PLAY-2).
 *
 *  The thread owns the engine while it plays. The service talks to it ONLY through a lock-free
 *  queue of `Live` messages, drained between blocks: a parameter, a gain, a pan, a mute, a seek —
 *  or a whole new engine (a structural edit), which the thread swaps in at the current position,
 *  handing the old one back through a second queue to be destroyed on the service's thread
 *  (`collect`). Position and meters come back through atomics. So the audio thread never takes a
 *  lock, never allocates (everything is sized before `start`), never touches a file.
 *
 *  The metronome (R-TIME-4, R-EDM-2) is HERE, after the engine, so it is in what you hear and never in
 *  a render: a Drum Machine of its own clicks every beat — the bar's first on the cowbell, the rest on
 *  the rim — into the device's first two channels, at the setting's level. On/off, level and tempo are
 *  atomics the service sets; the click device is made in `start`.
 *
 *  What every binding evaluated to (R-MIX-16) comes back too, block by block: after each block the
 *  thread copies the engine's `bindValues()` into a ring of pre-sized atomic slots (a sequence number
 *  brackets each write, so a reader never takes half of one), stamped with the frames handed to the
 *  device so far and the engine's generation. The service reads the slot the listener HEARS now —
 *  the device still holds `latency` of what was rendered — from the engine it last sent.
 */
#pragma once
#include "AudioOut.h"
#include "Engine.h"
#include "base/LockFreeQueue.h"
#include "device/Device.h"
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
            int generation = 0;                // Swap: which build of the song it is (its live values say so)
        };
        /** Where a port's channels land in the clock device's interleaved stream. */
        struct Route
        {
            int port = 0;
            int channel = 0;                   // the device channel the port's first channel goes to
        };

        static constexpr int kMaxStrips = 256; // meters beyond this are not reported
        static constexpr int kMaxBinds = 256;  // bindings beyond this report no live value
        static constexpr int kLiveSlots = 64;  // blocks of binding values kept: more than a device holds

        Player();
        ~Player();
        bool start(std::unique_ptr<IAudioOut> out, std::unique_ptr<engine::Engine> eng, std::vector<Route> routes,
                   int channels, int block, long long from, int generation = 0);
        void stop();
        bool running() const { return mRun.load(); }

        bool send(const Live &m) { return mIn.push(m); }
        /** The metronome: on or off, its gain (linear); and where the beats are (samples a beat, beats a bar). */
        void setClick(bool on, double gain) { mClickOn.store(on); mClickGain.store(gain); }
        void setTempo(double samplesPerBeat, int beatsPerBar) { mSpb.store(samplesPerBeat); mBpb.store(beatsPerBar < 1 ? 1 : beatsPerBar); }
        static constexpr int kClickBeat = 37, kClickBar = 56; // the Drum Machine's rim and cowbell
        void setLoop(long long from, long long to) { mLoopA.store(from); mLoopB.store(to); }
        /** The sample the listener hears now: rendered minus the device's latency and the engine's
         *  (R-MIX-17: what comes out of the engine is `outputLatency()` behind its position). */
        long long heard() const;
        long long rendered() const { return mRendered.load(); }
        double latency() const { return mLatency.load(); }
        /** Destroy engines the thread handed back (call on the service's thread). */
        void collect();
        /** Peaks of the last block: strip i at [2i, 2i+1], the master at [2·kMaxStrips, +1]. */
        float peak(int index) const { return mPeaks[(size_t)index].load(std::memory_order_relaxed); }
        /** R-MIX-16: every binding's value (the engine's bind order) as evaluated for the block the listener
         *  hears now — or the oldest kept, while the device still holds more than the ring. Only values of
         *  the engine of `generation`; false when there are none yet (call on the service's thread). */
        bool liveValues(int generation, std::vector<double> &out) const;

    private:
        /** One block's binding values: written by the audio thread, read by the service (a seqlock). */
        struct LiveSlot
        {
            std::atomic<unsigned> seq{0};           // odd while the audio thread writes it
            std::atomic<long long> out{0};          // frames handed to the device once its block was
            std::atomic<int> generation{-1}, count{0};
            std::atomic<double> value[kMaxBinds];
        };
        void run();
        void apply(const Live &m);
        void click(long long pos, int n);
        void keepLive(long long out); // the audio thread: this block's binding values into the ring

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
        std::atomic<long long> mGraphLatency{0}; // the playing engine's outputLatency (R-MIX-17)
        std::unique_ptr<std::atomic<float>[]> mPeaks{new std::atomic<float>[2 * kMaxStrips + 2]};
        std::unique_ptr<Device> mClick;
        std::vector<Sample> mClickL, mClickR;
        std::atomic<bool> mClickOn{false};
        std::atomic<double> mClickGain{0.5}, mSpb{24000.0};
        std::atomic<int> mBpb{4};
        bool mClickWas = false;
        std::unique_ptr<LiveSlot[]> mSlots;     // sized at construction: the audio thread only stores
        std::atomic<long long> mSlotsWritten{0};
        long long mOutFrames = 0;               // the audio thread's: frames handed to the device
        int mGeneration = 0;                    // the audio thread's: the engine it plays
    };
}
}
