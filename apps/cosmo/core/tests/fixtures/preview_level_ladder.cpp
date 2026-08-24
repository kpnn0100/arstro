/*
 *  R-PREVIEW fixture: what does each pyramid level cost, and which one does a 33 ms
 *  interactive budget actually buy on this machine?
 *
 *  R-PREVIEW-1 promises a frame every 33 ms during a gesture and R-PREVIEW-2 says the
 *  resolution follows from that rather than being configured. Both of those are claims
 *  about a NUMBER, so there has to be something that reads the number back — this is it.
 *  It renders every level of a real pyramid at a range of thread counts and prints, per
 *  level, the cost and whether the budget is met.
 *
 *  Run it on the target board and the same table says which level an A733 or an RK3588
 *  settles on, with no code change and nothing to configure.
 *
 *      g++ -O2 -std=c++17 -DARSTRO_ENABLE_THREADS -I core/ImageProcessing/src \
 *          -o /tmp/ladder apps/cosmo/core/tests/fixtures/preview_level_ladder.cpp \
 *          build/core/ImageProcessing/libarstro_image.a -lpthread -lEGL
 *      /tmp/ladder [previewEdge] [budgetMs] [threads...]
 */
#include "engine/EditEngine.h"
#include "engine/EditParams.h"
#include "base/Parallel.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace arstro;
using Clock = std::chrono::steady_clock;

static double ms(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}

/** Encoded RGBA8, as a decoder hands it in — so the pyramid is built the way the real
 *  load builds it, through the fused convert-and-downscale. */
static std::vector<uint8_t> encodedFrame(int w, int h)
{
    std::vector<uint8_t> b((size_t)w * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            uint8_t *p = b.data() + ((size_t)y * w + x) * 4;
            p[0] = (uint8_t)((x * 7 + y * 3) % 251);
            p[1] = (uint8_t)((x * 3 + y * 11) % 241);
            p[2] = (uint8_t)((x * 13 + y * 5) % 233);
            p[3] = 255;
        }
    return b;
}

int main(int argc, char **argv)
{
    const int edge = argc > 1 ? std::atoi(argv[1]) : 1600;
    const double budget = argc > 2 ? std::atof(argv[2]) : 33.0;
    std::vector<int> threadCounts;
    for (int i = 3; i < argc; ++i) threadCounts.push_back(std::atoi(argv[i]));
    if (threadCounts.empty()) threadCounts = {24, 8, 4, 2, 1};

    // A 24 MP frame is what a RAW decodes to; the pyramid is built from it once.
    const int sw = 6000, sh = 4000;
    std::printf("preview level ladder — source %dx%d, previewEdge %d, budget %.0f ms\n",
                sw, sh, edge, budget);
    const std::vector<uint8_t> bytes = encodedFrame(sw, sh);

    EditParams neutral;                      // the default-params case, the common one
    EditParams edited;                       // ...and a real edit, so stages actually run
    edited.exposure = 0.6f; edited.contrast = 12.f; edited.vibrance = 20.f;
    edited.curve = {CurvePoint{0.f, 0.f}, CurvePoint{0.45f, 0.55f}, CurvePoint{1.f, 1.f}};
    edited.clarity = 15.f;                   // a spatial stage too

    for (int t : threadCounts)
    {
        par::setThreads(t);
        EditEngine eng;
        eng.setPreviewSize(edge);
        const auto tBuild = Clock::now();
        eng.addImagePreviewOnly(bytes.data(), sw, sh, 4);
        const double buildMs = ms(tBuild, Clock::now());

        std::printf("\n  threads=%d   pyramid built in %.1f ms (%.1f MB resident)\n",
                    t, buildMs, eng.residentProxyBytes() / 1e6);
        std::printf("    %-6s %-11s %10s %10s   %s\n", "level", "size", "neutral", "edited", "fits budget");

        for (int l = 0; l < EditEngine::previewLevels(); ++l)
        {
            eng.setPreviewLevel(l);
            double best[2] = {1e18, 1e18};
            const EditParams *sets[2] = {&neutral, &edited};
            int w = 0, h = 0;
            for (int k = 0; k < 2; ++k)
            {
                eng.applyParams(*sets[k]);
                for (int trial = 0; trial < 4; ++trial)
                {
                    const auto t0 = Clock::now();
                    const PreviewBuffer pb = eng.renderPreview();
                    const double m = ms(t0, Clock::now());
                    if (m < best[k]) best[k] = m;
                    w = pb.width; h = pb.height;
                }
            }
            char size[32];
            std::snprintf(size, sizeof(size), "%dx%d", w, h);
            const bool fitsNeutral = best[0] <= budget, fitsEdited = best[1] <= budget;
            std::printf("    %-6d %-11s %9.2fms %9.2fms   %s\n", l, size, best[0], best[1],
                        fitsEdited ? "yes" : (fitsNeutral ? "neutral only" : "no"));
        }
    }
    par::setThreads(0);
    return 0;
}
