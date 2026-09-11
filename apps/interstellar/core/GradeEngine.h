/*
 *  interstellar_core — GradeEngine: step 5 of the frame pipeline. Decoded pixels in, graded pixels
 *  out (R-EVAL-2, R-COSMO-1a).
 *
 *  It owns one `arstro::EditEngine` and uses it as a PURE FUNCTION: clear, add this frame, apply
 *  these params, render. The engine is a photo editor's engine and holds slots for a reason, but a
 *  video frame is different pixels every time, so nothing is worth keeping between frames — and
 *  keeping a slot per frame would leak one per frame.
 *
 *  **The identity short-circuit is the difference between usable and not.** `EditEngine` converts
 *  to linear float on ingest — 24.8 MB for a 1080p frame — and at default parameters it then drops
 *  all 17 stages and converts back. So when the rack says "identity" (which is every source until
 *  P3 hosts a real Cosmo project) the decoded bytes are handed straight through and no conversion
 *  happens at all. That is why an ungraded cut scrubs at decode speed.
 *
 *  It is in the core rather than the host because `arstro_image` is portable and codec-free — only
 *  `cosmo_core` drags a toolkit, and that stays behind the `RackAccess` seam in the app layer.
 */
#pragma once
#include "Composite.h"
#include "engine/EditEngine.h"
#include "engine/EditParams.h"
#include <memory>

namespace arstro
{
namespace interstellar
{
    class GradeEngine
    {
    public:
        GradeEngine();
        ~GradeEngine();

        /** Render `in` with `params` into `out`. `longEdge <= 0` renders at full resolution
         *  (export); otherwise the frame is graded at that proxy edge (scrub, playback).
         *
         *  `hasParams == false` means identity: `out` becomes a copy of `in` and the engine is not
         *  touched. Returns false only when `in` is empty. */
        bool render(const Raster &in, const arstro::EditParams &params, bool hasParams,
                    int longEdge, Raster &out);

        /** True when `p` would change nothing, so a caller can take the fast path itself. Compared
         *  through the parameter codec rather than field by field: a hand-written comparison is a
         *  second list of what EditParams contains, and it would go stale the first time a field
         *  was added — which is exactly how a "nothing changed" check starts quietly missing
         *  changes. One string compare per CACHE MISS is nothing beside a decode. */
        static bool isIdentity(const arstro::EditParams &p);

    private:
        std::unique_ptr<arstro::EditEngine> mEngine;
    };
}
}
