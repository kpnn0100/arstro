/*
 *  interstellar_core — the service's private parts, shared by its two translation units
 *  (InterstellarService.cpp: state and commands; ServiceRender.cpp: frames and jobs). Not installed.
 */
#pragma once
#include "InterstellarService.h"
#include "FrameCache.h"
#include "GradeEngine.h"
#include "volume/Volume.h"
#include <memory>
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

    /** A queued render of a NAMED timeline (R-RENDER-1), advanced a frame per pump (R-RENDER-4). */
    struct InterstellarService::Job
    {
        RenderJobModel model;
        long long first = 0, count = 0, next = 0;
        bool png = false;
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

    /** "a/b/c.ext" → "c". */
    std::string fileStem(const std::string &path);
    /** Frame-snapped time. */
    double snapToFrame(double t, double fps);
}
}
