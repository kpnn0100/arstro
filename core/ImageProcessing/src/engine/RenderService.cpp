#include "RenderService.h"

namespace arstro
{
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
        AddCmd c;
        c.bytes.assign(rgba, rgba + (size_t)w * h * channels);
        c.w = w; c.h = h; c.ch = channels;
        mAddQueue.push_back(std::move(c));
        mCv.notify_all();
        return mNextSlot++;  // engine assigns the same id (queue is FIFO)
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
        AddCmd c;
        c.bytes = std::move(bytes);   // the whole point: no ~100 MB copy
        c.w = w; c.h = h; c.ch = channels;
        mAddQueue.push_back(std::move(c));
        const int slot = mNextSlot++;
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
        if (!loader(path, rgba, w, h)) return false;
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

    void RenderService::render(int slot, const EditParams &params)
    {
        std::lock_guard<std::mutex> lk(mMu);
        mPendingPreview = true;
        mPendingSlot = slot;
        mPendingParams = params;
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
                if (mPendingPreview) { doPrev = true; slot = mPendingSlot; params = mPendingParams; mPendingPreview = false; }
            }

            if (doReset)  // full workspace reset: drop all slots so the next add is slot 0
                mEngine.clearImages();
            for (auto &a : adds)  // apply queued image adds, in order
            {
                mEngine.addImage(a.bytes.data(), a.w, a.h, a.ch);
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
                doPreview(slot, params, maxEdge);
        }
    }

    void RenderService::doPreview(int slot, const EditParams &params, int maxEdge)
    {
        mEngine.setPreviewSize(maxEdge);
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
        if (!mSourceLoader(path, rgba, w, h)) return false;
        return mEngine.supplySource(slot, rgba.data(), w, h, 4);
    }
    void RenderService::releaseImage(int slot) { mEngine.releaseImage(slot); }
    void RenderService::reset()
    {
        mEngine.clearImages();
        mNextSlot = 0;
        mFrameReady = false;
        mSourcePaths.clear();
    }
    void RenderService::setPreviewSize(int maxEdge)
    {
        mPreviewMaxEdge = maxEdge < 1 ? 1 : maxEdge;
        mEngine.setPreviewSize(mPreviewMaxEdge);
    }
    void RenderService::render(int slot, const EditParams &params) { doPreview(slot, params, mPreviewMaxEdge); }

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
        mFrameReady = true;
    }
#endif

    // ── build-independent: GPU-preference relay (mirrors setPreviewSize — a plain
    //    engine scalar the worker re-reads on the next render; no queueing needed) ──
    void RenderService::setPreferGpu(bool prefer) { mEngine.setPreferGpu(prefer); }
    bool RenderService::gpuAvailable() const { return mGpuAvailable; }
}
