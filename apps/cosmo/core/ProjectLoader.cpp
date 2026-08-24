#include "ProjectLoader.h"

namespace arstro
{
namespace cosmo
{
    namespace
    {
        std::string baseName(const std::string &p)
        {
            const auto slash = p.find_last_of("/\\");
            return slash == std::string::npos ? p : p.substr(slash + 1);
        }
    }

    void ProjectLoader::start(std::vector<EditSession::WorkspaceEntry> entries, ThreadBudget &budget,
                              DecoderFactory makeDecoder, std::function<void()> onWorkerStart)
    {
        stop();                       // never two loads at once (R-CPU-3: a pool is sized when it starts)
        mEntries = std::move(entries);
        if (mEntries.empty()) return;

        mBudget = &budget;
        mFraction.assign(mEntries.size(), 0.0);
        mWorkers = budget.beginLoad();   // the ONE conversion of the user's percentage (R-SVC-10)

        mPipe.start(
            mEntries.size(), mWorkers, kDecodeWindow, kMaxInFlightBytes,
            [this, makeDecoder](std::size_t i) {
                // Stateless and per-call, as before: a decoder instance is free next to a
                // RAW decode, and per-call is provably per-thread without a thread_local.
                auto dec = makeDecoder ? makeDecoder() : nullptr;
                if (dec)
                {
                    // Coalesced on the way in: LibRaw calls back many times per second per
                    // worker, and a UI that pumps at 60 Hz wants the newest value per entry,
                    // not every one of them (D-24).
                    dec->setProgress([this, i](double f, const char *stage) {
                        std::lock_guard<std::mutex> lk(mStartedMu);
                        if (i < mFraction.size()) mFraction[i] = f;
                        for (auto &e : mProgressQueue)
                            if (e.index == i) { e.fraction = f; e.stage = stage ? stage : ""; return; }
                        mProgressQueue.push_back({i, f, stage ? stage : ""});
                    });
                }
                mBudget->producerEnter();          // measured peak, not assumed (R-CPU-4)
                {
                    // R-LOADUX-4: the claim is the earliest honest signal there is. A RAW decode
                    // reports no internal progress, so "a worker has started entry i" is the only
                    // thing cosmo can say during the seconds it takes.
                    std::lock_guard<std::mutex> lk(mStartedMu);
                    mStartedQueue.push_back(i);
                    ++mStartedCount;
                }
                Result r = produce(i, dec.get());
                mBudget->producerExit();
                return r;
            },
            [](const Result &r) { return r.rgba.size(); },
            std::move(onWorkerStart));
    }

    ProjectLoader::Result ProjectLoader::produce(std::size_t i, IImageDecoder *dec) const
    {
        const EditSession::WorkspaceEntry &e = mEntries[i];
        Result r;
        r.index = i;
        r.group = e.group;
        r.parent = e.parent;
        r.params = e.params;
        r.history = e.history;
        r.bypass = e.bypass;               // R-BYPASS-6
        if (e.group)
        {
            r.name = e.name;
            return r;
        }
        r.imagePath = e.imagePath;
        r.name = baseName(e.imagePath);
        if (!dec) return r;                // no decoder supplied: every leaf reads as missing
        // D-24: a load produces pixels that are downscaled to previewEdge before anyone sees
        // them, so it asks for the cheap demosaic. Export re-decodes at full fidelity — which
        // R-MEM-2's architecture already forces, since the load keeps no full-resolution
        // source and `renderFull` goes back to the file regardless.
        DecodedImage img = dec->decodeFile(e.imagePath, Fidelity::Preview);
        if (!img.ok()) return r;           // decoded stays false: reads as missing, not as a stall
        r.w = img.width;
        r.h = img.height;
        r.decoded = true;
        // Downsample HERE, on the worker, not on the UI thread (R-LOADPERF-2).
        r.thumb = EditSession::makeThumb(img.rgba.data(), img.width, img.height, EditSession::kThumbEdge);
        r.rgba = std::move(img.rgba);
        return r;
    }

    void ProjectLoader::drainProgress(std::vector<EntryProgress> &out)
    {
        std::lock_guard<std::mutex> lk(mStartedMu);
        out.insert(out.end(), mProgressQueue.begin(), mProgressQueue.end());
        mProgressQueue.clear();
    }

    double ProjectLoader::partial() const
    {
        std::lock_guard<std::mutex> lk(mStartedMu);
        double sum = 0.0;
        for (std::size_t i = mPipe.consumed(); i < mFraction.size(); ++i) sum += mFraction[i];
        return sum;
    }

    void ProjectLoader::drainStarted(std::vector<std::size_t> &out)
    {
        std::lock_guard<std::mutex> lk(mStartedMu);
        out.insert(out.end(), mStartedQueue.begin(), mStartedQueue.end());
        mStartedQueue.clear();
    }

    std::size_t ProjectLoader::started() const
    {
        std::lock_guard<std::mutex> lk(mStartedMu);
        return mStartedCount;
    }

    bool ProjectLoader::poll(Result &out)
    {
        if (!mPipe.tryConsume(out)) return false;
        {
            std::lock_guard<std::mutex> lk(mStartedMu);
            if (out.index < mFraction.size()) mFraction[out.index] = 1.0;
        }
        // The last result out is the end of the load: hand the engine its threads back
        // immediately rather than waiting for the host to call stop() (R-SVC-10).
        if (mPipe.finished()) releaseBudget();
        return true;
    }

    void ProjectLoader::stop()
    {
        mPipe.stop();
        releaseBudget();
        mEntries.clear();
        mWorkers = 0;
        {
            std::lock_guard<std::mutex> lk(mStartedMu);
            mStartedQueue.clear();
            mStartedCount = 0;
            mProgressQueue.clear();
            mFraction.clear();
        }
    }

    void ProjectLoader::releaseBudget()
    {
        if (!mBudget) return;
        mBudget->endLoad();
        mBudget = nullptr;
    }
}
}
