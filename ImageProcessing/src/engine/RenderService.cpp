#include "RenderService.h"

namespace arstro
{
#ifdef ARSTRO_ENABLE_THREADS
    // ───────────────────────── threaded ─────────────────────────
    RenderService::RenderService() { mWorker = std::thread([this] { workerLoop(); }); }

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
                mEngine.addImage(a.bytes.data(), a.w, a.h, a.ch);
            for (int s : releases)  // apply queued image releases, in order
                mEngine.releaseImage(s);

            if (doFull)
            {
                mEngine.selectImage(fullSlot);
                mEngine.setCurrentParams(fullParams);
                if (fullPreviewOnly) mEngine.setPreviewSize(maxEdge);
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
        mEngine.selectImage(slot);
        mEngine.setCurrentParams(params);
        PreviewBuffer pb = mEngine.renderPreview();
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
    RenderService::RenderService() {}
    RenderService::~RenderService() {}
    bool RenderService::threaded() const { return false; }

    int RenderService::addImage(const uint8_t *rgba, int w, int h, int channels)
    {
        const int slot = mEngine.addImage(rgba, w, h, channels);
        if (slot >= 0) mNextSlot = slot + 1;
        return slot;
    }
    void RenderService::releaseImage(int slot) { mEngine.releaseImage(slot); }
    void RenderService::reset()
    {
        mEngine.clearImages();
        mNextSlot = 0;
        mFrameReady = false;
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
        mEngine.selectImage(slot);
        mEngine.setCurrentParams(params);
        PreviewBuffer pb = mEngine.renderPreview();
        if (!pb.rgba) return;
        mReady.rgba.assign(pb.rgba, pb.rgba + (size_t)pb.width * pb.height * 4);
        mReady.width = pb.width; mReady.height = pb.height; mReady.hist = mEngine.histogram();
        mReady.preCurveHist = mEngine.preCurveHistogram(); mReady.preMixerHue = mEngine.preMixerHue();
        mFrameReady = true;
    }
#endif
}
