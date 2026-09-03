#include "Detection.h"
#include "../base/Parallel.h"
#include "../base/Spatial.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace arstro
{
    namespace detect
    {
        namespace
        {
            /** One separable pass of a sliding min or max over rows, then over columns. The
             *  window is 2*radius+1 wide and the naive form is O(pixels x radius); at the radii
             *  this uses (a handful of pixels) that is cheaper than the deque a true running
             *  extremum needs, and it is the version whose correctness a reader can check. */
            template <bool Max>
            void morphPass(std::vector<Pixel> &plane, int w, int h, int radius)
            {
                if (radius < 1) return;
                std::vector<Pixel> tmp(plane.size());
                par::parallelFor(h, [&](int y0, int y1) {
                    for (int y = y0; y < y1; ++y)
                        for (int x = 0; x < w; ++x)
                        {
                            float best = Max ? 0.f : 1.f;
                            const int a = std::max(0, x - radius), b = std::min(w - 1, x + radius);
                            for (int i = a; i <= b; ++i)
                            {
                                const float v = (float)plane[(std::size_t)y * w + i];
                                best = Max ? std::max(best, v) : std::min(best, v);
                            }
                            tmp[(std::size_t)y * w + x] = (Pixel)best;
                        }
                });
                par::parallelFor(h, [&](int y0, int y1) {
                    for (int y = y0; y < y1; ++y)
                        for (int x = 0; x < w; ++x)
                        {
                            float best = Max ? 0.f : 1.f;
                            const int a = std::max(0, y - radius), b = std::min(h - 1, y + radius);
                            for (int j = a; j <= b; ++j)
                            {
                                const float v = (float)tmp[(std::size_t)j * w + x];
                                best = Max ? std::max(best, v) : std::min(best, v);
                            }
                            plane[(std::size_t)y * w + x] = (Pixel)best;
                        }
                });
            }
        }

        void erodePlane(std::vector<Pixel> &plane, int w, int h, int radius)
        {
            if (w <= 0 || h <= 0 || plane.size() < (std::size_t)w * h) return;
            morphPass<false>(plane, w, h, radius);
        }

        void dilatePlane(std::vector<Pixel> &plane, int w, int h, int radius)
        {
            if (w <= 0 || h <= 0 || plane.size() < (std::size_t)w * h) return;
            morphPass<true>(plane, w, h, radius);
        }

        int keepSignificantBlobs(std::vector<Pixel> &plane, int w, int h)
        {
            const std::size_t n = (std::size_t)std::max(0, w * h);
            if (n == 0 || plane.size() < n) return 0;

            // Iterative flood fill with an explicit stack, 8-connected. Iterative because a
            // component can be most of a megapixel and the recursive form is one stack frame per
            // pixel — a crash that only ever happens on the photographs that matter most.
            std::vector<int> label(n, 0);
            std::vector<std::size_t> area;   // area[k] is the size of label k+1
            std::vector<std::size_t> stack;
            int next = 0;
            for (std::size_t seed = 0; seed < n; ++seed)
            {
                if (label[seed] != 0 || (float)plane[seed] < 0.5f) continue;
                ++next;
                std::size_t count = 0;
                stack.clear();
                stack.push_back(seed);
                label[seed] = next;
                while (!stack.empty())
                {
                    const std::size_t i = stack.back();
                    stack.pop_back();
                    ++count;
                    const int x = (int)(i % (std::size_t)w), y = (int)(i / (std::size_t)w);
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            if (dx == 0 && dy == 0) continue;
                            const int nx = x + dx, ny = y + dy;
                            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                            const std::size_t j = (std::size_t)ny * w + nx;
                            if (label[j] != 0 || (float)plane[j] < 0.5f) continue;
                            label[j] = next;
                            stack.push_back(j);
                        }
                }
                area.push_back(count);
            }
            if (next == 0) return 0;

            std::size_t largest = 0;
            for (std::size_t a : area) largest = std::max(largest, a);
            const std::size_t floorAbs = (std::size_t)(kMinBlobFraction * (float)n);
            const std::size_t floorRel = (std::size_t)(kRelBlobFraction * (float)largest);
            const std::size_t keepAt = std::max<std::size_t>(1, std::max(floorAbs, floorRel));

            int kept = 0;
            std::vector<bool> keep((std::size_t)next + 1, false);
            for (int k = 0; k < next; ++k)
                if (area[(std::size_t)k] >= keepAt) { keep[(std::size_t)k + 1] = true; ++kept; }
            for (std::size_t i = 0; i < n; ++i)
                if (!keep[(std::size_t)label[i]]) plane[i] = (Pixel)0;
            return kept;
        }
    }

    DetectionResult detectSubjectRegions(const Image &framedLinear, SemanticSubject subject,
                                         float sensitivity, ISegmenter *seg,
                                         const DetectionProgress &onProgress)
    {
        DetectionResult r;
        r.by = seg ? seg->name() : "built-in";
        auto report = [&](const char *stage, float f) { if (onProgress) onProgress(stage, f); };

        const int fw = framedLinear.width(), fh = framedLinear.height();
        if (fw <= 1 || fh <= 1 || framedLinear.channels() < 3) { report("outline", 1.f); return r; }

        // ── preparing ──────────────────────────────────────────────────────────────────────
        // Decide the regions ONCE, at a bounded resolution. Not only cheaper: it is what makes a
        // detection run on a preview and a detection run on the full image the same detection
        // rather than two similar ones, which matters now that the answer is STORED.
        report("preparing", 0.05f);
        const int factor = std::max(1, (std::min(fw, fh) + segment::kAnalysisEdge - 1) /
                                           segment::kAnalysisEdge);
        const Image small = segment::downscaleForAnalysis(framedLinear, factor);
        const int w = small.width(), h = small.height();
        const std::size_t n = (std::size_t)w * h;
        if (n == 0) { report("outline", 1.f); return r; }

        // ── colour ─────────────────────────────────────────────────────────────────────────
        // The seam first, the built-in second (R-AISEG-6). A model that declines is answering
        // normally. The size is re-checked because a foreign implementation returning the wrong
        // number of values would otherwise be read out of bounds, and "the host's model was
        // wrong" must not become "cosmo crashed".
        report("colour", 0.20f);
        std::vector<Pixel> plane;
        if (seg && seg->segment(small, subject, sensitivity, plane) && plane.size() == n)
            r.handled = true;
        else if (segment::builtinHandles(subject))
        {
            segment::builtinScore(small, subject, plane);
            r.handled = true;
            r.by = "built-in";
        }
        if (!r.handled) { report("outline", 1.f); return r; }

        // ── regions ────────────────────────────────────────────────────────────────────────
        // Threshold at the sensitivity (R-AISEG-5), then deepgaze's cleanup: open to erase the
        // specks, close to fill the pinholes. Binarised in between because morphology is a
        // statement about a set, not about a weighted average — an erode over a soft plane
        // erodes the soft edge instead of the region.
        report("regions", 0.45f);
        segment::scoreToCoverage(plane, w, h, sensitivity);
        for (Pixel &v : plane) v = (Pixel)((float)v >= 0.5f ? 1.f : 0.f);
        const float shortEdge = (float)std::min(w, h);
        const int openR = std::max(1, (int)std::lround(detect::kOpenFraction * shortEdge));
        const int closeR = std::max(1, (int)std::lround(detect::kCloseFraction * shortEdge));
        detect::erodePlane(plane, w, h, openR);
        detect::dilatePlane(plane, w, h, openR);
        detect::dilatePlane(plane, w, h, closeR);
        detect::erodePlane(plane, w, h, closeR);

        // ── shapes ─────────────────────────────────────────────────────────────────────────
        report("shapes", 0.70f);
        detect::keepSignificantBlobs(plane, w, h);
        std::size_t covered = 0;
        for (const Pixel &v : plane) covered += ((float)v >= 0.5f) ? 1u : 0u;
        r.coverage = (float)covered / (float)n;
        if (covered == 0) { report("outline", 1.f); return r; }

        // ── outline ────────────────────────────────────────────────────────────────────────
        // Blur before tracing: the plane is binary now, so its 0.5 contour would otherwise run
        // along pixel edges as a staircase, and no amount of simplification turns a staircase
        // into a jawline. Then trace every cell (R-AISEG-14 as amended) and thin the result by
        // shape rather than by sampling.
        report("outline", 0.85f);
        std::vector<Pixel> smooth;
        const float blurSigma = detect::kPreTraceBlurFraction * shortEdge;
        if (blurSigma >= 0.5f) spatial::fastBlurPlane(plane, smooth, w, h, blurSigma);
        else smooth = plane;

        auto loops = traceCoverageOutline(smooth, w, h, 0.5f, /*maxEdge=*/0,
                                          detect::kMinLoopPoints);
        r.regions.reserve(loops.size());
        for (const ContourLoop &raw : loops)
        {
            ContourLoop simple = simplifyLoop(raw, detect::kSimplifyTolerance);
            if (simple.size() < 3) continue;
            if (loopArea(simple) < detect::kMinLoopArea) continue;   // a speck the blur re-grew
            r.regions.push_back(std::move(simple));
        }
        report("outline", 1.f);
        return r;
    }
}
