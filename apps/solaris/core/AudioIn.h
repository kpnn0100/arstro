/*
 *  solaris_core — the capture seam (R-REC-1): what a host gives the core to hear an input device by.
 *
 *  Beside `IAudioOut`: the core never names a device API (R-SVC-4). A host's input runs its own thread and keeps what
 *  the device captured in a ring; the audio thread takes a block of it each cycle and NEVER waits — what has not
 *  arrived yet is silence, and counted, so a slow capture cannot stall the output that is the clock.
 */
#pragma once
#include <atomic>
#include <chrono>
#include <string>

namespace arstro
{
namespace solaris
{
    class IAudioIn
    {
    public:
        virtual ~IAudioIn() = default;
        /** Open `device` ("" = the system default input) at `rate` with `channels` (1 or 2), ~`bufferFrames` deep. */
        virtual bool open(const std::string &device, int rate, int channels, int bufferFrames, std::string &err) = 0;
        /** Up to `frames` captured frames, interleaved by the opened channels; never blocks — what has not arrived is
         *  written as zeros. Returns how many were real. Called from the audio thread only. */
        virtual int read(float *interleaved, int frames) = 0;
        /** Seconds between a sound reaching the input and its frame being readable. */
        virtual double latency() = 0;
        /** The frames `read` had to fill with silence since `open` (an overrun of the capture, or it falling behind). */
        virtual long long missed() const { return 0; }
        /** R-DEV-2: what it holds for `read` now, and when its last block landed (steady clock, ns) and how many frames
         *  it was — so a reader on ANOTHER device's clock can tell its true fill between arrivals and hold it there.
         *  False: it cannot say (it is read as it is, and may drift). The audio thread's. */
        virtual bool held(long long &frames, long long &lastNs, int &lastFrames) const
        {
            (void)frames, (void)lastNs, (void)lastFrames;
            return false;
        }
    };

    /** R-DEV-2: when a capture's last block landed and its size — marked by the capture's thread after each push,
     *  read by the audio thread whole (a seqlock: no lock, no wait). For a host's `held`. */
    class CaptureStamp
    {
    public:
        void mark(int frames)
        {
            const long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            mSeq.fetch_add(1, std::memory_order_acq_rel);
            mNs.store(ns, std::memory_order_relaxed);
            mFrames.store(frames, std::memory_order_relaxed);
            mSeq.fetch_add(1, std::memory_order_release);
        }
        bool read(long long &ns, int &frames) const
        {
            for (;;)
            {
                const unsigned a = mSeq.load(std::memory_order_acquire);
                if (a & 1u) continue;
                ns = mNs.load(std::memory_order_relaxed);
                frames = mFrames.load(std::memory_order_relaxed);
                std::atomic_thread_fence(std::memory_order_acquire);
                if (mSeq.load(std::memory_order_relaxed) == a) return frames > 0;
            }
        }

    private:
        std::atomic<unsigned> mSeq{0};
        std::atomic<long long> mNs{0};
        std::atomic<int> mFrames{0};
    };
}
}
