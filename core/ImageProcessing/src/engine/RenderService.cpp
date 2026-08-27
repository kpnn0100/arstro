#include "RenderService.h"
#include "../base/ColorSpace.h"   // color::takeNonFiniteCount (D-48)

namespace arstro
{
    // ── R-PREVIEW-1/2: the latency budget, and the level that fits it ────────────────
    //
    // The engine holds no opinion about how fast a frame should be; it renders the level
    // it is told to and reports what that cost. This is where the deciding happens, and it
    // is deliberately tiny: ONE measured number (ms per megapixel), ONE budget, and a
    // walk from the finest level down until the prediction fits.
    //
    // Cost is very close to linear in pixels — measured 113 / 28 / 7 ms at 1600 / 800 / 400
    // px on one thread — so a measurement taken at ANY level predicts every other one, and
    // the estimate keeps working as the machine changes speed (thermal throttling, a load
    // running concurrently, R-CPU shrinking the engine's share). A per-level table would
    // need every level visited before it could choose, and would go stale on each of those
    // events.

    void RenderService::setInteractiveBudgetMs(double ms)
    {
        mInteractiveBudgetMs = ms < 1.0 ? 1.0 : ms;
    }

    int RenderService::levelForBudget() const
    {
        const double perMpx = mMsPerMpx.load(std::memory_order_relaxed);
        // No measurement yet -> level 0. Optimistic on purpose: a desktop should never
        // render coarse, and the cost of being wrong on a slow board is exactly ONE slow
        // frame at the start of the first gesture, after which the measurement exists.
        // Starting pessimistic would instead make every fast machine begin every session
        // blurry, which is a permanent cost to avoid a transient one.
        if (perMpx <= 0.0) return 0;
        const int edge0 = mPreviewMaxEdge < 1 ? 1 : mPreviewMaxEdge;
        for (int l = 0; l < EditEngine::previewLevels(); ++l)
        {
            const int e = std::max(1, edge0 >> l);
            // Megapixels of a 3:2 frame at this edge. The exact aspect is unknown here and
            // does not matter: it is the same factor at every level, so it cancels out of
            // the comparison the moment the first real frame updates perMpx.
            const double mpx = (double)e * (double)e * (2.0 / 3.0) / 1e6;
            if (perMpx * mpx <= mInteractiveBudgetMs) return l;
        }
        return EditEngine::previewLevels() - 1;   // nothing fits: the coarsest is the best offer
    }

    void RenderService::learnCost(const Frame &f)
    {
        // A frame that had to RE-DECODE its photo is not a measurement of rendering — it is
        // a measurement of LibRaw, and folding a ~1 s decode into ms-per-megapixel would
        // drive every subsequent gesture to the coarsest level for no reason (D-44's cold
        // hops are exactly this shape). Skip them.
        if (f.rehydrated || f.width <= 0 || f.height <= 0 || f.ms <= 0.0) return;
        const double mpx = (double)f.width * (double)f.height / 1e6;
        if (mpx <= 0.0) return;
        const double sample = f.ms / mpx;
        const double prev = mMsPerMpx.load(std::memory_order_relaxed);
        // An exponential average, not the last sample: one frame that lost its slice to the
        // decode pool or to another application must not drop the whole session to a coarse
        // level. 0.3 settles within a few frames of a drag while still riding out a spike.
        const double next = prev <= 0.0 ? sample : prev * 0.7 + sample * 0.3;
        mMsPerMpx.store(next, std::memory_order_relaxed);
    }

    // ── shared by both builds: the size a slot was added with (D-56) ─────────────────────
    void RenderService::recordSourceSize(int slot, int w, int h)
    {
        if (slot < 0) return;
        if ((int)mSourceSizes.size() <= slot) mSourceSizes.resize(slot + 1, {0, 0});
        mSourceSizes[(std::size_t)slot] = {w, h};
    }

    bool RenderService::recordedSourceSize(int slot, int &w, int &h) const
    {
        if (slot < 0 || slot >= (int)mSourceSizes.size()) return false;
        const auto &wh = mSourceSizes[(std::size_t)slot];
        if (wh.first <= 0 || wh.second <= 0) return false;
        w = wh.first;
        h = wh.second;
        return true;
    }

#ifdef ARSTRO_ENABLE_THREADS
    // ───────────────────────── threaded ─────────────────────────
    RenderService::RenderService()
    {
        mGpuAvailable = mEngine.gpuAvailable();  // query before the worker starts (single-threaded here)
        mWorker = std::thread([this] { workerLoop(); });
    }

    RenderService::~RenderService()
    {
        {
            std::lock_guard<std::mutex> lk(mMu);
            mStop = true;
        }
        mCv.notify_all();
        if (mWorker.joinable())
            mWorker.join();
    }

    bool RenderService::threaded() const { return true; }

    int RenderService::addImage(const uint8_t *rgba, int w, int h, int channels)
    {
        if (!rgba || w <= 0 || h <= 0 || channels < 1)
            return -1;
        std::lock_guard<std::mutex> lk(mMu);
        const int slot = mNextSlot++;   // engine assigns the same id (queue is FIFO)
        AddCmd c;
        c.bytes.assign(rgba, rgba + (size_t)w * h * channels);
        c.w = w; c.h = h; c.ch = channels; c.slot = slot;
        mAddQueue.push_back(std::move(c));
        recordSourceSize(slot, w, h);   // D-56: known now, not when the worker gets to it
        mCv.notify_all();
        return slot;
    }

    int RenderService::addImage(std::vector<uint8_t> &&bytes, int w, int h, int channels)
    {
        return addImage(std::move(bytes), w, h, channels, std::string());
    }

    int RenderService::addImage(std::vector<uint8_t> &&bytes, int w, int h, int channels,
                                std::string sourcePath)
    {
        if (w <= 0 || h <= 0 || channels < 1) return -1;
        if (bytes.size() < (size_t)w * h * channels) return -1;
        std::lock_guard<std::mutex> lk(mMu);
        const int slot = mNextSlot++;
        AddCmd c;
        c.bytes = std::move(bytes);   // the whole point: no ~100 MB copy
        c.w = w; c.h = h; c.ch = channels; c.slot = slot;
        mAddQueue.push_back(std::move(c));
        recordSourceSize(slot, w, h);   // D-56
        // The path travels with the slot so an evicted slot can be re-decoded without the
        // worker ever reading session state (R-MEM-2).
        if ((int)mSourcePaths.size() <= slot) mSourcePaths.resize(slot + 1);
        mSourcePaths[slot] = std::move(sourcePath);
        mCv.notify_all();
        return slot;
    }

    void RenderService::setSourceLoader(SourceLoader loader)
    {
        std::lock_guard<std::mutex> lk(mMu);
        mSourceLoader = std::move(loader);
    }

    void RenderService::setMemoryCaps(size_t sourceBytes, size_t proxyBytes)
    {
        // Applied on the calling thread's behalf by the worker would need another queue;
        // the caps are two scalars the engine only reads inside touch*LRU, and they are set
        // once at startup and on a settings change, so this is the same benign-scalar rule
        // setPreviewSize already follows.
        std::lock_guard<std::mutex> lk(mMu);
        mEngine.setMemoryCaps(sourceBytes, proxyBytes);
    }

    size_t RenderService::residentBytes() const { return mResidentBytes.load(); }
    int RenderService::rehydrations() const { return mRehydrations.load(); }

    bool RenderService::ensureSource(int slot, bool needFullRes)
    {
        const bool cold = needFullRes ? !mEngine.slotHasSource(slot) : mEngine.slotNeedsSource(slot);
        if (!cold) return true;

        std::string path;
        SourceLoader loader;
        {
            std::lock_guard<std::mutex> lk(mMu);
            loader = mSourceLoader;
            if (slot >= 0 && slot < (int)mSourcePaths.size()) path = mSourcePaths[slot];
        }
        // No path and no loader is not an error: an image with no file behind it (a paste,
        // a test fixture) was simply never evictable-and-recoverable, and the engine will
        // render from whatever it still holds.
        if (!loader || path.empty()) return !needFullRes && !mEngine.slotNeedsSource(slot);

        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        // D-24: only pixels that will be WRITTEN need the expensive demosaic; a cold slot
        // being rehydrated for a preview is about to be downscaled to previewEdge anyway.
        if (!loader(path, needFullRes, rgba, w, h)) return false;
        return mEngine.supplySource(slot, rgba.data(), w, h, 4);
    }

    void RenderService::releaseImage(int slot)
    {
        std::lock_guard<std::mutex> lk(mMu);
        mReleaseQueue.push_back(slot);
        mCv.notify_all();
    }

    void RenderService::reset()
    {
        std::lock_guard<std::mutex> lk(mMu);
        // Discard queued work for the workspace being torn down and restart id
        // assignment; the worker drops all engine slots before applying new adds
        // (so the next addImage() → slot 0 matches the freshly-cleared vectors).
        mAddQueue.clear();
        mReleaseQueue.clear();
        mSourcePaths.clear();
        mSourceSizes.clear();
        mResetEngine = true;
        mNextSlot = 0;
        mFrameReady = false;
        mPendingPreview = false;
        mCv.notify_all();
    }

    void RenderService::setPreviewSize(int maxEdge)
    {
        std::lock_guard<std::mutex> lk(mMu);
        mPreviewMaxEdge = maxEdge < 1 ? 1 : maxEdge;
        mEngine.setPreviewSize(mPreviewMaxEdge);  // value is plain; safe to set, worker re-reads via render
    }

    void RenderService::setWantIntermediateHistograms(bool preCurveLuma, bool preMixerHue)
    {
        std::lock_guard<std::mutex> lk(mMu);
        mEngine.setWantIntermediateHistograms(preCurveLuma, preMixerHue);
    }

    bool RenderService::sourceSize(int slot, int &w, int &h) const
    {
        std::lock_guard<std::mutex> lk(mMu);
        if (recordedSourceSize(slot, w, h)) return true;
        return mEngine.sourceSize(slot, w, h);
    }

    bool RenderService::sampleSourceLinear(int slot, double nx, double ny, int radius,
                                          Pixel out[3]) const
    {
        std::lock_guard<std::mutex> lk(mMu);
        return mEngine.sampleSourceLinear(slot, nx, ny, radius, out);
    }

    void RenderService::render(int slot, const EditParams &params, RenderIntent intent, int explicitLevel)
    {
        std::lock_guard<std::mutex> lk(mMu);
        mPendingPreview = true;
        mPendingSlot = slot;
        mPendingParams = params;
        mPendingLevel = explicitLevel;
        // Coalescing keeps the LATEST request, and that has to include the intent: a
        // settle-and-refine request arriving on top of a superseded interactive one must
        // render fine, or the refinement would silently stay coarse (R-PREVIEW-3).
        mPendingIntent = intent;
        mCv.notify_all();
    }

    bool RenderService::tryAcquire(Frame &out)
    {
        std::lock_guard<std::mutex> lk(mMu);
        if (!mFrameReady)
            return false;
        out = std::move(mReady);
        mFrameReady = false;
        return true;
    }

    bool RenderService::renderFull(int slot, const EditParams &params, Frame &out)
    {
        std::unique_lock<std::mutex> lk(mMu);
        mPendingFull = true;
        mFullSlot = slot;
        mFullParams = params;
        mFullPreviewOnly = false;
        mFullDone = false;
        mCv.notify_all();
        mCv.wait(lk, [this] { return mFullDone || mStop; });
        if (!mFullDone)
            return false;
        out = std::move(mFullResult);
        return true;
    }

    bool RenderService::renderPreviewSync(int slot, const EditParams &params, Frame &out)
    {
        std::unique_lock<std::mutex> lk(mMu);
        mPendingFull = true;
        mFullSlot = slot;
        mFullParams = params;
        mFullPreviewOnly = true;   // render at preview size, not full-res
        mFullDone = false;
        mCv.notify_all();
        mCv.wait(lk, [this] { return mFullDone || mStop; });
        if (!mFullDone)
            return false;
        out = std::move(mFullResult);
        return true;
    }

    void RenderService::workerLoop()
    {
        for (;;)
        {
            std::vector<AddCmd> adds;
            std::vector<int> releases;
            bool doPrev = false, doFull = false, fullPreviewOnly = false, doReset = false;
            int slot = -1, fullSlot = -1, maxEdge = 1600;
            RenderIntent intent = RenderIntent::Final;
            int explicitLevel = -1;
            EditParams params, fullParams;
            {
                std::unique_lock<std::mutex> lk(mMu);
                mCv.wait(lk, [this] {
                    return mStop || mResetEngine || !mAddQueue.empty() || !mReleaseQueue.empty() || mPendingPreview || mPendingFull;
                });
                if (mStop)
                    return;
                doReset = mResetEngine; mResetEngine = false;
                adds.swap(mAddQueue);
                releases.swap(mReleaseQueue);
                maxEdge = mPreviewMaxEdge;
                if (mPendingFull) { doFull = true; fullSlot = mFullSlot; fullParams = mFullParams; fullPreviewOnly = mFullPreviewOnly; mPendingFull = false; }
                if (mPendingPreview) { doPrev = true; slot = mPendingSlot; params = mPendingParams; intent = mPendingIntent; explicitLevel = mPendingLevel; mPendingPreview = false; }
            }

            if (doReset)  // full workspace reset: drop all slots so the next add is slot 0
                mEngine.clearImages();
            for (auto &a : adds)  // apply queued image adds, in order
            {
                // R-MEM-5 / D-44: an image that knows where it came from is added PREVIEW ONLY —
                // its proxy is built here, in one fused pass off the encoded bytes, and no
                // 387 MB linear-float source is ever materialised. That is what fills the browse
                // cache during the load; before it, the cache was filled only by the user
                // happening to visit a photo, so the first hop to each of them paid a fresh
                // ~1 s LibRaw decode. An image with no path behind it cannot be re-decoded, so it
                // keeps its source and takes the old path.
                const bool recoverable = a.slot >= 0 && a.slot < (int)mSourcePaths.size() &&
                                         !mSourcePaths[a.slot].empty();
                mEngine.setPreviewSize(maxEdge);
                if (recoverable) mEngine.addImagePreviewOnly(a.bytes.data(), a.w, a.h, a.ch);
                else             mEngine.addImage(a.bytes.data(), a.w, a.h, a.ch);
                a.bytes = std::vector<uint8_t>();   // ~100 MB of encoded pixels, done with
            }
            for (int s : releases)  // apply queued image releases, in order
                mEngine.releaseImage(s);
            if (!adds.empty() || !releases.empty())
            {
                // R-MEM-4: a load is mostly adds and no renders, so publishing here is what
                // makes the number live WHILE the memory is being taken rather than only
                // after the first edit.
                mResidentBytes.store(mEngine.residentBytes());
                mRehydrations.store(mEngine.rehydrations());
            }

            if (doFull)
            {
                // R-MEM-2: a slot whose pixels were evicted is re-decoded here, on this
                // worker, before the render that needs them. Export asks for full
                // resolution, which a proxy cannot stand in for, so the two cases differ.
                if (fullPreviewOnly) mEngine.setPreviewSize(maxEdge);
                ensureSource(fullSlot, /*needFullRes=*/!fullPreviewOnly);
                mEngine.selectImage(fullSlot);
                mEngine.setCurrentParams(fullParams);
                // A blocking request is never a live gesture — the before/after baseline and
                // the export both want the real thing — so level 0, explicitly.
                if (fullPreviewOnly) mEngine.setPreviewLevel(0);
                PreviewBuffer pb = fullPreviewOnly ? mEngine.renderPreview() : mEngine.renderFull();
                Frame f;
                if (pb.rgba) { f.rgba.assign(pb.rgba, pb.rgba + (size_t)pb.width * pb.height * 4); f.width = pb.width; f.height = pb.height; f.hist = mEngine.histogram(); f.preCurveHist = mEngine.preCurveHistogram(); f.preMixerHue = mEngine.preMixerHue(); }
                {
                    std::lock_guard<std::mutex> lk(mMu);
                    mFullResult = std::move(f);
                    mFullDone = true;
                }
                mCv.notify_all();
            }
            if (doPrev)
                doPreview(slot, params, maxEdge, intent, explicitLevel);
        }
    }

    void RenderService::doPreview(int slot, const EditParams &params, int maxEdge, RenderIntent intent,
                                  int explicitLevel)
    {
        const auto t0 = std::chrono::steady_clock::now();
        const int rehyBefore = mEngine.rehydrations();
        mEngine.setPreviewSize(maxEdge);
        // R-PREVIEW-1/2: a gesture in flight gets the finest level that fits the budget;
        // anything else gets level 0. Chosen BEFORE ensureSource, because the level decides
        // which pyramid level counts as "cold" and therefore whether a re-decode is needed
        // at all — a coarse request on a partially evicted photo can often be served.
        // An explicit level wins over the intent: that is how the settle-and-refine walk
        // asks for ONE step up rather than a jump to level 0 (R-PREVIEW-3).
        const int level = explicitLevel >= 0 ? explicitLevel
                                             : (intent == RenderIntent::Interactive ? levelForBudget() : 0);
        mEngine.setPreviewLevel(level);
        // Before selecting: slotNeedsSource is answered against the preview size that is
        // about to be used, so a proxy built for a different size counts as cold.
        ensureSource(slot, /*needFullRes=*/false);
        mEngine.selectImage(slot);
        mEngine.setCurrentParams(params);
        PreviewBuffer pb = mEngine.renderPreview();
        // R-MEM-4: publish what the pools actually hold, every render, so the model and
        // the log report a measured number rather than the cap we hoped they respected.
        mResidentBytes.store(mEngine.residentBytes());
        mRehydrations.store(mEngine.rehydrations());
        if (!pb.rgba)
            return;
        Frame f;
        f.rgba.assign(pb.rgba, pb.rgba + (size_t)pb.width * pb.height * 4);
        f.width = pb.width; f.height = pb.height;
        f.hist = mEngine.histogram();
        f.preCurveHist = mEngine.preCurveHistogram();
        f.preMixerHue = mEngine.preMixerHue();
        f.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        f.rehydrated = mEngine.rehydrations() > rehyBefore;
        f.level = mEngine.previewLevel();
        f.levelEdge = mEngine.previewLevelEdge(f.level);
        // Read-and-reset, so the number belongs to THIS frame. Taken after the render and
        // before the frame is published, on the worker that did the work (D-48).
        f.nonFinite = color::takeNonFiniteCount();
        learnCost(f);
        {
            std::lock_guard<std::mutex> lk(mMu);
            mReady = std::move(f);
            mFrameReady = true;
        }
    }

#else
    // ───────────────────────── synchronous (no threads, e.g. web) ─────────────────────────
    RenderService::RenderService() { mGpuAvailable = mEngine.gpuAvailable(); }
    RenderService::~RenderService() {}
    bool RenderService::threaded() const { return false; }

    int RenderService::addImage(const uint8_t *rgba, int w, int h, int channels)
    {
        const int slot = mEngine.addImage(rgba, w, h, channels);
        if (slot >= 0) mNextSlot = slot + 1;
        recordSourceSize(slot, w, h);   // D-56: one owner of the answer on both builds
        return slot;
    }
    int RenderService::addImage(std::vector<uint8_t> &&bytes, int w, int h, int channels)
    {
        return addImage(std::move(bytes), w, h, channels, std::string());
    }
    int RenderService::addImage(std::vector<uint8_t> &&bytes, int w, int h, int channels,
                                std::string sourcePath)
    {
        // No worker to hand ownership to: the engine consumes the pixels inline here,
        // so there is nothing to move into and this is just the copying path.
        if (bytes.size() < (size_t)w * h * channels) return -1;
        const int slot = addImage(bytes.data(), w, h, channels);
        if (slot >= 0)
        {
            if ((int)mSourcePaths.size() <= slot) mSourcePaths.resize(slot + 1);
            mSourcePaths[slot] = std::move(sourcePath);
        }
        return slot;
    }
    void RenderService::setSourceLoader(SourceLoader loader) { mSourceLoader = std::move(loader); }
    void RenderService::setMemoryCaps(size_t sourceBytes, size_t proxyBytes)
    {
        mEngine.setMemoryCaps(sourceBytes, proxyBytes);
    }
    size_t RenderService::residentBytes() const { return mResidentBytes.load(); }
    int RenderService::rehydrations() const { return mRehydrations.load(); }
    bool RenderService::ensureSource(int slot, bool needFullRes)
    {
        const bool cold = needFullRes ? !mEngine.slotHasSource(slot) : mEngine.slotNeedsSource(slot);
        if (!cold) return true;
        std::string path;
        if (slot >= 0 && slot < (int)mSourcePaths.size()) path = mSourcePaths[slot];
        if (!mSourceLoader || path.empty()) return !needFullRes && !mEngine.slotNeedsSource(slot);
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        // D-24: only a render that will be WRITTEN needs the expensive demosaic. A cold slot
        // being rehydrated for a preview is about to be downscaled to previewEdge, so it asks
        // for the cheap one — which is the same trade the load makes, applied to the one path
        // that reaches back to the file after the load is over.
        if (!mSourceLoader(path, needFullRes, rgba, w, h)) return false;
        return mEngine.supplySource(slot, rgba.data(), w, h, 4);
    }
    void RenderService::releaseImage(int slot) { mEngine.releaseImage(slot); }
    void RenderService::reset()
    {
        mEngine.clearImages();
        mNextSlot = 0;
        mFrameReady = false;
        mSourcePaths.clear();
        mSourceSizes.clear();
    }
    void RenderService::setPreviewSize(int maxEdge)
    {
        mPreviewMaxEdge = maxEdge < 1 ? 1 : maxEdge;
        mEngine.setPreviewSize(mPreviewMaxEdge);
    }
    void RenderService::setWantIntermediateHistograms(bool preCurveLuma, bool preMixerHue)
    {
        mEngine.setWantIntermediateHistograms(preCurveLuma, preMixerHue);
    }
    bool RenderService::sourceSize(int slot, int &w, int &h) const
    {
        if (recordedSourceSize(slot, w, h)) return true;
        return mEngine.sourceSize(slot, w, h);
    }
    bool RenderService::sampleSourceLinear(int slot, double nx, double ny, int radius,
                                          Pixel out[3]) const
    {
        return mEngine.sampleSourceLinear(slot, nx, ny, radius, out);
    }
    void RenderService::render(int slot, const EditParams &params, RenderIntent intent, int explicitLevel)
    {
        doPreview(slot, params, mPreviewMaxEdge, intent, explicitLevel);
    }

    bool RenderService::tryAcquire(Frame &out)
    {
        if (!mFrameReady) return false;
        out = std::move(mReady);
        mFrameReady = false;
        return true;
    }
    bool RenderService::renderFull(int slot, const EditParams &params, Frame &out)
    {
        ensureSource(slot, /*needFullRes=*/true);   // R-MEM-2
        mEngine.selectImage(slot);
        mEngine.setCurrentParams(params);
        PreviewBuffer pb = mEngine.renderFull();
        if (!pb.rgba) return false;
        out.rgba.assign(pb.rgba, pb.rgba + (size_t)pb.width * pb.height * 4);
        out.width = pb.width; out.height = pb.height; out.hist = mEngine.histogram();
        out.preCurveHist = mEngine.preCurveHistogram(); out.preMixerHue = mEngine.preMixerHue();
        return true;
    }
    bool RenderService::renderPreviewSync(int slot, const EditParams &params, Frame &out)
    {
        mEngine.setPreviewSize(mPreviewMaxEdge);
        ensureSource(slot, /*needFullRes=*/false);   // R-MEM-2
        mEngine.selectImage(slot);
        mEngine.setCurrentParams(params);
        PreviewBuffer pb = mEngine.renderPreview();
        if (!pb.rgba) return false;
        out.rgba.assign(pb.rgba, pb.rgba + (size_t)pb.width * pb.height * 4);
        out.width = pb.width; out.height = pb.height; out.hist = mEngine.histogram();
        out.preCurveHist = mEngine.preCurveHistogram(); out.preMixerHue = mEngine.preMixerHue();
        return true;
    }
    void RenderService::doPreview(int slot, const EditParams &params, int maxEdge)
    {
        const auto t0 = std::chrono::steady_clock::now();
        const int rehyBefore = mEngine.rehydrations();
        mEngine.setPreviewSize(maxEdge);
        ensureSource(slot, /*needFullRes=*/false);   // R-MEM-2
        mEngine.selectImage(slot);
        mEngine.setCurrentParams(params);
        PreviewBuffer pb = mEngine.renderPreview();
        mResidentBytes.store(mEngine.residentBytes());   // R-MEM-4
        mRehydrations.store(mEngine.rehydrations());
        if (!pb.rgba) return;
        mReady.rgba.assign(pb.rgba, pb.rgba + (size_t)pb.width * pb.height * 4);
        mReady.width = pb.width; mReady.height = pb.height; mReady.hist = mEngine.histogram();
        mReady.preCurveHist = mEngine.preCurveHistogram(); mReady.preMixerHue = mEngine.preMixerHue();
        mReady.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        mReady.rehydrated = mEngine.rehydrations() > rehyBefore;
        mFrameReady = true;
    }
#endif

    // ── build-independent: GPU-preference relay (mirrors setPreviewSize — a plain
    //    engine scalar the worker re-reads on the next render; no queueing needed) ──
    void RenderService::setPreferGpu(bool prefer) { mEngine.setPreferGpu(prefer); }
    bool RenderService::gpuAvailable() const { return mGpuAvailable; }
}
