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
#include <cstdint>
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
        struct Frame
        {
            std::vector<uint8_t> rgba;  // straight RGBA8
            int width = 0, height = 0;
            HistogramData hist;         // final output histogram
            HistogramData preCurveHist; // luma entering the tone curve
            HueHistogram preMixerHue;   // hue entering the colour mixer
        };

        RenderService();
        ~RenderService();
        RenderService(const RenderService &) = delete;
        RenderService &operator=(const RenderService &) = delete;

        /** Add an image (copied); returns its slot id (assigned sequentially). */
        int addImage(const uint8_t *rgba, int w, int h, int channels = 4);
        /** As above but TAKES the buffer instead of copying it. A full-resolution frame
         *  is ~100 MB, and a project loader already owns a freshly decoded vector it will
         *  never touch again, so the copy is pure cost on the thread that can least
         *  afford it. Falls back to a copy on the non-threaded build, where the engine
         *  consumes the pixels inline. */
        int addImage(std::vector<uint8_t> &&bytes, int w, int h, int channels = 4);
        /** Free a slot's pixels (e.g. removed from the session); its index is never
         *  reused, so every other slot's id stays valid. */
        void releaseImage(int slot);
        /** Drop every image and restart slot-id assignment from 0 — for a full
         *  workspace reset (New/Open/Import). Without this the slot counter keeps
         *  growing while the session's per-slot vectors are cleared, so the next
         *  opened image gets an out-of-range id (the reset-then-open segfault). */
        void reset();
        void setPreviewSize(int maxEdge);
        /** Opt into GPU-accelerated rendering when a backend is available (else CPU).
         *  A benign scalar the engine re-reads on the next render (like preview size). */
        void setPreferGpu(bool prefer);
        /** True when a platform GPU accelerator exists and is usable (queried once at
         *  construction, so it is safe to read from the UI thread). */
        bool gpuAvailable() const;
        /** Request a preview render of (slot, params); coalesced to the latest request. */
        void render(int slot, const EditParams &params);
        /** Pick up the most recent completed preview, if any (moves it out). */
        bool tryAcquire(Frame &out);
        /** Full-resolution render (blocks until done) — for export. */
        bool renderFull(int slot, const EditParams &params, Frame &out);
        /** Preview-size render (blocks until done) — for the before/after baseline,
         *  so it is cheap to draw and matches the live preview's resolution. */
        bool renderPreviewSync(int slot, const EditParams &params, Frame &out);

        bool threaded() const;

    private:
        void doPreview(int slot, const EditParams &params, int maxEdge);
        EditEngine mEngine;
        int mNextSlot = 0;
        int mPreviewMaxEdge = 1600;
        bool mGpuAvailable = false;  // cached at construction (engine accel availability)
        Frame mReady;          // latest completed preview (both builds)
        bool mFrameReady = false;

#ifdef ARSTRO_ENABLE_THREADS
        void workerLoop();
        struct AddCmd { std::vector<uint8_t> bytes; int w, h, ch; };

        std::thread mWorker;
        std::mutex mMu;
        std::condition_variable mCv;
        bool mStop = false;

        std::vector<AddCmd> mAddQueue;   // pending image adds (applied in order)
        std::vector<int> mReleaseQueue;  // pending image releases (applied in order)
        bool mResetEngine = false;       // drop all engine slots before the next adds
        bool mPendingPreview = false;
        int mPendingSlot = -1;
        EditParams mPendingParams;

        bool mPendingFull = false;       // blocking full-res request
        int mFullSlot = -1;
        EditParams mFullParams;
        bool mFullDone = false;
        bool mFullPreviewOnly = false;   // the blocking request renders at preview size
        Frame mFullResult;
#endif
    };
}
