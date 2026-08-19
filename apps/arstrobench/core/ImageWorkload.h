/*
 *  Arstrobench by arstro — the ImageProcessing workload (R-IMG).
 *
 *  Generate a deterministic dummy image, then time it through cosmo's OWN pipeline
 *  (arstro::EditEngine with every adjustment stage engaged). Timing the real pipeline
 *  rather than a synthetic filter chain is what makes the score predictive of how
 *  cosmo will feel on this machine.
 *
 *  Generation and the sRGB->linear conversion happen before the clock starts
 *  (R-IMG-3); the timed region is exactly the renderImage() call.
 *
 *  UI-free: depends only on arstro_image.
 */
#pragma once
#include "Workload.h"
#include "engine/EditParams.h"
#include <cstdint>
#include <string>
#include <vector>

namespace arstro
{
namespace arstrobench
{
    class ImageWorkload
    {
    public:
        // Fixed workload (R-SCORE-4). 1920x1080 is a full HD frame: big enough that the
        // spatial stages dominate scheduler noise, small enough that a pass stays under a
        // second or so on a modern desktop.
        static constexpr int kWidth = 1920;
        static constexpr int kHeight = 1080;
        static constexpr int kPasses = 3;

        /** Defaults are the real workload; the smaller sizes exist for the unit tests
         *  (R-TEST-1), which must not spend seconds proving the plumbing works.
         *  `preferGpu` opts into the engine's accelerator exactly as cosmo's own setting
         *  does (R-IMG-4); the WORK measured is identical either way, so the two scores
         *  are directly comparable. */
        explicit ImageWorkload(int width = kWidth, int height = kHeight, int passes = kPasses,
                               bool preferGpu = false)
            : mWidth(width), mHeight(height), mPasses(passes), mPreferGpu(preferGpu) {}

        bool preferGpu() const { return mPreferGpu; }
        void setPreferGpu(bool prefer) { mPreferGpu = prefer; }

        /** True when this machine HAS a usable GPU accelerator at all. The toggle is
         *  disabled without one, rather than offering a switch that cannot do anything. */
        static bool gpuAvailable();

        /** How a finished run describes the backend that ACTUALLY produced the pixels.
         *  An available accelerator may still decline an edit it does not implement, and
         *  reporting the requested backend instead of the real one would be a lie the
         *  user cannot detect (R-IMG-4a). */
        static std::string backendText(bool preferGpu, bool accelerated, const char *name);

        WorkloadResult run() const;

        /** Deterministic synthetic RGBA8 scene (R-IMG-1): colour ramp + radial falloff +
         *  a high-frequency checker + hash noise. Broadband on purpose — a smooth image
         *  would let noise reduction, clarity, texture and sharpen do unrepresentative work. */
        static std::vector<uint8_t> makeDummyPixels(int width, int height);

        /** Every adjustment stage engaged at a non-neutral value (R-IMG-2), geometry 1:1
         *  (R-IMG-2a). Public so a test can assert the set is actually non-neutral. */
        static EditParams benchParams();

        /** Number of adjustment stages this workload engages — for the card's detail line. */
        static int activeStageCount();

        /** One line naming the work this instance will do. The card shows it before the
         *  run as well as after, so it lives here rather than only on the result. */
        std::string describe() const;

    private:
        int mWidth, mHeight, mPasses;
        bool mPreferGpu;
    };
}
}
