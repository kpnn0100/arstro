/*
 *  interstellar_core — the graded preview cache (R-PLAY-1).
 *
 *  Measured first (DR-PLAY-2): decoding a frame costs 4–11 ms, grading it 170–220 ms. So what is
 *  cached is the GRADED frame — the current timeline as the monitor shows it, at the cache edge, as
 *  one-second H.264 segments under `<stem>.cache/<timeline>/`. Every frame of a segment is
 *  remembered by the hash of its PLAN: the plan names everything that changes the pixels (sources,
 *  frames, grades, effects, geometry, size), so "is this cached frame still the frame?" is one hash
 *  compare, and an edit invalidates exactly the frames it touches.
 *
 *  The UI thread plans and checks — planning reads the project and the rack — and hands one segment
 *  at a time to the builder thread, which grades (its own decoders and engine) and encodes through
 *  the host's writer (R-SCOPE-3), on the video unit when Hardware video is on (R-PLAY-3). It builds
 *  when the user has stopped for a moment and drops the segment in hand the moment they start
 *  again. A rebuilt segment is a new file, so a playback worker holding the old one is never handed
 *  a half-written stream. Read only while PLAYING (`scheduleAhead`); a paused frame, a render and
 *  an export are always graded.
 */
#include "ServiceInternal.h"
#include "Project.h"
#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace arstro
{
namespace interstellar
{
    using EK = Event::Kind;

    namespace
    {
        constexpr double kIdleMs = 1500.0;       // the user has stopped: build
        constexpr double kCheckBudgetMs = 6.0;   // planning per pump on the UI thread, at most
        constexpr int kQuality = 20;             // a preview, not a deliverable
        const char *const kMagic = "interstellar-preview-cache 1";

        uint64_t keyHash(const std::string &k)
        {
            uint64_t h = 1469598103934665603ull;   // FNV-1a: stable across runs and builds, unlike std::hash
            for (unsigned char ch : k) { h ^= ch; h *= 1099511628211ull; }
            return h ? h : 1;
        }

        std::string hex(uint64_t v)
        {
            char b[17];
            std::snprintf(b, sizeof b, "%016" PRIx64, v);
            return b;
        }

        /** Encoders want even sizes: repeat the last column/row; the reader crops it off again. */
        void padEven(const Raster &in, Raster &out)
        {
            const int w = in.width + (in.width & 1), h = in.height + (in.height & 1);
            out.allocate(w, h);
            for (int y = 0; y < h; ++y)
            {
                const int sy = std::min(y, in.height - 1);
                for (int x = 0; x < w; ++x)
                {
                    const int sx = std::min(x, in.width - 1);
                    std::copy_n(&in.rgba[((size_t)sy * in.width + sx) * 4], 4, &out.rgba[((size_t)y * w + x) * 4]);
                }
            }
        }
    }

    int InterstellarService::cacheEdge() const
    {
        // the playing picture's best size (R-PLAY-2 steps 1280 → 960 → 640), capped by Preview quality
        const int cap = mSettings.previewEdge > 0 ? mSettings.previewEdge : 1600;
        return std::min(cap, 1280);
    }

    bool InterstellarService::cacheLookup(long long frame, const FramePlan &plan, std::string &file, long long &index, int &w, int &h) const
    {
        if (!mPCache || !mPCache->loaded || mPCache->timeline != currentTimeline() || mPCache->edge != cacheEdge()) return false;
        const PreviewCache &c = *mPCache;
        const auto it = c.segs.find(frame / c.perSeg);
        if (it == c.segs.end()) return false;
        const long long i = frame % c.perSeg;
        if (i >= (long long)it->second.keys.size() || it->second.keys[(size_t)i] != keyHash(plan.key)) return false;
        file = c.dir + "/" + it->second.file;
        index = i;
        w = it->second.width;
        h = it->second.height;
        return true;
    }

    void InterstellarService::stopCache()
    {
        if (!mPCache) return;
        mPCache->yield = true;
        {
            std::lock_guard<std::mutex> l(mPCache->mu);
            mPCache->stop = true;
        }
        mPCache->cv.notify_all();
        if (mPCache->thread.joinable()) mPCache->thread.join();
        mPCache.reset();
    }

    void InterstellarService::cacheLoop()
    {
        PreviewCache &c = *mPCache;
        for (;;)
        {
            std::unique_ptr<PreviewCache::Job> job;
            {
                std::unique_lock<std::mutex> l(c.mu);
                c.cv.wait(l, [&] { return c.stop || c.job; });
                if (c.stop) return;
                job = std::move(c.job);
                c.working = true;
                c.yield = false;
            }
            PreviewCache::Result r;
            r.seg = job->seg;
            r.dir = job->dir;
            r.file = job->file;
            r.keys = job->keys;
            r.width = job->width;
            r.height = job->height;
            const std::string path = job->dir + "/" + job->file;
            std::unique_ptr<IFrameWriter> w = mHost.frameWriter ? mHost.frameWriter() : nullptr;
            EncodeSpec spec;
            spec.codec = "h264";
            spec.quality = kQuality;
            spec.speed = "veryfast";
            spec.hardware = job->hardware;
            if (!w) r.why = "no video writer installed";
            else if (!w->begin(path, job->width + (job->width & 1), job->height + (job->height & 1), job->fps, (long long)job->plans.size(), spec))
                r.why = "the H.264 writer refused " + path;
            else
            {
                r.note = w->note();
                bool ok = true;
                for (const auto &plan : job->plans)
                {
                    if (c.yield) { r.dropped = true; break; }
                    Raster f, even;
                    // remember=false: a cache frame is never asked for again at this size; keep the
                    // frame cache for what the user is grading
                    if (!executePlan(c.ctx, plan, f, false) || f.width != job->width || f.height != job->height)
                    {
                        ok = false;
                        r.why = "a frame could not be made";
                        break;
                    }
                    const Raster *src = &f;
                    if ((f.width & 1) || (f.height & 1)) { padEven(f, even); src = &even; }
                    if (!w->write(*src)) { ok = false; r.why = "the H.264 writer failed on " + path; break; }
                }
                const bool ended = w->end();
                r.ok = ok && !r.dropped && ended;
                if (ok && !r.dropped && !ended) r.why = "the H.264 writer could not finish " + path;
            }
            if (!r.ok)
            {
                std::error_code ec;
                fs::remove(path, ec);
            }
            {
                std::lock_guard<std::mutex> l(c.mu);
                c.working = false;
                c.results.push_back(std::move(r));
            }
        }
    }

    void InterstellarService::pumpPreviewCache()
    {
        PreviewCache *pc = mPCache.get();
        // 1. what the builder finished: into the index (on disk too: it outlives the session)
        bool changed = false;
        if (pc)
        {
            std::deque<PreviewCache::Result> done;
            {
                std::lock_guard<std::mutex> l(pc->mu);
                done.swap(pc->results);
            }
            for (auto &r : done)
            {
                if (r.dir != pc->dir) continue;   // a timeline left behind
                if (!r.ok)
                {
                    if (!r.dropped)
                    {
                        pc->failed = r.why.empty() ? "the writer refused" : r.why;
                        emit(Event(EK::Info).with("text", "preview cache stopped: " + pc->failed));
                    }
                    if (r.seg < (long long)pc->state.size()) pc->state[(size_t)r.seg] = pc->segs.count(r.seg) ? PreviewCache::kStale : PreviewCache::kNone;
                    pc->todo.push_front(r.seg);   // it is still wanted
                    continue;
                }
                const auto old = pc->segs.find(r.seg);
                if (old != pc->segs.end() && old->second.file != r.file)
                {
                    std::error_code ec;
                    fs::remove(pc->dir + "/" + old->second.file, ec);   // an open reader keeps its handle
                }
                pc->segs[r.seg] = PreviewCache::Seg{r.file, r.keys, r.width, r.height};
                if (r.seg < (long long)pc->state.size()) pc->state[(size_t)r.seg] = PreviewCache::kCached;
                if (!r.note.empty()) emit(Event(EK::Info).with("text", "preview cache: " + r.note));
                changed = true;
            }
            if (changed)
            {
                std::ofstream f(pc->dir + "/index", std::ios::trunc);
                f << kMagic << " edge=" << pc->edge << " fps=" << canonicalNumber(pc->fps) << " perSeg=" << pc->perSeg << "\n";
                for (const auto &kv : pc->segs)
                {
                    f << kv.first << ' ' << kv.second.file << ' ' << kv.second.width << ' ' << kv.second.height;
                    for (uint64_t k : kv.second.keys) f << ' ' << hex(k);
                    f << '\n';
                }
            }
        }

        // 2. is it wanted? A window builds when idle; `cache build` builds in any host, now.
        const bool want = mOpen && !mIspPath.empty() && (mCacheForced || (mSettings.previewCache && mHost.asyncPreview));
        if (!want)
        {
            if (pc) pc->yield = true;
            if (changed) refreshModel();
            return;
        }
        if (!pc)
        {
            mPCache.reset(new PreviewCache());
            pc = mPCache.get();
            pc->thread = std::thread([this] { cacheLoop(); });
        }
        PreviewCache &c = *pc;

        // 3. the timeline, size and rate it is FOR — a change opens another index
        const NodeId tl = currentTimeline();
        const double fps = mProject->fps > 0 ? mProject->fps : 24.0;
        const fs::path isp(mIspPath);
        const std::string dir = (isp.parent_path() / (isp.stem().string() + ".cache") / tl).string();
        if (!c.loaded || c.dir != dir || c.edge != cacheEdge() || c.fps != fps)
        {
            c.yield = true;
            c.timeline = tl;
            c.dir = dir;
            c.edge = cacheEdge();
            c.fps = fps;
            c.perSeg = std::max(1, (int)std::lround(fps));
            c.segs.clear();
            c.state.clear();
            c.todo.clear();
            c.failed.clear();
            c.checkedEpoch = ~0u;
            c.loaded = true;
            std::error_code ec;
            fs::create_directories(dir, ec);
            std::ifstream f(dir + "/index");
            std::string line;
            if (std::getline(f, line) && line.rfind(kMagic, 0) == 0)
            {
                std::istringstream h(line.substr(std::string(kMagic).size()));
                std::string e, r, p;
                h >> e >> r >> p;
                if (e == "edge=" + std::to_string(c.edge) && r == "fps=" + canonicalNumber(fps) && p == "perSeg=" + std::to_string(c.perSeg))
                    while (std::getline(f, line))
                    {
                        std::istringstream s(line);
                        long long n = -1;
                        PreviewCache::Seg seg;
                        std::string k;
                        if (!(s >> n >> seg.file >> seg.width >> seg.height) || n < 0) continue;
                        while (s >> k) seg.keys.push_back(std::strtoull(k.c_str(), nullptr, 16));
                        unsigned g = 0;
                        if (std::sscanf(seg.file.c_str(), "seg_%*d_%u", &g) == 1) c.gen = std::max(c.gen, g + 1);
                        if (fs::exists(dir + "/" + seg.file)) c.segs[n] = std::move(seg);   // a file removed by hand is not cached
                    }
            }
        }
        if (!c.failed.empty()) { if (mCacheForced) { mCacheForced = false; refreshModel(); } return; }

        // 4. idle? Building takes cores; the user working, playing or rendering comes first.
        bool rendering = false;
        for (const auto &j : mJobs) rendering = rendering || j->model.state == "running" || j->model.state == "queued";
        // a rack still loading is not the picture yet: its frames would be cached under the wrong plans
        const bool idle = !mPending && (mCacheForced || (!mPlaying && !rendering && mNowMs - mLastCommandMs >= kIdleMs));
        bool inFlight;
        {
            std::lock_guard<std::mutex> l(c.mu);
            inFlight = c.job || c.working;
        }
        if (!idle)
        {
            if (inFlight) c.yield = true;
            if (changed) refreshModel();
            return;
        }

        // 5. check: plan each segment's frames and compare hashes, nearest the playhead first
        const double dur = timelineDuration(tl);
        const long long frames = dur > 0 ? (long long)std::ceil(dur * fps - 1e-6) : 0;
        const long long nseg = (frames + c.perSeg - 1) / c.perSeg;
        if ((long long)c.state.size() != nseg) c.state.assign((size_t)nseg, PreviewCache::kNone);
        auto planSeg = [&](long long n, std::vector<FramePlan> &plans, std::vector<uint64_t> &keys) {
            const long long first = n * c.perSeg, last = std::min(frames, first + c.perSeg);
            for (long long f = first; f < last; ++f)
            {
                FramePlan p;
                if (!planFrame(tl, snapToFrame(f / fps, fps), c.edge, p, nullptr)) return false;
                keys.push_back(keyHash(p.key));
                plans.push_back(std::move(p));
            }
            return !plans.empty();
        };
        auto upToDate = [&](long long n, const std::vector<uint64_t> &keys) {
            const auto it = c.segs.find(n);
            return it != c.segs.end() && it->second.keys == keys;
        };
        if (c.checkedEpoch != mEpoch)
        {
            c.checkedEpoch = mEpoch;
            c.passStart = nseg > 0 ? std::min(nseg - 1, (long long)std::floor(mModel.playhead * fps) / c.perSeg) : 0;
            c.passDone = 0;
            c.passComplete = false;
            c.todo.clear();
            for (auto &s : c.state) if (s == PreviewCache::kCached || s == PreviewCache::kBuilding) s = PreviewCache::kStale;
            changed = true;
        }
        const auto t0 = std::chrono::steady_clock::now();
        while (!c.passComplete && c.passDone < nseg &&
               std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() < kCheckBudgetMs)
        {
            const long long n = (c.passStart + c.passDone) % nseg;
            ++c.passDone;
            std::vector<FramePlan> plans;
            std::vector<uint64_t> keys;
            if (!planSeg(n, plans, keys)) continue;
            const bool ok = upToDate(n, keys);
            c.state[(size_t)n] = ok ? PreviewCache::kCached : c.segs.count(n) ? PreviewCache::kStale : PreviewCache::kNone;
            if (!ok) c.todo.push_back(n);
            changed = true;
        }
        if (c.passDone >= nseg) c.passComplete = true;

        // 6. build: one segment at a time, re-planned now (it may have changed since it was checked)
        if (!inFlight)
            while (!c.todo.empty())
            {
                const long long n = c.todo.front();
                c.todo.pop_front();
                if (n >= nseg) continue;
                std::unique_ptr<PreviewCache::Job> job(new PreviewCache::Job());
                if (!planSeg(n, job->plans, job->keys)) continue;
                if (upToDate(n, job->keys)) { c.state[(size_t)n] = PreviewCache::kCached; changed = true; continue; }
                job->seg = n;
                job->dir = c.dir;
                job->file = "seg_" + std::to_string(n) + "_" + std::to_string(c.gen++) + ".mp4";
                job->width = job->plans.front().width;
                job->height = job->plans.front().height;
                job->fps = fps;
                job->hardware = mSettings.hardwareVideo;
                c.state[(size_t)n] = PreviewCache::kBuilding;
                {
                    std::lock_guard<std::mutex> l(c.mu);
                    c.job = std::move(job);
                }
                c.cv.notify_one();
                inFlight = true;
                changed = true;
                break;
            }
        if (mCacheForced && c.passComplete && c.todo.empty() && !inFlight)
        {
            mCacheForced = false;   // `cache build` is done: every frame of the timeline is cached and current
            changed = true;
        }
        if (changed)
        {
            refreshModel();
            long long cached = 0;
            for (long long n = 0; n < nseg; ++n)
                if (c.state[(size_t)n] == PreviewCache::kCached) cached += std::min(frames, (n + 1) * c.perSeg) - n * c.perSeg;
            emit(Event(EK::CacheChanged).with("timeline", tl).with("frames", (int)cached).with("total", (int)frames)
                     .with("building", inFlight));
        }
    }

    void InterstellarService::fillCacheModel(AppModel &m) const
    {
        m.playbackFromCache = mPlaying && mLastFromCache;
        m.previewCacheFrames = m.previewCacheTotal = 0;
        m.previewCacheBuilding = false;
        m.previewCacheSegments.clear();
        if (!mPCache || !mOpen || mPCache->timeline != currentTimeline()) return;
        const PreviewCache &c = *mPCache;
        const double dur = timelineDuration(c.timeline);
        const long long frames = dur > 0 ? (long long)std::ceil(dur * c.fps - 1e-6) : 0;
        m.previewCacheTotal = (int)frames;
        m.previewCacheSegmentSeconds = c.perSeg / c.fps;
        m.previewCacheSegments.assign(c.state.begin(), c.state.end());
        for (size_t n = 0; n < c.state.size(); ++n)
        {
            if (c.state[n] == PreviewCache::kBuilding) m.previewCacheBuilding = true;
            if (c.state[n] == PreviewCache::kCached)
                m.previewCacheFrames += (int)(std::min(frames, (long long)(n + 1) * c.perSeg) - (long long)n * c.perSeg);
        }
    }

    bool InterstellarService::cacheCommand(const Command &c)
    {
        if (c.kind == Command::Kind::CacheBuild)
        {
            mCacheForced = true;
            if (mPCache) mPCache->failed.clear();
            mOutput = "building the preview cache of " + currentTimeline() + "\n";
            return true;
        }
        // cache clear: the current timeline's segments and index — the next idle moment rebuilds
        const fs::path isp(mIspPath);
        const std::string dir = (isp.parent_path() / (isp.stem().string() + ".cache") / currentTimeline()).string();
        stopCache();
        std::error_code ec;
        const auto n = fs::remove_all(dir, ec);
        mCacheForced = false;
        mOutput = "cleared " + std::to_string(n > 0 ? n - 1 : 0) + " cache files of " + currentTimeline() + "\n";
        return true;
    }
}
}
