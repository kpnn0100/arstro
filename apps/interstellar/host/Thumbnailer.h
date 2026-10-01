/*
 *  interstellar_host — Thumbnailer: the app's optional `thumbnail` hook (app/AppHooks.h).
 *
 *  A Home card's cover and a rack filmstrip cell need a small still of a source at a time. The
 *  model carries no pixels, so the host answers — from a cache, because the hook is called on the
 *  UI thread and a 4K decode there would be a visible stall (R2). Decoding goes through
 *  HostFrameSource (FFmpeg for video, Cosmo's decoder for stills); the downscale is a box filter,
 *  which is right for a thumbnail and costs nothing to reason about.
 */
#pragma once
#include "Raster.h"
#include <map>
#include <string>
#include <tuple>

namespace arstro
{
namespace interstellar_host
{
    class Thumbnailer
    {
    public:
        bool get(const std::string &mediaPath, double t, int edge, interstellar::Raster &out);
        void clear() { mCache.clear(); }

    private:
        std::map<std::tuple<std::string, long long, int>, interstellar::Raster> mCache;
    };

    /** Box-filter `in` so its long edge is at most `edge`. */
    void boxDownscale(const interstellar::Raster &in, int edge, interstellar::Raster &out);
}
}
