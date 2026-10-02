/*
 *  interstellar_host — Thumbnailer: the app's optional `thumbnail` hook (app/AppHooks.h).
 *
 *  A Home card's cover, a rack filmstrip cell and the Grade deck's reference strip need small stills
 *  of a source. The hook is called on the UI thread, and a decode is not a UI-thread cost: in a
 *  capture's long GOP one exact still is ~120 ms at 1080p and the strip wants a dozen (D-5, D-6:
 *  the first Edit frame took 973 ms). So, asynchronously:
 *
 *    - `get` answers from the cache, or queues the request and returns false at once;
 *    - one worker thread decodes queued requests with ONE persistent decoder per file (a strip's
 *      requests arrive in increasing time and decode forward), box-filters to the asked edge, and
 *      bumps `epoch()`;
 *    - the app re-asks for what it is missing when the epoch moves (AppHooks::thumbnailEpoch).
 *
 *  A failed decode is cached as empty too, so a missing file is not re-probed every frame.
 *  `Mode::Sync` keeps the old blocking behaviour for harnesses that want a still immediately.
 */
#pragma once
#include "FrameSource.h"
#include "Raster.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <tuple>

namespace arstro
{
namespace interstellar_host
{
    class Thumbnailer
    {
    public:
        enum class Mode { Async, Sync };
        explicit Thumbnailer(Mode mode = Mode::Async);
        ~Thumbnailer();
        Thumbnailer(const Thumbnailer &) = delete;
        Thumbnailer &operator=(const Thumbnailer &) = delete;

        /** True with the still when it is ready; otherwise false (queued in Async mode). */
        bool get(const std::string &mediaPath, double t, int edge, interstellar::Raster &out);
        /** Rises each time a queued still lands. */
        unsigned epoch() const { return mEpoch.load(); }
        /** Block until the queue is empty or `ms` passes — for harnesses and tests. */
        bool waitIdle(int ms);
        void clear();

    private:
        using Key = std::tuple<std::string, long long, int>;   // path, t in ms, edge
        void worker();
        bool decode(const Key &k, interstellar::Raster &out);

        Mode mMode;
        std::mutex mMu;
        std::condition_variable mCv, mIdleCv;
        std::deque<Key> mQueue;
        std::set<Key> mQueued;
        std::map<Key, interstellar::Raster> mCache;
        std::atomic<unsigned> mEpoch{0};
        bool mStop = false, mBusy = false;
        std::thread mThread;
        // Worker-owned (or caller-owned in Sync mode): one decoder per file, kept warm.
        std::map<std::string, std::unique_ptr<interstellar::IFrameSource>> mSources;
        std::map<std::string, interstellar::IFrameSource::Info> mInfo;
    };

    /** Box-filter `in` so its long edge is at most `edge`. */
    void boxDownscale(const interstellar::Raster &in, int edge, interstellar::Raster &out);
}
}
