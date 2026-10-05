/*
 *  Arstro ImageProcessing Library
 *
 *  ComputeBackend: the OPTIONAL accelerator seam for the edit pipeline (e.g. a GPU
 *  backend). It is the image-domain analogue of Artboard's platform rule — the CPU
 *  pipeline in EditEngine is the *reference* and the *guaranteed fallback*; an
 *  accelerator is used only when the user opts in, it is available, and it accepts
 *  the job, and it MUST produce output matching the CPU path.
 *
 *  This header is platform-free (no OS/GPU includes). Concrete backends (OpenGL and
 *  OpenGL ES today; Metal / Vulkan / Direct3D / WebGPU later) are registered per
 *  platform behind the single factory `createComputeAccelerator()`, which picks among
 *  them at run time.
 */
#pragma once
#include "../analysis/Histogram.h"
#include "../base/Image.h"
#include "../engine/EditParams.h"
#include <memory>
#include <mutex>
#include <vector>

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
        /** Whether the two INTERMEDIATE histogram taps are wanted (EditEngine forwards its own
         *  switch). A backend that runs several stages may skip reading them back when not;
         *  `ComputeResult::preCurveHist`/`preMixerHue` are then left empty. Default: wanted. */
        virtual void setWantIntermediateTaps(bool preCurve, bool preMixer) { (void)preCurve; (void)preMixer; }
    };

    /** Several built backends, one of which is chosen at RUN time: the first whose
     *  available() is true, in the order given (R-GPU-7). Being able to COMPILE a backend
     *  says nothing about whether the driver runs it — an RK3588 builds desktop GL and GLES,
     *  and its Mali driver runs only GLES — so the choice cannot be an #if.
     *
     *  The choice is made lazily, on the first call to any member, and exactly once:
     *  constructing an EditEngine must not create a GPU context. With nothing available it
     *  reports available()==false and the engine stays on the CPU, byte-identical. */
    class SelectingComputeBackend : public IComputeBackend
    {
    public:
        explicit SelectingComputeBackend(std::vector<std::unique_ptr<IComputeBackend>> candidates);

        const char *name() const override;
        Kind kind() const override;
        bool available() const override { return chosen() != nullptr; }
        bool process(const Image &linearSource, const EditParams &params, ComputeResult &out) override;
        void setWantIntermediateTaps(bool preCurve, bool preMixer) override
        {
            for (auto &c : mCandidates) if (c) c->setWantIntermediateTaps(preCurve, preMixer);
        }

    private:
        IComputeBackend *chosen() const;

        std::vector<std::unique_ptr<IComputeBackend>> mCandidates;
        mutable std::once_flag mOnce;
        mutable IComputeBackend *mChosen = nullptr;
    };

    /** The single per-platform extension point. Returns the platform's GPU accelerator —
     *  every backend this build compiled, behind a SelectingComputeBackend when there is
     *  more than one — or nullptr when none is built (the web build), so the engine
     *  renders on the CPU reference path. Other APIs (Metal / Vulkan / D3D / WebGPU) are
     *  added here as further candidates. */
    std::unique_ptr<IComputeBackend> createComputeAccelerator();
}
