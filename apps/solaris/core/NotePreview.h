/*
 *  solaris_core — NotePreview: a note heard through one strip's chain, outside the song (R-ROLL-7).
 *
 *  The browser's Auditioner plays a decoded file; this plays an ENGINE — the service compiles one strip,
 *  its rack and a one-note pattern into a graph of their own (`SolarisService::previewCommand`), so a
 *  preview sounds exactly as that strip will in the song, and the song's engine never knows it exists.
 *  Never in a render: a render does not know it either.
 *
 *  Its own stream and thread, opened by the first preview and closed after a second of quiet, so a run of
 *  previews — a drag through pitch rows, keys pressed one after another — costs no stream per note. A
 *  preview started while another rings takes over: the old one fades out under it over kFadeFrames,
 *  never a click. A preview ends when its sound has stayed under kQuietDb for kQuietFrames after its note
 *  (or at its longest).
 *
 *  The thread never allocates, frees or locks (R-PLAY-2's rule): a Voice — its engine and the buffers its
 *  render writes, sized by the service — arrives through an atomic slot; a Voice the thread is done with
 *  goes back through a ring of atomic slots for the service to free (`collect`, called from its pump).
 */
#pragma once
#include "AudioOut.h"
#include "Engine.h"
#include <array>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace arstro
{
namespace solaris
{
    class NotePreview
    {
    public:
        static constexpr int kFadeFrames = 480;      // 10 ms at 48 kHz: the old note's way out
        static constexpr double kQuietDb = -80.0;    // under this after its note, a preview is over …
        static constexpr int kQuietFrames = 4800;    // … once it stayed there 0.1 s (at 48 kHz)
        static constexpr int kIdleFrames = 48000;    // a second with nothing to hear: the stream closes

        /** One preview: built on the service's thread, rendered on the preview's. */
        struct Voice
        {
            std::unique_ptr<engine::Engine> engine;
            engine::PortBuffers out;                 // sized for `block` frames by `prepare`
            long long noteFrames = 0, maxFrames = 0; // when its note ends; its longest
            long long pos = 0;
            int quiet = 0;
        };

        ~NotePreview();
        /** The stream is open and the thread runs (a preview is heard, or was within the last second). */
        bool running() const { return mRun.load(); }
        /** Open the thread on `out` (opened by the caller); a stream still running is closed first. The
         *  service hands a preview over (`hear`) and THEN starts a thread when none runs. */
        void start(std::unique_ptr<IAudioOut> out, int block);
        int block() const { return mBlock; }
        /** Hear `v` from its start, taking over from what is heard now. */
        void hear(std::unique_ptr<Voice> v);
        void stop();
        /** Free what the thread is done with — the service's thread, never the audio's. */
        void collect();
        /** A preview is being heard (or waits to be). */
        bool sounding() const { return mSounding.load() || mIncoming.load() != nullptr; }
        /** Previews heard to their end or taken over since the start (a test's count). */
        long long finished() const { return mFinished.load(); }

    private:
        void run();
        void retire(Voice *v);
        std::thread mThread;
        std::atomic<bool> mRun{false}, mSounding{false};
        std::unique_ptr<IAudioOut> mOut;
        int mBlock = 256;
        std::vector<float> mMix;                     // interleaved stereo, sized in `start`
        std::atomic<Voice *> mIncoming{nullptr};
        std::array<std::atomic<Voice *>, 32> mRetired{};
        std::atomic<long long> mFinished{0};
    };
}
}
