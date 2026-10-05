/*
 *  interstellar_core — the service's private parts, shared by its two translation units
 *  (InterstellarService.cpp: state and commands; ServiceRender.cpp: frames and jobs). Not installed.
 */
#pragma once
#include "InterstellarService.h"
#include "FrameCache.h"
#include "GradeEngine.h"
#include "Composite.h"
#include "ColourTransform.h"
#include "Lut.h"
#include "volume/Volume.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <thread>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar
{
    /** One media file the timeline reads: its decoder and its lazy volume (R-VOL). */
    struct InterstellarService::Source
    {
        std::unique_ptr<IFrameSource> src;
        IFrameSource::Info info;
        std::unique_ptr<CachedVolume> volume;
        bool ok = false;
        std::string why;
    };

    /** One clip's part of a frame, decided on the UI thread and decoded on any thread: everything a
     *  worker needs is copied in, so it never touches the project (D-5). */
    struct PlanLayer
    {
        std::string media;              // resolved path
        long long frame = 0;            // source frame
        std::string fxType;             // "" | denoise | blend — the first temporal effect (v1)
        int fxRadius = 0;
        double fxStrength = 0.5;
        std::string fxKey;              // part of the cache key
        EditParams params;              // the grade (version overrides and group fold applied)
        bool identity = true;
        // D-7: a group with weight < 1 fades its OWN contribution: the pixels mix between the
        // grade with those groups bypassed (`paramsGroupsOff`) and with them on, by their product.
        bool groupMix = false;
        EditParams paramsGroupsOff;
        double groupWeight = 1.0;
        double weight = 1.0;            // the source's own weight: ungraded → graded
        int edge = 0;                   // grade at this long edge (0 = source size)
        // R-FX-5: the plugins after Cosmo — the source's, then its groups' — and their key
        std::vector<render::EffectRun> effects;
        std::string effectsKey;
        int srcWidth = 0;               // the source's own width: plugin sizes are in its pixels
        // R-COLOR-2: the source's space → the working space, before Cosmo grades (null = identity)
        std::shared_ptr<const render::colour::Transform> input;
        // R-COLOR-5: the source's input LUT, after the transform (null = none) and its version stamp
        std::shared_ptr<const render::Lut> lut;
        std::string lutKey;
        render::Layer layer;            // geometry, fit, opacity, blend, dissolve; src filled at execute
    };

    struct InterstellarService::FramePlan
    {
        int width = 0, height = 0;
        std::vector<PlanLayer> layers;
        std::string key;                // identity of the pixels this plan produces
        // R-COLOR-4: the working space → the picture: the monitor's view (Rec.709), or a render's --output
        std::shared_ptr<const render::colour::Transform> output;
    };

    /** A .cube read once per (path, size, mtime) — plans ask for it every frame (R-COLOR-5). Null and
     *  `err` when it does not read; `stamp` names the version the caches key on. Thread-safe. */
    std::shared_ptr<const render::Lut> loadLut(const std::string &resolvedPath, std::string &err, std::string *stamp = nullptr);

    /** Decoders and a grade engine for ONE thread: the UI thread's (renders, stills) or the
     *  preview worker's. FrameCache is shared — it is thread-safe. */
    struct InterstellarService::RenderCtx
    {
        std::map<std::string, std::unique_ptr<Source>> sources;
        std::unique_ptr<render::GradeEngine> grade{new render::GradeEngine()};
    };

    /** The monitor's worker: the latest plan wins, the last finished frame stays on screen. */
    struct InterstellarService::PreviewWorker
    {
        std::thread thread;
        std::mutex mu;
        std::condition_variable cv;
        std::unique_ptr<FramePlan> pending;
        std::string busyKey, doneKey;
        Raster done;
        bool stop = false;
        bool resetSources = false;      // a new project: drop the worker's decoders before the next job
        std::atomic<unsigned> doneSeq{0};
        unsigned seenSeq = 0;
        RenderCtx ctx;
    };

    /** R-PLAY-2: the read-ahead pool. While playing, the UI thread plans the frames after the
     *  playhead (cheap) and these workers grade them in parallel — each with its OWN decoders and
     *  grade engine (RenderCtx is one thread's) — into a small ring keyed by plan, which the monitor
     *  reads. A frame is a pure function of its plan (R-RENDER-2), so a ring frame IS the frame. */
    struct InterstellarService::AheadPool
    {
        struct Item
        {
            std::string key;
            double t = 0;
            FramePlan plan;
            // R-PLAY-1: the frame is in the preview cache unchanged — decode it from there instead
            std::string cacheFile;
            long long cacheIndex = -1;
            int cacheW = 0, cacheH = 0;
        };
        std::vector<std::thread> threads;
        std::vector<std::unique_ptr<RenderCtx>> ctxs;
        std::vector<char> resetSources;
        std::mutex mu;
        std::condition_variable cv;
        std::deque<Item> queue;
        std::set<std::string> busy;
        std::map<std::string, std::pair<double, Raster>> done;   // key → (t, frame)
        std::set<std::string> doneFromCache;                      // ring keys decoded from the preview cache
        std::vector<std::map<std::string, std::unique_ptr<IFrameSource>>> cacheSrcs;   // per worker: open segments
        bool stop = false;
        std::atomic<long long> finished{0};
        double workMs = 0;              // a frame's work, smoothed — how far ahead to aim
    };

    /** R-PLAY-1: the graded preview cache of the current timeline — one-second H.264 segments of the
     *  frames the monitor shows, at the cache edge, every frame remembered by the hash of its PLAN, so
     *  an edit invalidates exactly the frames whose pixels it changes. The UI thread plans and checks
     *  (planning reads the project); one builder thread grades and encodes. A render never reads it. */
    struct InterstellarService::PreviewCache
    {
        struct Seg
        {
            std::string file;                 // a name inside `dir`
            std::vector<uint64_t> keys;       // per frame of the segment: the hash of its plan at `edge`
            int width = 0, height = 0;        // the frame (the file may be one pixel larger: encoders want even)
        };
        enum State { kNone = 0, kCached = 1, kStale = 2, kBuilding = 3 };
        NodeId timeline;
        std::string dir;                      // <stem>.cache/<timeline id>
        int edge = 0, perSeg = 24;
        double fps = 24.0;
        std::map<long long, Seg> segs;        // the index: segments on disk
        std::vector<int> state;               // per segment, for the timeline's cache bar
        unsigned checkedEpoch = ~0u;          // the project state the pass below checks against
        long long passStart = 0, passDone = 0;
        bool passComplete = false;
        std::deque<long long> todo;           // segments found missing or stale, nearest the playhead first
        unsigned gen = 0;                     // a rebuilt segment is a NEW file: a reader may hold the old one
        std::string failed;                   // the writer refused: stop until a setting or the project changes
        bool loaded = false;
        // the builder thread
        struct Job
        {
            long long seg = 0;
            std::string dir, file;
            std::vector<FramePlan> plans;
            std::vector<uint64_t> keys;
            int width = 0, height = 0;
            double fps = 24.0;
            bool hardware = false;
        };
        struct Result
        {
            long long seg = 0;
            std::string dir, file;
            std::vector<uint64_t> keys;
            int width = 0, height = 0;
            bool ok = false, dropped = false;
            std::string why, note;
        };
        std::thread thread;
        std::mutex mu;
        std::condition_variable cv;
        std::unique_ptr<Job> job;             // handed over, not yet taken
        bool working = false;                 // the builder has a job in hand
        std::deque<Result> results;
        bool stop = false;
        std::atomic<bool> yield{false};       // drop the segment in hand: the user is working
        RenderCtx ctx;
    };

    /** A queued render of a NAMED timeline (R-RENDER-1), advanced a frame per pump (R-RENDER-4). */
    struct InterstellarService::Job
    {
        RenderJobModel model;
        long long first = 0, count = 0, next = 0;
        bool png = false;
        EncodeSpec spec;              // what the writer is asked for (R-RENDER-6)
        int width = 0, height = 0;    // the output frame
        int proxyEdge = 0;            // the render path's long edge for it (0 = the project's size)
        double fps = 24.0;            // the output rate
        std::shared_ptr<const render::colour::Transform> output;   // R-COLOR-4: --output (null = as graded)
        bool begun = false;
        std::unique_ptr<IFrameWriter> writer;
    };

    /** A rack load in flight. `entryRackObj[i]` is the #rackobj bound to the i-th `.cmp` entry. */
    struct InterstellarService::PendingRack
    {
        enum class Kind { Open, Add, Reload } kind = Kind::Open;
        std::vector<NodeId> entryRackObj;            // Open / Reload
        std::vector<std::string> addedPaths;         // Add: as handed to Cosmo
        std::vector<NodeId> addedRackObjs;           // Add: pre-created, in path order
        bool projectOpening = false;                 // an Open that is also the project's open
    };

    /** Everything an undoable edit can change: the `.isp` text, and each bound rack node's own
     *  params and bypass (read from the rack's cache — Cosmo's values, never a second copy kept
     *  as authority: a restore writes them back THROUGH Cosmo). */
    struct InterstellarService::UndoState
    {
        std::string project;
        struct Node { NodeId rackObj; std::string params; bool bypass = false; };
        std::vector<Node> rack;
        std::vector<int> nodeIds;   // the rack's node set — a change means a structural edit
        bool operator==(const UndoState &o) const;
    };

    struct InterstellarService::UndoEntry
    {
        std::string label, key;     // key: consecutive writes to the same addresses coalesce
        double atMs = 0;            // wall clock, for coalescing a slider drag into one step
        UndoState before, after;
    };

    /** "a/b/c.ext" → "c". */
    std::string fileStem(const std::string &path);
    /** Frame-snapped time. */
    double snapToFrame(double t, double fps);
}
}
