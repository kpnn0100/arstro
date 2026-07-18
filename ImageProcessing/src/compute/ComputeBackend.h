/*
 *  Arstro ImageProcessing Library
 *
 *  ComputeBackend: the OPTIONAL accelerator seam for the edit pipeline (e.g. a GPU
 *  backend). It is the image-domain analogue of Artboard's platform rule — the CPU
 *  pipeline in EditEngine is the *reference* and the *guaranteed fallback*; an
 *  accelerator is used only when the user opts in, it is available, and it accepts
 *  the job, and it MUST produce output matching the CPU path.
 *
 *  This header is platform-free (no OS/GPU includes). Concrete backends
 *  (Metal / Vulkan / Direct3D / WebGPU / OpenGL) are added per platform behind the
 *  single factory `createComputeAccelerator()` in a later change — today it returns
 *  nullptr, so the engine is CPU-only with zero behaviour change.
 */
#pragma once
#include "../analysis/Histogram.h"
#include "../base/Image.h"
#include "../engine/EditParams.h"
#include <memory>

namespace arstro
{
    /** The result of an accelerated per-image render: the fully processed, gamma-
     *  encoded straight image (ready to pack to RGBA8) plus the histogram taps the
     *  UI reads at the pipeline boundaries. Mirrors what EditEngine::renderInto
     *  produces on the CPU path. */
    struct ComputeResult
    {
        Image processed;              ///< processed + gamma-encoded (straight RGBA)
        HistogramData finalHist;      ///< output histogram
        HistogramData preCurveHist;   ///< luma entering the tone curve
        HueHistogram preMixerHue;     ///< hue entering the colour mixer
    };

    /** An optional accelerator for the image edit pipeline. Returning false from
     *  process() (or available()==false) makes EditEngine fall back to its CPU
     *  reference path. */
    class IComputeBackend
    {
    public:
        enum class Kind { Cpu, Gpu };

        virtual ~IComputeBackend() = default;

        /** Human-readable name (diagnostics), e.g. "CPU", "Metal", "Vulkan". */
        virtual const char *name() const = 0;
        virtual Kind kind() const = 0;

        /** True if this backend can run on the current device. Must be a
         *  side-effect-free hardware fact, safe to query once at startup. */
        virtual bool available() const = 0;

        /** Render `linearSource` through the FULL edit pipeline for `params`
         *  (global chain + masks + gamma encode), filling `out` (processed image +
         *  histogram taps). Return false to decline the job (engine falls back to
         *  the CPU reference path). When it returns true the output must match the
         *  CPU path within a small tolerance (a hardware backend is not bit-exact in
         *  float) — the software pipeline is the conformance reference. A backend may
         *  also decline (return false) an edit it does not yet fully support. */
        virtual bool process(const Image &linearSource, const EditParams &params, ComputeResult &out) = 0;
    };

    /** The single per-platform extension point. Returns the platform's GPU
     *  accelerator, or nullptr when none is built. TODAY: always nullptr (CPU-only).
     *  A concrete backend is registered here per platform later, e.g.:
     *      #if   defined(__APPLE__)      return std::make_unique<MetalComputeBackend>();
     *      #elif defined(_WIN32)         return std::make_unique<D3DComputeBackend>();
     *      #elif defined(__linux__)      return std::make_unique<VulkanComputeBackend>();
     *      #elif defined(__EMSCRIPTEN__) return std::make_unique<WebGpuComputeBackend>();
     *  Until then the engine renders on the CPU reference path. */
    std::unique_ptr<IComputeBackend> createComputeAccelerator();
}
