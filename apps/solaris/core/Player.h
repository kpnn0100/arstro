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
 *
 *  The meters (R-METER-1, R-METER-2) are measured here too, after each block, by the DSP library's meters
 *  built in `start`: every strip's and the master's RMS (300 ms), held peak (1.5 s, then falling 20 dB a
 *  second) and clip latch, and the master's loudness, true peak, correlation and spectrum off a tap of the
 *  engine's master. All published through atomics; `clearMeters` is a flag the thread takes at its next block.
 */
#pragma once
#include "AudioIn.h"
#include "AudioOut.h"
#include "Follower.h"
#include "DriftIn.h"
#include "Engine.h"
#include "WorkerPool.h"
#include "MidiIn.h"
#include <bitset>
#include "base/LockFreeQueue.h"
#include "device/Device.h"
#include "meter/Meters.h"
#include <array>
#include <atomic>
#include <chrono>
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
            enum Kind { StripGain, StripPan, StripSilent, MasterGain, DeviceParam, DeviceBypass, Seek, Swap, ClearMeters } kind = Seek;
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
        /** R-MIDI-2: the queue a keyboard fills (the service's — it outlives the player) and the strip its notes play
         *  (−1: none — they are dropped). The queue is set before `start`; the armed strip may change while it runs,
         *  and the notes still held on the strip it left are let go. */
        void setMidi(LockFreeQueue<MidiEvent> *q) { mMidi = q; }
        void setArmed(int strip) { mArmed.store(strip); }
        /** R-MIDI-2: while on, every note played is kept where it was HEARD — rendered minus every latency, as `heard`
         *  says — velocity 0 its end (the pedal's held ends where the pedal came up). */
        struct Played
        {
            long long at = 0;
            int pitch = 0, velocity = 0;
            int kind = 0;          // R-MIDI-3: 0 a note; 1 the pitch wheel, 2 the mod wheel — `value`
            double value = 0;
        };
        void setRecording(bool on) { mRecording.store(on); }
        /** R-MIDI-2: a count-in before the song — `frames` of silence clicked on its beats (the metronome forced on),
         *  leading into `from` on the beat; set before `start`. While it runs `heard` counts back to `from`. */
        void setCountIn(long long frames) { mCountIn.store(std::max(0LL, frames)); }
        bool countingIn() const { return mCountIn.load() > 0; }
        bool takePlayed(Played &p) { return mPlayed.pop(p); } // the service's thread
        /** R-REC-1: the capture device (the service opens it; set before `start`, closed by `stop`). Each cycle a block is
         *  read — never waiting — and the armed AUDIO strip hears it through its chain while monitoring is on. */
        void setInput(std::unique_ptr<IAudioIn> in, int channels);
        void setMonitor(int strip, bool on) { mMonStrip.store(strip); mMonOn.store(on); }
        double inputLatency() const { return mInLatency.load(); }
        bool hasInput() const { return (bool)mInput; }
        long long graphLatency() const { return mGraphLatency.load(); } // R-MIX-17: what the engine's output lags by
        /** R-REC-1: keep what the input brings, from the next frame read (not during a count-in): the audio thread hands
         *  it to a ring a recorder thread of the player's drains into the take — so a long take needs no one to pump it.
         *  `endTake` (after `stop`, so nothing more can come) gives it back with the song sample its first frame was read
         *  at (−1: none was). */
        struct Take
        {
            std::vector<float> left, right;
            long long from = -1;
            long long dropped = 0; // frames the ring could not hold (the recorder fell behind)
            // R-REC-2: where the song's position jumped (a loop turned): (frame of the take, the song sample read there) —
            // the first is (0, from)
            std::vector<std::pair<long long, long long>> marks;
        };
        void startTake();
        Take endTake();
        /** R-DEV-5: the device's underruns since `start` (−1: its driver cannot count them). */
        long long xruns() const { return mOut ? mOut->xruns() : -1; }
        /** R-DEV-1/2: a FOLLOWER device and the ports it carries (each at its first channel) — set before `start`,
         *  stopped by `stop`. The audio thread pushes their sound to it each block; it keeps itself on the clock. */
        void addFollower(std::unique_ptr<Follower> f, std::vector<Route> routes);
        /** R-DEV-4: the clock's own channels heard `frames` later (a follower slower than it) — set before `start`. */
        void setClockDelay(int frames) { mClockDelay = std::max(0, frames); }
        int clockDelay() const { return mClockDelay; }
        /** R-DEV-5: what each follower says (the service's thread, while it runs). */
        std::vector<Follower::State> followerStates() const;
        /** R-DEV-1/2: the input, when it is on another device and held to the clock (else null). */
        const DriftIn *driftInput() const { return dynamic_cast<const DriftIn *>(mInput.get()); }
        /** Destroy engines the thread handed back (call on the service's thread). */
        void collect();
        /** Peaks of the last block: strip i at [2i, 2i+1], the master at [2·kMaxStrips, +1]. */
        float peak(int index) const { return mPeaks[(size_t)index].load(std::memory_order_relaxed); }
        /** R-METER-2, indexed as `peak`: the RMS over the last 300 ms (a one-pole on the blocks' mean squares). */
        float rms(int index) const { return mRms[(size_t)index].load(std::memory_order_relaxed); }
        /** R-PERF-1: the audio thread's load — its time on a block (all but the device's wait) over the block's
         *  duration, smoothed over about a third of a second; its worst block since the last take; strip i's share. */
        float cpu() const { return mCpu.load(std::memory_order_relaxed); }
        /** R-PERF-2: how many threads render (the audio thread one of them) — before `start`. */
        void setThreads(int n) { mThreads = std::max(1, n); }
        float takeCpuPeak() { return mCpuPeak.exchange(0.0f, std::memory_order_relaxed); }
        float stripCpu(int i) const { return i >= 0 && i < kMaxStrips ? mStripCpu[(size_t)i].load(std::memory_order_relaxed) : 0.0f; }
        /** The held peak: the most of the last 1.5 s, then falling 20 dB a second. */
        float hold(int index) const { return mHold[(size_t)index].load(std::memory_order_relaxed); }
        /** Whether strip i (the master: kMaxStrips) reached full scale since the last call — and forget it. */
        bool takeClip(int strip) { return mClip[(size_t)strip].exchange(false, std::memory_order_relaxed); }
        /** R-METER-1: the master as measured — LUFS, dBTP (the most since play or `clearMeters`), −1…+1, dB a band. */
        struct Loudness
        {
            double momentary = LoudnessMeter::kSilence, shortTerm = LoudnessMeter::kSilence, integrated = LoudnessMeter::kSilence;
            double truePeak = LoudnessMeter::kSilence, correlation = 0;
            std::array<double, SpectrumAnalyser::kBands> spectrum{};
        };
        void loudness(Loudness &out) const;
        /** Forget the held peaks, the clip latches, the true peak and the integrated loudness: a message, so the
         *  thread does it in order with the edits sent before it (a gain brought down, then the clip light cleared). */
        void clearMeters();
        /** A clear is sent and not yet taken: what the meters say now is from before it — not to be read. */
        bool clearing() const { return mClearPending.load(std::memory_order_acquire); }
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
        void measure(int n);          // the audio thread: this block into the meters (R-METER-1, R-METER-2)
        void midi();                  // the audio thread: the keyboard's messages since the last block (R-MIDI-2)
        void ballistics(int k, float peak, double meanSquare, int n); // one meter's RMS and held peak
        void input(int n, long long pos, bool song); // the audio thread: a block of the input — monitored, kept (R-REC-1)
        void recorder();                             // the recorder thread: the ring into the take

        std::thread mThread;
        std::atomic<bool> mRun{false};
        std::unique_ptr<IAudioOut> mOut;
        engine::Engine *mEng = nullptr;
        int mThreads = 1;                     // R-PERF-2
        std::unique_ptr<WorkerPool> mPool;    // … the strips nothing feeds, rendered on it
        std::vector<Route> mRoutes;
        // R-DEV-1/2/4: the followers, each with its ports and its block (sized at start), and the clock's delay line
        struct Feed
        {
            std::unique_ptr<Follower> f;
            std::vector<Route> routes;
            std::vector<float> buf;
        };
        std::vector<Feed> mFollowers;
        int mClockDelay = 0;
        std::vector<float> mDelay; // mClockDelay frames of the clock's channels, a ring
        size_t mDelayAt = 0;
        void feedFollowers(int n, bool silent);
        void delayClock(int n);
        int mChannels = 2, mBlock = 256, mRate = 48000;
        engine::PortBuffers mPb;
        std::vector<float> mInterleaved;
        LockFreeQueue<Live> mIn{1024};
        LockFreeQueue<engine::Engine *> mRetired{64};
        std::atomic<long long> mRendered{0}, mLoopA{0}, mLoopB{0};
        std::atomic<double> mLatency{0};
        std::atomic<long long> mGraphLatency{0}; // the playing engine's outputLatency (R-MIX-17)
        std::unique_ptr<std::atomic<float>[]> mPeaks{new std::atomic<float>[2 * kMaxStrips + 2]};
        // R-METER-1/2: sized at construction or in `start`; the thread only stores
        std::unique_ptr<std::atomic<float>[]> mRms{new std::atomic<float>[2 * kMaxStrips + 2]};
        std::unique_ptr<std::atomic<float>[]> mHold{new std::atomic<float>[2 * kMaxStrips + 2]};
        std::unique_ptr<std::atomic<bool>[]> mClip{new std::atomic<bool>[kMaxStrips + 1]};
        std::vector<double> mMeanSq, mHeld;     // the audio thread's: each meter's mean square and held peak
        std::vector<long long> mHeldFor;        // samples since the held peak was set
        std::vector<float> mTapL, mTapR;        // the engine's master, a block (Engine::tapMaster)
        std::unique_ptr<LoudnessMeter> mLoud;
        std::unique_ptr<TruePeakMeter> mTrue;
        std::unique_ptr<CorrelationMeter> mCorr;
        std::unique_ptr<SpectrumAnalyser> mSpec;
        long long mLoudBlocks = -1;             // the gating blocks the integrated loudness was last read at
        std::atomic<double> mMomentary{LoudnessMeter::kSilence}, mShortTerm{LoudnessMeter::kSilence},
            mIntegrated{LoudnessMeter::kSilence}, mTruePeak{LoudnessMeter::kSilence}, mCorrelation{0};
        std::array<std::atomic<float>, SpectrumAnalyser::kBands> mSpectrum{};
        std::atomic<float> mCpu{0.0f}, mCpuPeak{0.0f};       // R-PERF-1
        std::array<std::atomic<float>, kMaxStrips> mStripCpu{};
        void timed(int frames, std::chrono::steady_clock::time_point t0);
        std::atomic<bool> mClearPending{false};
        // R-MIDI-2: the keyboard — the queue is the service's; the rest the audio thread's
        LockFreeQueue<MidiEvent> *mMidi = nullptr;
        std::atomic<int> mArmed{-1};
        std::atomic<bool> mRecording{false};
        LockFreeQueue<Played> mPlayed{4096};
        SustainPedal mPedal;
        std::bitset<128> mSounding;             // pitches held down on the armed strip
        int mArmedWas = -1;
        std::atomic<long long> mCountIn{0};     // R-MIDI-2: frames of count-in still to play
        bool mClearNow = false;                 // the audio thread's: a ClearMeters message taken, for `measure`
        std::unique_ptr<Device> mClick;
        std::vector<Sample> mClickL, mClickR;
        std::atomic<bool> mClickOn{false};
        std::atomic<double> mClickGain{0.5}, mSpb{24000.0};
        std::atomic<int> mBpb{4};
        bool mClickWas = false;
        std::unique_ptr<LiveSlot[]> mSlots;     // sized at construction: the audio thread only stores
        // R-REC-1: the input — the device and its buffers sized in `start`; the ring the take goes through
        std::unique_ptr<IAudioIn> mInput;
        int mInChannels = 2;
        std::vector<float> mInBuf, mInL, mInR;
        std::atomic<int> mMonStrip{-1};
        std::atomic<bool> mMonOn{false}, mTaking{false}, mRecRun{false};
        std::atomic<double> mInLatency{0};
        std::atomic<long long> mTakeDropped{0};
        struct InChunk
        {
            long long at = 0; // the song sample (the engine's position) its first frame was read at
            int n = 0;
            std::array<float, 512> l{}, r{};
        };
        LockFreeQueue<InChunk> mRecQ{512};     // ~5 s at 48 kHz in flight: the recorder drains it every 2 ms
        std::thread mRecorder;
        Take mTake;                             // the recorder thread's while it runs
        long long mTakeNext = 0;                // the song sample the next chunk follows on from (the recorder's)
        std::atomic<long long> mSlotsWritten{0};
        long long mOutFrames = 0;               // the audio thread's: frames handed to the device
        int mGeneration = 0;                    // the audio thread's: the engine it plays
    };
}
}
