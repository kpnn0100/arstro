/*
 *  Arstro ImageProcessing Library
 *
 *  RenderService: runs an EditEngine on its OWN worker thread so the front end never
 *  blocks on a render. The UI thread submits edit requests (a slot + an EditParams
 *  snapshot, coalesced to the latest) and polls for completed frames; the engine
 *  lives entirely on the worker, so there is no shared engine access. This is the
 *  "UI on one core, core engine on another" split — and it is UI-independent, so a
 *  video editor can reuse it.
 *
 *  Built with std::thread when ARSTRO_ENABLE_THREADS is defined (native, -pthread);
 *  otherwise it degrades to a synchronous in-line renderer (e.g. single-threaded web).
 */
#pragma once
#include "EditEngine.h"
#include "EditParams.h"
#include "../analysis/Histogram.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#ifdef ARSTRO_ENABLE_THREADS
#include <condition_variable>
#include <mutex>
#include <thread>
#endif

namespace arstro
{
    class RenderService
    {
    public:
        /** What a preview render is FOR (R-PREVIEW-1/2/3).
         *
         *  `Interactive` means a gesture is in flight, so LATENCY outranks resolution: the
         *  worker renders the finest pyramid level its own last measurement says will fit
         *  the interactive budget. `Final` means the gesture has settled (or this is a
         *  photo change, a preset, an undo — anything the user is not dragging), so it
         *  renders level 0 whatever that costs.
         *
         *  Default is `Final`, so every existing caller keeps its exact behaviour and only
         *  a front end that knows it is mid-gesture opts in. */
        enum class RenderIntent { Final, Interactive };
        struct Frame
        {
            std::vector<uint8_t> rgba;  // straight RGBA8
            int width = 0, height = 0;
            HistogramData hist;         // final output histogram
            HistogramData preCurveHist; // luma entering the tone curve
            HueHistogram preMixerHue;   // hue entering the colour mixer
            /** What this frame cost, wall-clock, and whether a cold slot had to be re-decoded
             *  to produce it (R-MEM-2). Carried on the frame because the service cannot time
             *  work it does not do — and without it `frame.ready` reported no number at all, so
             *  a 250 ms hop and a 2537 ms one looked identical in the log (D-44). */
            double ms = 0.0;
            bool rehydrated = false;
            /** Which pyramid level produced this frame, 0 = finest, and that level's long
             *  edge (R-PREVIEW-2). A view needs both: the level to know whether to keep
             *  refining, the edge because a coarse frame is drawn at the SAME canvas rect
             *  as a fine one and so is upscaled on the way to the screen. */
            int level = 0;
            int levelEdge = 0;
            /** How many non-finite pixel values the transfer functions had to substitute
             *  while producing this frame (D-48). Normally 0. Non-zero means something
             *  upstream produced a NaN and the frame is wrong — reported rather than either
             *  crashing (which it used to) or being silent (which the fix would otherwise
             *  have made it). */
            unsigned long long nonFinite = 0;
            /** Where the masks whose region is COMPUTED actually landed, in normalised
             *  framed-image coordinates (R-AISEG-13). It rides on the frame for the same reason
             *  the histograms do: it is a product of this render, it is only true of this
             *  render, and the view that draws the frame is the view that draws it. Geometry,
             *  not pixels — which is what makes it something a view is allowed to hold. */
            std::vector<MaskOutline> maskOutlines;
        };

        RenderService();
        ~RenderService();
        RenderService(const RenderService &) = delete;
        RenderService &operator=(const RenderService &) = delete;

        /** Where a slot's pixels came from, so an evicted slot can be re-decoded (R-MEM-2).
         *  A path, not a slot id: the loader runs on the render worker, and handing it a
         *  slot would mean reaching back into caller-owned, UI-thread-mutated state from
         *  another thread. `rgba` is filled with straight RGBA8, the same bytes `addImage`
         *  takes. Return false and the render is skipped rather than drawn wrong.
         *
         *  A `std::function` seam, exactly like `EditEngine::setComputeAccelerator`: no
         *  codec enters arstro_image, and the host's own decoder — budgeted, per R-CPU-2c
         *  — is what actually runs. */
        using SourceLoader = std::function<bool(const std::string &path, bool fullFidelity,
                                                std::vector<uint8_t> &rgba, int &w, int &h)>;
        void setSourceLoader(SourceLoader loader);

        /** Cap the engine's two pixel pools in bytes (R-MEM-1). Applied on the worker. */
        void setMemoryCaps(size_t sourceBytes, size_t proxyBytes);
        /** Measured resident pixel bytes and the number of re-decodes eviction has caused
         *  (R-MEM-4). Cheap snapshots kept by the worker, so the UI thread may read them. */
        /** A slot's full-resolution pixel size, {0,0} if unknown. Answered from the size
         *  recorded when the slot was ADDED, not from the engine: an add is queued to the
         *  worker, so asking the engine returned {0,0} until the worker got round to it and
         *  the answer depended on how many times the caller had pumped (D-56). The size is
         *  known at ingest and never changes, so there is nothing to wait for. Takes the
         *  render mutex on the threaded build; a cheap read the UI may make whenever it likes. */
        bool sourceSize(int slot, int &w, int &h) const;
        /** Box-average the slot's linear source pixels around a normalised point, for the
         *  white-balance picker (R-WB-1). Takes the render mutex; a cheap read the UI may make. */
        bool sampleSourceLinear(int slot, double nx, double ny, int radius, Pixel out[3]) const;
        size_t residentBytes() const;
        int rehydrations() const;

        /** Add an image (copied); returns its slot id (assigned sequentially). */
        int addImage(const uint8_t *rgba, int w, int h, int channels = 4);
        /** As above but TAKES the buffer instead of copying it. A full-resolution frame
         *  is ~100 MB, and a project loader already owns a freshly decoded vector it will
         *  never touch again, so the copy is pure cost on the thread that can least
         *  afford it. Falls back to a copy on the non-threaded build, where the engine
         *  consumes the pixels inline. */
        int addImage(std::vector<uint8_t> &&bytes, int w, int h, int channels = 4);
        /** As above, remembering where the pixels came from so the slot can be re-decoded
         *  after eviction (R-MEM-2). A slot added WITHOUT a path is never evictable-and-
         *  recoverable, so it is simply never re-decoded — which is right for an image
         *  that has no file behind it (a paste, a test fixture). */
        int addImage(std::vector<uint8_t> &&bytes, int w, int h, int channels, std::string sourcePath);
        /** Free a slot's pixels (e.g. removed from the session); its index is never
         *  reused, so every other slot's id stays valid. */
        void releaseImage(int slot);
        /** Drop every image and restart slot-id assignment from 0 — for a full
         *  workspace reset (New/Open/Import). Without this the slot counter keeps
         *  growing while the session's per-slot vectors are cleared, so the next
         *  opened image gets an out-of-range id (the reset-then-open segfault). */
        void reset();
        void setPreviewSize(int maxEdge);
        /** Ask for (or stop asking for) the two INTERMEDIATE histogram taps
         *  (R-PREVIEW-6). Each costs a full statistics pass over every rendered frame and
         *  feeds exactly one panel background, so a front end showing neither should say
         *  so. A benign scalar the engine re-reads on the next render, exactly like
         *  `setPreviewSize` and `setPreferGpu`. Default is both ON. */
        void setWantIntermediateHistograms(bool preCurveLuma, bool preMixerHue);
        /** Opt into GPU-accelerated rendering when a backend is available (else CPU).
         *  A benign scalar the engine re-reads on the next render (like preview size). */
        void setPreferGpu(bool prefer);
        /** True when a platform GPU accelerator exists and is usable (queried once at
         *  construction, so it is safe to read from the UI thread). */
        bool gpuAvailable() const;
        /** Request a preview render of (slot, params); coalesced to the latest request.
         *  `intent` decides whether resolution or latency wins — see RenderIntent. */
        void render(int slot, const EditParams &params, RenderIntent intent = RenderIntent::Final,
                    int explicitLevel = -1);
        /** How long an INTERACTIVE frame may take, in ms (R-PREVIEW-1; 33 = ~30 fps, the
         *  number the product decision set). The level is chosen to fit this from the
         *  worker's own measured cost per megapixel, so the same budget produces level 0
         *  on a desktop and a coarse level on a small board with no configuration
         *  (R-PREVIEW-2). Values below 1 ms are treated as 1. */
        void setInteractiveBudgetMs(double ms);
        double interactiveBudgetMs() const { return mInteractiveBudgetMs; }
        /** The worker's measured cost per megapixel, ms — 0 until the first frame lands.
         *  Exposed so a front end (and a test) can read back the number the level choice
         *  is actually made from, rather than trusting that it exists: R-PREVIEW-6 asks
         *  for the budget to be measurable and this is the measurement. */
        double msPerMegapixel() const { return mMsPerMpx.load(std::memory_order_relaxed); }
        /** The level `intent == Interactive` would pick right now. Pure function of the
         *  measurement above and the budget; safe to call from any thread. */
        int levelForBudget() const;
        /** Pick up the most recent completed preview, if any (moves it out). */
        bool tryAcquire(Frame &out);
        /** Full-resolution render (blocks until done) — for export. */
        bool renderFull(int slot, const EditParams &params, Frame &out);
        /** Preview-size render (blocks until done) — for the before/after baseline,
         *  so it is cheap to draw and matches the live preview's resolution. */
        bool renderPreviewSync(int slot, const EditParams &params, Frame &out);

        bool threaded() const;

    private:
        void doPreview(int slot, const EditParams &params, int maxEdge, RenderIntent intent,
                       int explicitLevel);
        /** Fold a completed frame into the ms-per-megapixel estimate. Ignores frames that
         *  had to re-decode: those measure LibRaw, not the render. */
        void learnCost(const Frame &f);
        /** Re-decode `slot` through the SourceLoader if the engine says it is cold, so the
         *  render that follows has pixels to work with (R-MEM-2). Runs on whichever thread
         *  is about to render. False when the slot is cold and cannot be recovered. */
        bool ensureSource(int slot, bool needFullRes);
        /** D-56: remember / read back a slot's size at ADD time. Both are plain vector
         *  accesses; on the threaded build the caller already holds mMu. */
        void recordSourceSize(int slot, int w, int h);
        bool recordedSourceSize(int slot, int &w, int &h) const;
        mutable EditEngine mEngine;
        int mNextSlot = 0;
        int mPreviewMaxEdge = 1600;
        bool mGpuAvailable = false;  // cached at construction (engine accel availability)
        Frame mReady;          // latest completed preview (both builds)
        bool mFrameReady = false;
        SourceLoader mSourceLoader;
        // Slot -> the file its pixels came from. Written by addImage, read by the worker;
        // both under mMu on the threaded build, so the worker never touches session state.
        std::vector<std::string> mSourcePaths;
        // Slot -> its full-resolution pixel size, recorded when the add is QUEUED. The
        // engine is the wrong place to ask: it does not know until the worker has applied
        // the add, which makes the answer depend on the caller's frame count (D-56).
        std::vector<std::pair<int, int>> mSourceSizes;
        // Snapshots the UI thread may read without taking the render mutex (R-MEM-4).
        // Outside the threads guard because doPreview is shared with the synchronous build.
        std::atomic<size_t> mResidentBytes{0};
        std::atomic<int> mRehydrations{0};
        // R-PREVIEW-2: ONE number, not a per-level table. Cost is very close to linear in
        // pixels (measured: 1600 px 113 ms, 800 px 28, 400 px 7 at one thread), so ms per
        // megapixel transfers between levels — which means a measurement taken at level 2
        // predicts level 0 and the estimate keeps working as the machine speeds up or slows
        // down under load. A per-level table would need every level to be visited before it
        // could choose, and would go stale the moment the CPU budget changed.
        std::atomic<double> mMsPerMpx{0.0};
        double mInteractiveBudgetMs = 33.0;   // ~30 fps; the product decision, 2026-08-24

#ifdef ARSTRO_ENABLE_THREADS
        void workerLoop();
        // `slot` travels with the add so the worker can ask whether this image has a file
        // behind it — which decides whether it may be kept as a proxy alone (R-MEM-5).
        struct AddCmd { std::vector<uint8_t> bytes; int w, h, ch; int slot = -1; };

        std::thread mWorker;
        mutable std::mutex mMu;
        std::condition_variable mCv;
        bool mStop = false;

        std::vector<AddCmd> mAddQueue;   // pending image adds (applied in order)
        std::vector<int> mReleaseQueue;  // pending image releases (applied in order)
        bool mResetEngine = false;       // drop all engine slots before the next adds
        bool mPendingPreview = false;
        int mPendingSlot = -1;
        EditParams mPendingParams;
        // Coalescing keeps the LATEST request, so the intent travels with it: a
        // settle-and-refine request landing on top of a superseded interactive one must
        // still render fine, or the refinement would silently stay coarse (R-PREVIEW-3).
        RenderIntent mPendingIntent = RenderIntent::Final;
        int mPendingLevel = -1;          // >= 0 overrides the intent's choice

        bool mPendingFull = false;       // blocking full-res request
        int mFullSlot = -1;
        EditParams mFullParams;
        bool mFullDone = false;
        bool mFullPreviewOnly = false;   // the blocking request renders at preview size
        Frame mFullResult;
#endif
    };
}
