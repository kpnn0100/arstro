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
        mWorkers = budget.beginLoad();   // the ONE conversion of the user's percentage (R-SVC-10)

        mPipe.start(
            mEntries.size(), mWorkers, kDecodeWindow, kMaxInFlightBytes,
            [this, makeDecoder](std::size_t i) {
                // Stateless and per-call, as before: a decoder instance is free next to a
                // RAW decode, and per-call is provably per-thread without a thread_local.
                auto dec = makeDecoder ? makeDecoder() : nullptr;
                mBudget->producerEnter();          // measured peak, not assumed (R-CPU-4)
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
        DecodedImage img = dec->decodeFile(e.imagePath);
        if (!img.ok()) return r;           // decoded stays false: reads as missing, not as a stall
        r.w = img.width;
        r.h = img.height;
        r.decoded = true;
        // Downsample HERE, on the worker, not on the UI thread (R-LOADPERF-2).
        r.thumb = EditSession::makeThumb(img.rgba.data(), img.width, img.height, EditSession::kThumbEdge);
        r.rgba = std::move(img.rgba);
        return r;
    }

    bool ProjectLoader::poll(Result &out)
    {
        if (!mPipe.tryConsume(out)) return false;
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
    }

    void ProjectLoader::releaseBudget()
    {
        if (!mBudget) return;
        mBudget->endLoad();
        mBudget = nullptr;
    }
}
}
