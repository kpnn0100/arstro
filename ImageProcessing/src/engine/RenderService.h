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
        /** Free a slot's pixels (e.g. removed from the session); its index is never
         *  reused, so every other slot's id stays valid. */
        void releaseImage(int slot);
        void setPreviewSize(int maxEdge);
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
