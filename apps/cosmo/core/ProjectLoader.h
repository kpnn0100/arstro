/*
 *  Arstro cosmo_core — ProjectLoader: turn a project's entries into decoded images,
 *  on a pool, delivered strictly in entry order.
 *
 *  This used to be three functions in the GTK host (`startEntriesLoad` / `decodeEntry` /
 *  `pollLoad` in linux_main.cpp), which made the app's most important behaviour
 *  unreachable without a window: a project could not be opened from a shell (D-6), so the
 *  CPU budget's two defects (D-11, D-12) had to be found by reading rather than by
 *  measuring, and the log line that was supposed to prove the feature had never once been
 *  observed. R-SVC-1 puts it where the architecture doc always said it lived.
 *
 *  What stayed behind in the host is the part that is genuinely the host's: applying a
 *  result to the App and driving the animation. This class knows nothing about App, GTK,
 *  Artboard or a screen — it takes entries and hands back results, so a test can run a
 *  whole 18-image load with a fake decoder and no display.
 *
 *  Order matters and is not negotiable: `.cosmoproj` identifies a node's parent by ENTRY
 *  INDEX, so results are produced out of order (workers claim with an atomic counter) and
 *  consumed strictly in order. The pool, the window and the byte cap all come from
 *  OrderedParallelLoad; this is the binding plus the decode itself.
 *
 *  The worker count comes from ThreadBudget, never from the core count and never from a
 *  second conversion of the user's percentage — that duplication was D-11.
 */
#pragma once
#include "EditSession.h"
#include "OrderedParallelLoad.h"
#include "ThreadBudget.h"
#include "decode/ImageDecoder.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo
{
    class ProjectLoader
    {
    public:
        /** Entries ahead of the applier, and decoded-but-unapplied bytes: a 24 MP frame is
         *  ~100 MB and N workers outrun one applier, so without a bound a big catalog
         *  would be decoded into RAM all at once (R-LOADPERF-1a). */
        static constexpr std::size_t kDecodeWindow = 8;
        static constexpr std::size_t kMaxInFlightBytes = 512ull * 1024 * 1024;

        /** One decoded entry, in the shape the applier needs. Groups carry no pixels. */
        struct Result
        {
            std::size_t index = 0;          // which entry this is (R-LOADUX-1)
            bool group = false;
            std::string name;               // group name, or leaf display name
            std::string imagePath;          // source path (image leaves)
            int parent = -1;
            EditParams params;              // per-item edit params (image or group)
            History history;                // per-item branching edit timeline
            std::vector<uint8_t> rgba;      // decoded pixels (image leaves)
            EditSession::Thumb thumb;       // built on the worker (R-LOADPERF-2)
            int w = 0, h = 0;
            bool decoded = false;           // false = missing or failed to decode
            bool bypass = false;            // R-BYPASS-6
        };

        /** A decoder per worker thread. NativeImageDecoder is stateless, so this is
         *  normally `[]{ return std::make_unique<NativeImageDecoder>(); }` — but it is a
         *  factory, not a shared instance, so a decoder that *is* stateful stays safe and
         *  a test can inject a fake. */
        using DecoderFactory = std::function<std::unique_ptr<IImageDecoder>()>;

        ProjectLoader() = default;
        ProjectLoader(const ProjectLoader &) = delete;
        ProjectLoader &operator=(const ProjectLoader &) = delete;
        ~ProjectLoader() { stop(); }

        /** Begin decoding. `budget` is reserved for the duration (ThreadBudget::beginLoad)
         *  and released by `stop()` or when the last result is consumed, so the engine
         *  gets its threads back the moment the load ends. `onWorkerStart` runs once on
         *  each worker thread before it claims work — the host uses it to pin nested
         *  library parallelism (R-CPU-2c). */
        void start(std::vector<EditSession::WorkspaceEntry> entries, ThreadBudget &budget,
                   DecoderFactory makeDecoder, std::function<void()> onWorkerStart = {});

        /** Take the next result IN ORDER if one is ready; false if not (or if done).
         *  Non-blocking, so a UI thread can call it every frame. */
        bool poll(Result &out);

        bool active() const { return mBudget != nullptr; }
        bool finished() const { return mPipe.finished(); }
        std::size_t consumed() const { return mPipe.consumed(); }
        std::size_t total() const { return mEntries.size(); }
        /** The pool size actually started — what the budget allotted, which is not always
         *  what the percentage suggests (R-CPU-5: the cap can bind first). */
        int workers() const { return mWorkers; }

        /** Entries a worker has CLAIMED and is decoding, in claim order, moved out for the
         *  caller to report. R-LOADUX-4: a count of finished entries is not progress when one
         *  entry takes nine seconds — five workers start together, so without this the bar sits
         *  at zero for the length of the first decode and then leaps by five (D-22).
         *
         *  Buffered rather than delivered by callback because `produce()` runs on a worker and
         *  the service is single-threaded by contract (R-SVC-6): the claim is recorded under a
         *  mutex here and turned into an Event by whoever pumps. */
        void drainStarted(std::vector<std::size_t> &out);
        /** How many entries have been claimed — finished ones included. */
        std::size_t started() const;

        /** Sub-image progress for the entries currently being decoded (D-24). One RAF takes
         *  ~8.3 s and 90% of it is a single `dcraw_process()`, so per-entry reporting left the
         *  bar unable to move for nine seconds at a time. Recorded on the worker under the same
         *  mutex as the claims and drained by whoever pumps, because the service is
         *  single-threaded by contract (R-SVC-6). */
        struct EntryProgress { std::size_t index = 0; double fraction = 0.0; std::string stage; };
        void drainProgress(std::vector<EntryProgress> &out);
        /** Sum of the in-flight fractions — what a bar adds to `consumed()` to move smoothly. */
        double partial() const;

        /** Stop the workers, join them, and release the budget. Idempotent. */
        void stop();

    private:
        Result produce(std::size_t i, IImageDecoder *dec) const;
        void releaseBudget();

        mutable std::mutex mStartedMu;
        std::vector<std::size_t> mStartedQueue;   // claimed, not yet reported
        std::size_t mStartedCount = 0;
        std::vector<EntryProgress> mProgressQueue;          // coalesced: newest per index
        std::vector<double> mFraction;                     // by entry index, 0..1

        std::vector<EditSession::WorkspaceEntry> mEntries;
        OrderedParallelLoad<Result> mPipe;
        ThreadBudget *mBudget = nullptr;
        int mWorkers = 0;
    };
}
}
