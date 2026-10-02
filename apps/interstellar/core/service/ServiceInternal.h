/*
 *  interstellar_core — the service's private parts, shared by its two translation units
 *  (InterstellarService.cpp: state and commands; ServiceRender.cpp: frames and jobs). Not installed.
 */
#pragma once
#include "InterstellarService.h"
#include "FrameCache.h"
#include "GradeEngine.h"
#include "Composite.h"
#include "volume/Volume.h"
#include <atomic>
#include <condition_variable>
#include <map>
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
        render::Layer layer;            // geometry, fit, opacity, blend, dissolve; src filled at execute
    };

    struct InterstellarService::FramePlan
    {
        int width = 0, height = 0;
        std::vector<PlanLayer> layers;
        std::string key;                // identity of the pixels this plan produces
    };

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
