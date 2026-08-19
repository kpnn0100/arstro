#include "ImageWorkload.h"
#include "engine/EditEngine.h"
#include <chrono>
#include <cmath>
#include <string>

namespace arstro
{
namespace arstrobench
{
    namespace
    {
        // A cheap integer hash — deterministic per pixel on every platform (no <random>,
        // whose engines are portable but whose distributions are not).
        inline uint32_t hash32(uint32_t v)
        {
            v ^= v >> 16; v *= 0x7feb352dU;
            v ^= v >> 15; v *= 0x846ca68bU;
            v ^= v >> 16;
            return v;
        }

        inline uint8_t clamp8(double v)
        {
            return (uint8_t)(v < 0.0 ? 0.0 : (v > 255.0 ? 255.0 : v));
        }

        /** Fold the rendered bytes into one number (R-SCORE-6). Sampled with a prime
         *  stride: enough of the buffer to catch any change, cheap enough that it does
         *  not matter that it runs between passes. Never inside the timed region. */
        double foldChecksum(const uint8_t *rgba, size_t bytes)
        {
            uint64_t acc = 1469598103934665603ULL;
            for (size_t i = 0; i < bytes; i += 4093)
            {
                acc ^= rgba[i];
                acc *= 1099511628211ULL;
            }
            return (double)(acc & 0xFFFFFFFFULL);
        }
    }

    std::vector<uint8_t> ImageWorkload::makeDummyPixels(int width, int height)
    {
        std::vector<uint8_t> px((size_t)width * height * 4, 255);
        const double cx = width * 0.5, cy = height * 0.5;
        const double maxR = std::sqrt(cx * cx + cy * cy);
        for (int y = 0; y < height; ++y)
        {
            const double fy = (double)y / (height > 1 ? height - 1 : 1);
            for (int x = 0; x < width; ++x)
            {
                const double fx = (double)x / (width > 1 ? width - 1 : 1);
                // Radial falloff: gives the vignette/dehaze/tone stages a real luminance
                // gradient to work on instead of a flat field.
                const double dx = x - cx, dy = y - cy;
                const double fall = 1.0 - 0.55 * (std::sqrt(dx * dx + dy * dy) / maxR);
                // High-frequency checker + hash noise: the broadband content the spatial
                // stages (NR, texture, clarity, sharpen) actually cost time on (R-IMG-1).
                const double checker = (((x >> 3) ^ (y >> 3)) & 1) ? 1.06 : 0.94;
                const uint32_t n = hash32((uint32_t)(y * 73856093 ^ x * 19349663));
                const double noise = ((double)(n & 0xFF) - 128.0) * 0.09;

                const double base = 255.0 * fall * checker;
                px[((size_t)y * width + x) * 4 + 0] = clamp8(base * (0.25 + 0.70 * fx) + noise);
                px[((size_t)y * width + x) * 4 + 1] = clamp8(base * (0.30 + 0.60 * fy) + noise * 0.7);
                px[((size_t)y * width + x) * 4 + 2] = clamp8(base * (0.85 - 0.55 * fx * fy) + noise * 1.3);
                px[((size_t)y * width + x) * 4 + 3] = 255;
            }
        }
        return px;
    }

    EditParams ImageWorkload::benchParams()
    {
        EditParams p;
        // Tone
        p.exposure = 0.45f; p.contrast = 22.0f;
        p.highlights = -35.0f; p.shadows = 30.0f; p.whites = 12.0f; p.blacks = -14.0f;
        // White balance + presence
        p.temp = 7200.0f; p.tint = 8.0f;
        p.vibrance = 28.0f; p.saturation = 10.0f;
        p.texture = 30.0f; p.clarity = 25.0f;
        // Effects
        p.dehaze = 20.0f; p.grainAmount = 18.0f; p.grainSize = 40.0f;
        // Detail — the spatial stages, the most expensive part of the pipeline
        p.sharpenAmount = 70.0f; p.sharpenRadius = 1.4f; p.sharpenMasking = 30.0f;
        p.nrLuminance = 35.0f; p.nrColor = 25.0f;
        // Lens corrections (a resampling pass + a per-pixel falloff)
        p.lensDistortion = 12.0f; p.lensCA = 10.0f; p.lensVignette = -25.0f;
        // Tone curve: a real S-curve, so the LUT is not the identity
        p.curve = {CurvePoint{0.0f, 0.0f}, CurvePoint{0.25f, 0.19f},
                   CurvePoint{0.75f, 0.82f}, CurvePoint{1.0f, 1.0f}};
        // Colour mixer: hue / sat / lum each bent away from flat
        p.mixer[0] = {CurvePoint{0.0f, 0.10f}, CurvePoint{120.0f, -0.12f}, CurvePoint{240.0f, 0.08f}};
        p.mixer[1] = {CurvePoint{0.0f, 0.15f}, CurvePoint{180.0f, -0.10f}, CurvePoint{300.0f, 0.12f}};
        p.mixer[2] = {CurvePoint{0.0f, -0.08f}, CurvePoint{150.0f, 0.14f}, CurvePoint{280.0f, -0.06f}};
        // Colour grading: all three wheels off-neutral + a balance shift
        p.grade[0] = GradeWheel{210.0f, 22.0f, -8.0f};   // shadows
        p.grade[1] = GradeWheel{35.0f, 14.0f, 5.0f};     // midtones
        p.grade[2] = GradeWheel{48.0f, 18.0f, 10.0f};    // highlights
        p.balance = 15.0f;
        // Geometry stays 1:1 (R-IMG-2a): full-frame crop, no rotation.
        return p;
    }

    bool ImageWorkload::gpuAvailable()
    {
        // Constructing an engine installs the platform accelerator; available() is a
        // side-effect-free hardware fact, safe to ask once.
        EditEngine probe;
        return probe.gpuAvailable();
    }

    std::string ImageWorkload::backendText(bool preferGpu, bool accelerated, const char *name)
    {
        if (!preferGpu) return "CPU";
        if (accelerated) return std::string("GPU (") + (name ? name : "?") + ")";
        // The two ways asking for the GPU still yields a CPU number. Both are stated,
        // because a score labelled GPU that the CPU produced is worse than no toggle.
        return gpuAvailable() ? "CPU  (GPU declined this edit)" : "CPU  (no GPU backend)";
    }

    std::string ImageWorkload::describe() const
    {
        return std::to_string(mWidth) + " x " + std::to_string(mHeight) + " px  ·  " +
               std::to_string(activeStageCount()) + " adjustments  ·  " +
               (mPreferGpu ? "GPU requested" : "CPU");
    }

    int ImageWorkload::activeStageCount()
    {
        // LensCorrection, NoiseReduction, Exposure, Contrast, ToneRegions, WhiteBalance,
        // ToneCurve, Texture, Clarity, Vibrance, ColorMixer, ColorGrading, Dehaze,
        // Sharpen, Grain. (Crop and Rotate are in the pipeline but held at identity.)
        return 15;
    }

    WorkloadResult ImageWorkload::run() const
    {
        if (mWidth <= 0 || mHeight <= 0 || mPasses <= 0)
            return WorkloadResult{};

        // ── generation: outside the clock (R-IMG-3) ──
        const std::vector<uint8_t> pixels = makeDummyPixels(mWidth, mHeight);
        EditEngine engine;
        engine.setPreferGpu(mPreferGpu);  // the user's choice; CPU stays the default (R-IMG-4)
        const int slot = engine.addImage(pixels.data(), mWidth, mHeight, 4);
        if (slot < 0) return WorkloadResult{};
        engine.selectImage(slot);
        engine.setCurrentParams(benchParams());  // configures the pipeline; not timed

        // ── measurement: the fastest of N full-resolution renders (R-SCORE-3) ──
        double best = 0.0, checksum = 0.0;
        for (int pass = 0; pass < mPasses; ++pass)
        {
            const auto t0 = std::chrono::steady_clock::now();
            const PreviewBuffer out = engine.renderFull();
            const auto t1 = std::chrono::steady_clock::now();

            if (!out.rgba || out.width <= 0 || out.height <= 0)
                return WorkloadResult{};
            const double secs = std::chrono::duration<double>(t1 - t0).count();
            if (pass == 0 || secs < best) best = secs;
            // Folded after the clock stops, so the checksum never inflates the measurement.
            checksum = foldChecksum(out.rgba, (size_t)out.width * out.height * 4);
        }

        WorkloadResult r = WorkloadResult::fromSeconds(best);
        r.checksum = checksum;
        // Report the backend that actually ran, not the one that was asked for.
        r.detail = std::to_string(mWidth) + " x " + std::to_string(mHeight) + " px  ·  " +
                   std::to_string(activeStageCount()) + " adjustments  ·  " +
                   backendText(mPreferGpu, engine.lastRenderAccelerated(), engine.activeBackendName());
        return r;
    }
}
}
