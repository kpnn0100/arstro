/*
 *  interstellar_render — GradeEngine: the grade step. Decoded pixels in, graded pixels out — the
 *  seventeen Cosmo stages applied per frame, unchanged (R-FX-1).
 *
 *  It owns one `arstro::EditEngine` and uses it as a PURE FUNCTION per frame: this frame, these
 *  params, this edge -> pixels. Nothing about one frame survives into the next except reusable
 *  scratch memory, so the output depends only on (frame, params, longEdge), which is what
 *  R-RENDER-2 asks of a render.
 *
 *  **The identity short-circuit is the difference between usable and not.** EditEngine converts
 *  the frame to linear float on ingest (33 MB for a 1080p RGBA frame), and at default parameters
 *  it then drops all seventeen stages and converts back — to produce the input. So when the params
 *  are identity, the decoded bytes are copied straight through and the engine is never touched.
 *  The identity output keeps the INPUT size even when a proxy edge was asked for: the composite's
 *  Fit absorbs the size, and a downscale would cost more than it saves for a frame nobody graded.
 *
 *  **Which engine entry point, and why it is not the slot API.** The obvious sequence —
 *  clearImages, addImage, selectImage, applyParams, then renderFull or setPreviewSize+renderPreview
 *  — is the photo editor's, and `renderFull` is its one-shot EXPORT path: it frees every scratch
 *  buffer on the way out so a single photo export does not park memory. Per video frame that
 *  meant re-faulting ~200 MB, and measured end to end it was 58-64 ms per 1080p frame against
 *  33-35 ms for the same stages through `renderImage`, which EditEngine documents as "the seam a
 *  video editor reuses frame after frame" and which keeps its scratch. The two produce byte-identical pixels at full and at
 *  proxy size — a test pins that against the slot sequence, so the faster path can never quietly
 *  become a different grade. NOTES.md has the numbers.
 *
 *  The cost is that the scratch stays resident between frames (~200 MB measured at 1080p for a
 *  typical grade, roughly 4x at UHD). Peak memory is the same either way — `renderFull` reaches the same
 *  peak every frame and gives it back after — so the difference is only what is parked while idle,
 *  and `releaseScratch()` gives that back when playback or an export stops.
 *
 *  The two intermediate histogram taps (pre-curve luma, pre-mixer hue) are switched off: each is a
 *  full pass over the frame that exists only to draw a photo editor's panel background.
 *
 *  Not thread-safe: one GradeEngine per render thread.
 */
#pragma once
#include "Raster.h"
#include "engine/EditParams.h"
#include <memory>

namespace arstro
{
class EditEngine;

namespace interstellar
{
namespace render
{
    class GradeEngine
    {
    public:
        GradeEngine();
        ~GradeEngine();
        GradeEngine(const GradeEngine &) = delete;
        GradeEngine &operator=(const GradeEngine &) = delete;

        /** Grade `in` with `p` into `out`. `longEdge <= 0` renders at full resolution (export);
         *  otherwise at that proxy long edge (scrub, playback) — never upscaled. `hasParams ==
         *  false` means identity. Returns false only when `in` is empty or malformed. If the engine
         *  declines to render (it never has), `out` is the ungraded input rather than nothing, and
         *  the call still returns true — a master must not drop a frame. */
        bool render(const Raster &in, const arstro::EditParams &p, bool hasParams, int longEdge, Raster &out);
        // A DEEP `in` (R-COLOR-1) is graded deep: 16-bit in, the engine's float result packed to
        // 16-bit out — never through its RGBA8 bytes. Identity copies, as above.

        /** Free the engine and its scratch; the next non-identity render builds a new one. For a
         *  host that stops playing or exporting and does not want ~200 MB parked meanwhile. */
        void releaseScratch();
        /** The engine's GPU opt-in (EditEngine::setPreferGpu): takes effect only where a backend
         *  is available, and the CPU path stays the reference (R-RENDER-5 is held on CPU). */
        void setPreferGpu(bool prefer);
        bool gpuAvailable();
        /** The last render that reached the engine ran on the GPU backend (R-GPU-1) — false after an
         *  identity frame, a CPU render or a declined job. */
        bool lastAccelerated() const { return mLastAccelerated; }

        /** True when `p` would change nothing. Compared through the parameter codec rather than
         *  field by field, for the same reason ParamHash is: a hand-written comparison is a second
         *  list of what EditParams contains, and it would quietly start missing changes the first
         *  time a field was added. */
        static bool isIdentity(const arstro::EditParams &p);

    private:
        void ensureEngine();
        bool renderDeep(const Raster &in, const arstro::EditParams &p, bool hasParams, int longEdge, Raster &out);
        bool mLastAccelerated = false;
        std::unique_ptr<arstro::EditEngine> mEngine;
        bool mPreferGpu = false;
    };
}
}
}
