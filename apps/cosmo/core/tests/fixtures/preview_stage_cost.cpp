/*
 *  D-45 fixture: WHERE do the ~600 ms of an interactive preview render go?
 *
 *  D-45's recommended first step is a measurement, not an optimisation: "extend the bench to
 *  time the preview pipeline per stage so the 600 ms is attributed to named processors before
 *  anything is optimised". This is that measurement, done without needing a RAW file — it
 *  synthesises a linear image at exactly the preview size the app uses (previewEdge 1600) and
 *  times, separately:
 *
 *    * par::parallelFor's own cost with an EMPTY body, which is pure thread create/join —
 *      Parallel.h spawns fresh std::threads on every call and has no pool, and one preview
 *      render makes ~13 such calls;
 *    * one PointProcessor pass at its identity default (Exposure), which is the per-pass cost
 *      the eight point stages each pay whether or not the user changed anything;
 *    * the serial std::copy an early-outing SPATIAL processor does (nine of them do this) —
 *      27 MB out and 27 MB in, single-threaded, per stage;
 *    * color::encodeInPlace, which is std::pow(double, 1/2.4) per colour channel;
 *    * the three histogram taps (final, pre-curve, pre-mixer hue), which each add up to four
 *      more srgbEncode calls per pixel;
 *    * the float->RGBA8 pack in EditEngine::renderInto;
 *    * and EditEngine::renderImage end-to-end at DEFAULT params, as the total to attribute.
 *
 *  Build against the tree you already built:
 *
 *      g++ -O2 -std=c++17 -DARSTRO_ENABLE_THREADS \
 *          -I core/ImageProcessing/src \
 *          -o /tmp/stagecost apps/cosmo/core/tests/fixtures/preview_stage_cost.cpp \
 *          build/core/ImageProcessing/libarstro_image.a -lpthread
 *
 *      /tmp/stagecost [edge] [iters]      # default 1600 8
 */
#include "engine/EditEngine.h"
#include "engine/EditParams.h"
#include "base/Image.h"
#include "base/ColorSpace.h"
#include "base/Parallel.h"
#include "analysis/Histogram.h"
#include "tone/Exposure.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <vector>

using namespace arstro;

namespace
{
    using Clock = std::chrono::steady_clock;
    double ms(Clock::time_point a, Clock::time_point b)
    {
        return std::chrono::duration<double, std::milli>(b - a).count();
    }

    /** A deterministic non-trivial image, so no stage can short-circuit on flat data. */
    Image makeLinear(int w, int h)
    {
        Image img(w, h, 4, ColorSpace::LinearSRGB);
        Pixel *d = img.data();
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                Pixel *p = d + ((size_t)y * w + x) * 4;
                p[0] = (Pixel)((x * 7 + y * 3) % 251) / (Pixel)251;
                p[1] = (Pixel)((x * 3 + y * 11) % 241) / (Pixel)241;
                p[2] = (Pixel)((x * 13 + y * 5) % 233) / (Pixel)233;
                p[3] = (Pixel)1;
            }
        return img;
    }

    void report(const char *name, double total, int iters, const char *note)
    {
        std::printf("  %-34s %8.2f ms   %s\n", name, total / iters, note);
    }
}

int main(int argc, char **argv)
{
    const int edge = argc > 1 ? std::atoi(argv[1]) : 1600;
    const int iters = argc > 2 ? std::atoi(argv[2]) : 8;
    // 3:2, the shape a 24 MP frame downscales to at previewEdge.
    const int w = edge, h = (int)(edge * 2.0 / 3.0);
    const size_t bytes = (size_t)w * h * 4 * sizeof(Pixel);

    std::printf("preview stage cost — %dx%d (%.2f Mpx, %.1f MB/buffer), threads=%d, iters=%d\n",
                w, h, (double)w * h / 1e6, bytes / 1e6, par::threads(), iters);

    const Image src = makeLinear(w, h);

    // 1. parallelFor with an empty body: thread create + join only.
    {
        const auto t0 = Clock::now();
        for (int i = 0; i < iters; ++i)
            par::parallelFor(h, [](int, int) {});
        const auto t1 = Clock::now();
        report("parallelFor (empty body)", ms(t0, t1), iters, "x ~13 calls per render");
    }

    // 2. one point processor at identity (Exposure default = 0 EV).
    {
        Exposure ex;
        Image out;
        const auto t0 = Clock::now();
        for (int i = 0; i < iters; ++i) ex.apply(src, out);
        const auto t1 = Clock::now();
        report("PointProcessor pass (identity)", ms(t0, t1), iters, "x 8 point stages");
    }

    // 3. the serial copy an early-outing spatial stage does.
    {
        Image out; out.resizeLike(src);
        const size_t n = (size_t)w * h * 4;
        const auto t0 = Clock::now();
        for (int i = 0; i < iters; ++i)
            std::copy(src.data(), src.data() + n, out.data());
        const auto t1 = Clock::now();
        report("spatial early-out copy (serial)", ms(t0, t1), iters, "x 9 spatial stages");
    }

    // 4. sRGB encode (pow per colour channel), parallel.
    {
        const auto t0 = Clock::now();
        for (int i = 0; i < iters; ++i)
        {
            Image tmp = src.clone();
            color::encodeInPlace(tmp);
        }
        const auto t1 = Clock::now();
        report("encodeInPlace (+clone)", ms(t0, t1), iters, "once per render");
    }

    // 5. the three histogram taps.
    {
        const auto t0 = Clock::now();
        for (int i = 0; i < iters; ++i) (void)Histogram::compute(src);
        const auto t1 = Clock::now();
        report("Histogram::compute (linear in)", ms(t0, t1), iters, "x 2 per render");

        const auto t2 = Clock::now();
        for (int i = 0; i < iters; ++i) (void)Histogram::computeHue(src);
        const auto t3 = Clock::now();
        report("Histogram::computeHue", ms(t2, t3), iters, "x 1 per render");
    }

    // 6. the float -> RGBA8 pack in renderInto.
    {
        std::vector<uint8_t> outBytes;
        const Pixel *s = src.data();
        const auto t0 = Clock::now();
        for (int i = 0; i < iters; ++i)
        {
            outBytes.assign((size_t)w * h * 4, 255);
            uint8_t *d = outBytes.data();
            par::parallelFor(h, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y)
                    for (int x = 0; x < w; ++x)
                    {
                        const Pixel *p = s + ((size_t)y * w + x) * 4;
                        uint8_t *q = d + ((size_t)y * w + x) * 4;
                        q[0] = (uint8_t)(clamp01(p[0]) * 255 + 0.5f);
                        q[1] = (uint8_t)(clamp01(p[1]) * 255 + 0.5f);
                        q[2] = (uint8_t)(clamp01(p[2]) * 255 + 0.5f);
                        q[3] = (uint8_t)(clamp01(p[3]) * 255 + 0.5f);
                    }
            });
        }
        const auto t1 = Clock::now();
        report("float -> RGBA8 pack", ms(t0, t1), iters, "once per render");
    }

    // 7. the whole thing, through the public engine seam, at DEFAULT params.
    {
        EditEngine eng;
        EditParams p;   // every field at its neutral value
        // renderImage downscales to maxEdge; pass the edge the image already is so no
        // downscale happens and the number is the render alone.
        const auto t0 = Clock::now();
        for (int i = 0; i < iters; ++i) (void)eng.renderImage(src, p, edge);
        const auto t1 = Clock::now();
        std::printf("  %-34s %8.2f ms   %s\n", "renderImage (all params default)",
                    ms(t0, t1) / iters, "<- D-45's number, to attribute");
    }

    return 0;
}
