/*
 *  interstellar_render_tests — L1 tests for the per-frame render path: blend maths, layer geometry
 *  (scale, rotation, crop, every Fit), the dissolve group, the active set across a transition, the
 *  frame cache's accounting, the grade step's identity passthrough and direction, and the param
 *  hash. Fixed inputs, no files, no display, well under a second.
 *
 *  Plain assert(), and NDEBUG is undone BEFORE <cassert> is included: a Release build otherwise
 *  compiles every assert to nothing and the suite "passes" having checked nothing — which has
 *  happened in this repo twice.
 */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "ActiveSet.h"
#include "ColourTransform.h"
#include "Composite.h"
#include "Effects.h"
#include "Prescale.h"
#include "FrameCache.h"
#include "GradeEngine.h"
#include "ParamHash.h"
#include "base/Parallel.h"
#include "engine/EditEngine.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace arstro::interstellar;
using namespace arstro::interstellar::render;

namespace
{
    bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

    Raster solid(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
    {
        Raster out;
        out.allocate(w, h, 0);
        for (int i = 0; i < w * h; ++i)
        {
            out.rgba[i * 4 + 0] = r;
            out.rgba[i * 4 + 1] = g;
            out.rgba[i * 4 + 2] = b;
            out.rgba[i * 4 + 3] = a;
        }
        return out;
    }

    const uint8_t *px(const Raster &r, int x, int y) { return &r.rgba[((size_t)y * r.width + x) * 4]; }

    bool is(const Raster &r, int x, int y, int cr, int cg, int cb, int ca, int tol = 0)
    {
        const uint8_t *p = px(r, x, y);
        return std::abs(p[0] - cr) <= tol && std::abs(p[1] - cg) <= tol && std::abs(p[2] - cb) <= tol &&
               std::abs(p[3] - ca) <= tol;
    }

    bool untouched(const Raster &r, int x, int y) { return is(r, x, y, 0, 0, 0, 0); }

    Layer layerOf(const Raster *src, Fit fit = Fit::Contain)
    {
        Layer l;
        l.src = src;
        l.fit = fit;
        return l;
    }

    double meanRgb(const Raster &r)
    {
        double s = 0;
        for (size_t i = 0; i < r.rgba.size(); i += 4) s += r.rgba[i] + r.rgba[i + 1] + r.rgba[i + 2];
        return s / (3.0 * r.width * r.height);
    }

    // ── Composite ────────────────────────────────────────────────────────────────────────────

    void test_blend_modes_on_known_operands()
    {
        assert(near(blendChannel(Blend::Normal, 0.2, 0.7), 0.7));
        assert(near(blendChannel(Blend::Multiply, 0.5, 0.5), 0.25));
        assert(near(blendChannel(Blend::Screen, 0.5, 0.5), 0.75));
        assert(near(blendChannel(Blend::Overlay, 0.25, 0.5), 0.25));          // dark base: 2bo
        assert(near(blendChannel(Blend::Overlay, 0.75, 0.5), 0.75));          // light base: screen-like
        assert(near(blendChannel(Blend::Add, 0.8, 0.5), 1.0));                // clamps
        assert(near(blendChannel(Blend::Add, 0.2, 0.3), 0.5));
        assert(near(blendChannel(Blend::Subtract, 0.2, 0.5), 0.0));           // clamps
        assert(near(blendChannel(Blend::Subtract, 0.8, 0.5), 0.3));
        assert(near(blendChannel(Blend::Difference, 0.8, 0.5), 0.3));
        assert(near(blendChannel(Blend::Difference, 0.5, 0.8), 0.3));

        // The 8-bit kernels must agree with the reference over an opaque backdrop, every mode,
        // a spread of operands, within one code value.
        const Blend modes[] = {Blend::Normal, Blend::Multiply, Blend::Screen, Blend::Overlay,
                               Blend::Add, Blend::Subtract, Blend::Difference};
        const int vals[] = {0, 1, 64, 127, 128, 200, 254, 255};
        for (Blend m : modes)
            for (int b : vals)
                for (int o : vals)
                {
                    Raster base = solid(1, 1, (uint8_t)b, (uint8_t)b, (uint8_t)b);
                    Raster over = solid(1, 1, (uint8_t)o, (uint8_t)o, (uint8_t)o);
                    Layer l = layerOf(&over, Fit::Stretch);
                    l.blend = m;
                    placeLayer(l, base);
                    const double want = blendChannel(m, b / 255.0, o / 255.0) * 255.0;
                    assert(std::fabs(px(base, 0, 0)[0] - want) <= 1.0);
                    assert(px(base, 0, 0)[3] == 255);
                }

        // W3C separable blending: over NOTHING a blend-mode layer is just itself, not multiplied
        // into the black of an empty raster.
        Raster over = solid(4, 4, 200, 100, 50);
        Layer l = layerOf(&over, Fit::Stretch);
        l.blend = Blend::Multiply;
        Raster out;
        compose({l}, 4, 4, out);
        assert(is(out, 1, 1, 200, 100, 50, 255));
        std::printf("[PASS] blend modes on known operands (reference + 8-bit kernels + backdrop rule)\n");
    }

    void test_half_scale_contain_lands_centred()
    {
        // Same aspect as the raster, so Contain fills it; scale 0.5 then covers the centre half.
        // Once through the copy path (source already at the drawn size) and once through the
        // bilinear path (a 4x source), which must agree on coverage exactly.
        Raster small = solid(100, 50, 10, 200, 30);
        Raster big = solid(400, 200, 10, 200, 30);
        for (const Raster *src : {&small, &big})
        {
            // Contain fits 100x50 by 2.0 and 400x200 by 0.5; x 0.5 both draw 100x50 in 200x100.
            Layer l = layerOf(src, Fit::Contain);
            l.geom.scale = 0.5;
            Raster out;
            compose({l}, 200, 100, out);
            // Drawn rectangle is [50,150) x [25,75).
            assert(is(out, 50, 25, 10, 200, 30, 255));
            assert(is(out, 149, 74, 10, 200, 30, 255));
            assert(is(out, 100, 50, 10, 200, 30, 255));
            assert(untouched(out, 49, 25) && untouched(out, 150, 74));
            assert(untouched(out, 50, 24) && untouched(out, 149, 75));
            assert(untouched(out, 0, 0) && untouched(out, 199, 0) && untouched(out, 0, 99) &&
                   untouched(out, 199, 99));
            int covered = 0;
            for (size_t i = 3; i < out.rgba.size(); i += 4) covered += out.rgba[i] != 0;
            assert(covered == 100 * 50);
        }
        std::printf("[PASS] a half-scale Contain layer lands centred, corners untouched\n");
    }

    void test_rotation_90_moves_a_known_pixel()
    {
        // 4x2 source, every pixel distinct. Rotated 90 deg clockwise about its centre into a 2x4
        // raster it is an exact permutation: source (x, y) -> output (H-1-y, x).
        Raster src;
        src.allocate(4, 2, 255);
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 4; ++x)
            {
                uint8_t *p = &src.rgba[((size_t)y * 4 + x) * 4];
                p[0] = (uint8_t)(10 + x * 40 + y * 7);
                p[1] = (uint8_t)(200 - x * 30);
                p[2] = (uint8_t)(50 + y * 100);
            }
        Layer l = layerOf(&src, Fit::None);
        l.geom.rotation = 90;
        Raster out;
        compose({l}, 2, 4, out);
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 4; ++x)
            {
                const uint8_t *s = px(src, x, y);
                assert(is(out, 2 - 1 - y, x, s[0], s[1], s[2], 255));
            }
        // The named pixel the requirement speaks of: the source's top-left lands top-right.
        assert(is(out, 1, 0, px(src, 0, 0)[0], px(src, 0, 0)[1], px(src, 0, 0)[2], 255));

        // And -90 is the inverse permutation: source (x, y) -> (y, W-1-x).
        l.geom.rotation = -90;
        compose({l}, 2, 4, out);
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 4; ++x)
            {
                const uint8_t *s = px(src, x, y);
                assert(is(out, y, 4 - 1 - x, s[0], s[1], s[2], 255));
            }
        std::printf("[PASS] a 90 degree rotation puts a known source pixel where expected\n");
    }

    void test_crop_selects_the_right_region()
    {
        // Four 2x2 quadrants of distinct colours. Cropping to the bottom-right quadrant and
        // stretching it over the whole raster must show ONLY that colour — including at the edges,
        // where an unclamped bilinear tap would bleed in the neighbouring quadrants.
        Raster src;
        src.allocate(4, 4, 255);
        const uint8_t q[4][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {250, 250, 0}};
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
            {
                const int k = (y >= 2 ? 2 : 0) + (x >= 2 ? 1 : 0);
                uint8_t *p = &src.rgba[((size_t)y * 4 + x) * 4];
                p[0] = q[k][0]; p[1] = q[k][1]; p[2] = q[k][2];
            }
        for (int k = 0; k < 4; ++k)
        {
            Layer l = layerOf(&src, Fit::Stretch);
            l.geom.cropX = (k & 1) ? 0.5 : 0.0;
            l.geom.cropY = (k & 2) ? 0.5 : 0.0;
            l.geom.cropW = l.geom.cropH = 0.5;
            Raster out;
            compose({l}, 9, 7, out);   // odd sizes: non-integer scale, so the bilinear path runs
            for (int y = 0; y < 7; ++y)
                for (int x = 0; x < 9; ++x) assert(is(out, x, y, q[k][0], q[k][1], q[k][2], 255));
        }
        std::printf("[PASS] crop selects the right source region, with no bleed at its edges\n");
    }

    void test_each_fit_mode()
    {
        // Contain: a 2:1 source in a square shows all of it, letterboxed top and bottom.
        Raster wide = solid(100, 50, 90, 90, 90);
        Raster out;
        compose({layerOf(&wide, Fit::Contain)}, 100, 100, out);
        assert(is(out, 0, 25, 90, 90, 90, 255) && is(out, 99, 74, 90, 90, 90, 255));
        assert(untouched(out, 0, 24) && untouched(out, 99, 75) && untouched(out, 50, 0));

        // Cover: fills the square and shows the source's CENTRE. Bands: left quarter red, middle
        // half green, right quarter blue — scaled 2x and centred, only green can be visible.
        Raster bands;
        bands.allocate(100, 50, 255);
        for (int y = 0; y < 50; ++y)
            for (int x = 0; x < 100; ++x)
            {
                uint8_t *p = &bands.rgba[((size_t)y * 100 + x) * 4];
                p[0] = x < 25 ? 255 : 0;
                p[1] = (x >= 25 && x < 75) ? 255 : 0;
                p[2] = x >= 75 ? 255 : 0;
            }
        compose({layerOf(&bands, Fit::Cover)}, 100, 100, out);
        // (Columns 0 and 99 sample across the band edge, which sits exactly at the visible edge.)
        for (int y = 0; y < 100; y += 9)
            for (int x = 1; x < 99; x += 7) assert(is(out, x, y, 0, 255, 0, 255));
        int coverCovered = 0;
        for (size_t i = 3; i < out.rgba.size(); i += 4) coverCovered += out.rgba[i] == 255;
        assert(coverCovered == 100 * 100);

        // Stretch: each axis independently — fills the square, left quarter of the source over the
        // left quarter of the raster.
        compose({layerOf(&bands, Fit::Stretch)}, 100, 100, out);
        assert(is(out, 5, 0, 255, 0, 0, 255) && is(out, 5, 99, 255, 0, 0, 255));
        assert(is(out, 50, 50, 0, 255, 0, 255) && is(out, 95, 99, 0, 0, 255, 255));
        int covered = 0;
        for (size_t i = 3; i < out.rgba.size(); i += 4) covered += out.rgba[i] == 255;
        assert(covered == 100 * 100);

        // None: one source pixel per output pixel, centred.
        compose({layerOf(&wide, Fit::None)}, 200, 200, out);
        assert(is(out, 50, 75, 90, 90, 90, 255) && is(out, 149, 124, 90, 90, 90, 255));
        assert(untouched(out, 49, 75) && untouched(out, 150, 124) && untouched(out, 50, 74) &&
               untouched(out, 50, 125));

        // geom.scale multiplies Stretch too (the first version silently ignored it there).
        Layer half = layerOf(&wide, Fit::Stretch);
        half.geom.scale = 0.5;
        compose({half}, 100, 100, out);
        assert(is(out, 25, 25, 90, 90, 90, 255) && is(out, 74, 74, 90, 90, 90, 255));
        assert(untouched(out, 24, 25) && untouched(out, 75, 74));
        std::printf("[PASS] each Fit mode: Contain, Cover, Stretch, None (and scale applies to all)\n");
    }

    void test_dissolve_group_sums_to_one_on_screen()
    {
        // The R-TL-4 picture, not just the numbers: two equally bright shots crossfading at the
        // midpoint must stay equally bright. Laid one over the other they dip to 75 %.
        Raster a = solid(16, 16, 200, 200, 200), b = solid(16, 16, 200, 200, 200);
        for (double f : {0.1, 0.25, 0.5, 0.8})
        {
            Layer la = layerOf(&a, Fit::Stretch), lb = layerOf(&b, Fit::Stretch);
            la.opacity = 1.0 - f;
            lb.opacity = f;
            lb.dissolveWithPrevious = true;
            Raster out;
            compose({la, lb}, 16, 16, out);
            assert(is(out, 8, 8, 200, 200, 200, 255, 1));

            lb.dissolveWithPrevious = false;   // what naive stacking would show
            compose({la, lb}, 16, 16, out);
            if (f == 0.5) assert(px(out, 8, 8)[0] <= 151);
        }

        // Partial coverage: B is a half-size picture-in-picture. Outside B, A must FADE (to the
        // base), not stay at full strength and pop off at the end of the transition.
        Raster red = solid(16, 16, 255, 0, 0), green = solid(16, 16, 0, 255, 0);
        Layer la = layerOf(&red, Fit::Stretch), lb = layerOf(&green, Fit::Stretch);
        lb.geom.scale = 0.5;
        la.opacity = 0.75;
        lb.opacity = 0.25;
        lb.dissolveWithPrevious = true;
        Raster out;
        compose({la, lb}, 16, 16, out);
        assert(is(out, 0, 0, 191, 0, 0, 191, 1));      // A only: 0.75 of red over the empty base
        assert(is(out, 8, 8, 191, 64, 0, 255, 1));     // both: 0.75 red + 0.25 green
        std::printf("[PASS] a dissolve group mixes against one base: constant brightness, A fades outside B\n");
    }

    void test_sampling_matches_a_reference_bilinear()
    {
        // Every sampling kind (Axis for scaled, General for rotated, Axis with a negative step for
        // 180 deg) against a bilinear written here, in doubles, straight from the Geom definition —
        // so the fixed-point, tap-table and span machinery is checked against the rule it encodes.
        Raster src;
        src.allocate(37, 23, 255);
        for (size_t i = 0; i < src.rgba.size(); ++i)
            if (i % 4 != 3) src.rgba[i] = (uint8_t)((i * 2654435761u) >> 11);
        const int W = 64, H = 48;
        for (double rot : {0.0, 23.0, 180.0, -71.0})
        {
            Layer l = layerOf(&src, Fit::Contain);
            l.geom.scale = 1.3;
            l.geom.x = 3.37;
            l.geom.y = -2.71;
            l.geom.rotation = rot;
            l.geom.cropX = 0.1;
            l.geom.cropY = 0.05;
            l.geom.cropW = 0.8;
            l.geom.cropH = 0.9;
            Raster out;
            compose({l}, W, H, out);

            const double cx0 = 0.1 * 37, cy0 = 0.05 * 23, cx1 = 0.9 * 37, cy1 = 0.95 * 23;
            const double srcW = 37 * 0.8, srcH = 23 * 0.9;
            const double s = std::min(W / srcW, H / srcH) * 1.3;
            const double ax = 0.5 * srcW * s, ay = 0.5 * srcH * s;
            const double ox = W * 0.5 + 3.37, oy = H * 0.5 - 2.71;
            const double th = rot * 3.14159265358979323846 / 180.0;
            const int lox = (int)std::floor(cx0), hix = (int)std::ceil(cx1) - 1;
            const int loy = (int)std::floor(cy0), hiy = (int)std::ceil(cy1) - 1;
            int covered = 0, worst = 0;
            for (int Y = 0; Y < H; ++Y)
                for (int X = 0; X < W; ++X)
                {
                    const double dx = X + 0.5 - ox, dy = Y + 0.5 - oy;
                    const double qx = std::cos(th) * dx + std::sin(th) * dy + ax;
                    const double qy = -std::sin(th) * dx + std::cos(th) * dy + ay;
                    const double u = cx0 + qx / s, v = cy0 + qy / s;
                    const bool in = u >= cx0 && u < cx1 && v >= cy0 && v < cy1;
                    assert(in == (px(out, X, Y)[3] != 0));
                    if (!in) continue;
                    ++covered;
                    const double fx = u - 0.5, fy = v - 0.5;
                    const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
                    const double wx = fx - x0, wy = fy - y0;
                    auto at = [&](int x, int y, int c) {
                        x = std::min(hix, std::max(lox, x));
                        y = std::min(hiy, std::max(loy, y));
                        return (double)px(src, x, y)[c];
                    };
                    for (int c = 0; c < 3; ++c)
                    {
                        const double want = at(x0, y0, c) * (1 - wx) * (1 - wy) + at(x0 + 1, y0, c) * wx * (1 - wy) +
                                            at(x0, y0 + 1, c) * (1 - wx) * wy + at(x0 + 1, y0 + 1, c) * wx * wy;
                        worst = std::max(worst, (int)std::lround(std::fabs(px(out, X, Y)[c] - want)));
                    }
                }
            assert(covered > W * H / 4);
            assert(worst <= 1);   // 8-bit weights: within one code value of the exact bilinear
        }
        std::printf("[PASS] sampling (axis, rotated, 180 deg) matches a reference bilinear within 1/255\n");
    }

    // ── the deep path (R-COLOR-1) ─────────────────────────────────────────────────────────────

    void test_deep_compose_places_layers_on_the_same_pixels()
    {
        // The 16-bit composite runs the same plan, spans and taps as the 8-bit one: fed the same
        // picture widened, it must cover exactly the same pixels and agree within a code value —
        // for every sampling kind, a blend mode over a backdrop, partial opacity and a dissolve.
        Raster base, over;
        base.allocate(37, 23, 255);
        over.allocate(29, 31, 255);
        for (size_t i = 0; i < base.rgba.size(); ++i)
            if (i % 4 != 3) base.rgba[i] = (uint8_t)((i * 2654435761u) >> 11);
        for (size_t i = 0; i < over.rgba.size(); ++i)
            over.rgba[i] = i % 4 == 3 ? (uint8_t)(128 + (i * 7) % 128) : (uint8_t)((i * 40503u) >> 5);
        Raster baseD, overD;
        toDeep(base, baseD);
        toDeep(over, overD);
        const int W = 64, H = 48;
        struct Case { double rot, scale, x; Blend blend; double op; bool dissolve; };
        const Case cases[] = {{0, 1.0, 0, Blend::Normal, 1.0, false},    // Copy (Fit::None below)
                              {0, 1.3, 3.37, Blend::Multiply, 0.6, false},  // Axis
                              {23, 1.1, -2.2, Blend::Screen, 1.0, false},   // General
                              {180, 0.9, 1.5, Blend::Difference, 0.8, false},
                              {0, 1.0, 0, Blend::Normal, 0.4, true}};      // a dissolve pair
        for (const Case &c : cases)
        {
            auto stack = [&](const Raster *b, const Raster *o) {
                Layer lb = layerOf(b, Fit::Cover);
                Layer lo = layerOf(o, c.scale == 1.0 && c.rot == 0 ? Fit::None : Fit::Contain);
                lo.geom.rotation = c.rot;
                lo.geom.scale = c.scale;
                lo.geom.x = c.x;
                lo.blend = c.blend;
                lo.opacity = c.op;
                lo.dissolveWithPrevious = c.dissolve;
                if (c.dissolve) lb.opacity = 1.0 - c.op;
                return std::vector<Layer>{lb, lo};
            };
            Raster out8, out16, back;
            compose(stack(&base, &over), W, H, out8);
            compose(stack(&baseD, &overD), W, H, out16);
            assert(!out8.deep() && out16.deep() && out16.width == W && out16.height == H);
            toShallow(out16, back);
            int worst = 0;
            for (int i = 0; i < W * H; ++i)
            {
                assert((out8.rgba[(size_t)i * 4 + 3] == 0) == (out16.rgba16[(size_t)i * 4 + 3] == 0));   // the same coverage
                for (int k = 0; k < 4; ++k) worst = std::max(worst, std::abs(out8.rgba[(size_t)i * 4 + k] - back.rgba[(size_t)i * 4 + k]));
            }
            assert(worst <= 1);
        }
        // a deep layer beside an 8-bit one: the composite is deep and the 8-bit one is widened
        Raster mixed;
        compose({layerOf(&base, Fit::Cover), layerOf(&overD, Fit::Contain)}, W, H, mixed);
        assert(mixed.deep());
        std::printf("[PASS] deep composite: the same coverage as 8-bit and within one code value, every sampling kind, blend and dissolve\n");
    }

    Raster deepRamp(int w, int h, int from, int step)
    {
        Raster r;
        r.allocate16(w, h, 65535);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                uint16_t *p = &r.rgba16[((size_t)y * w + x) * 4];
                p[0] = p[1] = p[2] = (uint16_t)(from + x * step);
            }
        return r;
    }

    int distinct(const Raster &r, int row)
    {
        std::vector<int> v;
        for (int x = 0; x < r.width; ++x)
            v.push_back(r.deep() ? r.rgba16[((size_t)row * r.width + x) * 4] : r.rgba[((size_t)row * r.width + x) * 4] * 257);
        std::sort(v.begin(), v.end());
        return (int)(std::unique(v.begin(), v.end()) - v.begin());
    }

    void test_deep_keeps_what_8_bits_cannot()
    {
        // 256 steps of 3/65535 span three 8-bit codes. Graded, effected and composited deep, the
        // ramp keeps its steps; the same picture at 8 bits has a handful.
        const Raster ramp = deepRamp(256, 4, 30000, 3);
        Raster ramp8;
        toShallow(ramp, ramp8);
        assert(distinct(ramp, 0) == 256 && distinct(ramp8, 0) <= 4);

        GradeEngine g;
        arstro::EditParams p;
        p.exposure = 1.0f;
        p.contrast = 20.f;
        Raster graded, graded8;
        assert(g.render(ramp, p, true, 0, graded) && graded.deep() && graded.width == 256);
        assert(g.render(ramp8, p, true, 0, graded8) && !graded8.deep());
        assert(distinct(graded, 0) >= 200);
        assert(distinct(graded8, 0) <= 6);
        // the deep grade is the 8-bit grade, unquantised: same picture within a code value
        Raster g8;
        assert(g.render(ramp8, p, true, 0, g8));
        Raster wide;
        toDeep(ramp8, wide);
        Raster gw, gwBack;
        assert(g.render(wide, p, true, 0, gw));
        toShallow(gw, gwBack);
        for (size_t i = 0; i < g8.rgba.size(); ++i) assert(std::abs(g8.rgba[i] - gwBack.rgba[i]) <= 1);
        // identity passes the deep bytes straight through
        Raster same;
        assert(g.render(ramp, arstro::EditParams{}, true, 0, same) && same.rgba16 == ramp.rgba16);

        // an effect at 16 bits keeps the steps too, and agrees with its 8-bit self
        EffectRun blur;
        blur.type = "blur.box";
        blur.p = {{"radius", 1.0}};
        Raster b = graded;
        assert(applyEffect(blur, 1.0, b) && b.deep());
        assert(distinct(b, 0) >= 200);
        Raster b8 = g8, bw = gw, bwBack;
        applyEffect(blur, 1.0, b8);
        applyEffect(blur, 1.0, bw);
        toShallow(bw, bwBack);
        for (size_t i = 0; i < b8.rgba.size(); ++i) assert(std::abs(b8.rgba[i] - bwBack.rgba[i]) <= 1);

        // and composited at 2x (the Axis sampler) the output row has the steps between them as well
        Raster out;
        compose({layerOf(&graded, Fit::Stretch)}, 512, 8, out);
        assert(out.deep() && distinct(out, 0) >= 400);
        std::printf("[PASS] deep: a ramp finer than 8 bits survives grade, effect and composite (%d levels vs %d at 8-bit)\n",
                    distinct(graded, 0), distinct(graded8, 0));
    }

    // ── colour management (R-COLOR-2..4) ─────────────────────────────────────────────────────

    float mapOne(const colour::Transform &t, float r, float g, float b, int c)
    {
        const float in[3] = {r, g, b};
        float out[3];
        t.map(in, out);
        return out[c];
    }

    void test_camera_curves_put_grey_where_the_vendors_say()
    {
        // Each vendor's published 18 % grey code value, through its input transform into ACEScct,
        // must land on ACEScct(0.18) = 0.41359 — the curve AND the gamut (grey is neutral in every
        // gamut once adapted to the ACES white). A code value is mapped to FFmpeg's video-range RGB.
        auto rgbOfCv = [](double cv) { return (float)((cv * 1023.0 - 64.0) / 876.0); };
        const float want = (std::log2(0.18f) + 9.72f) / 17.52f;
        struct G { const char *id; float x; };
        const G greys[] = {{"logc3", rgbOfCv(0.391007)},  {"logc4", rgbOfCv(0.278396)},
                           {"slog3", rgbOfCv(420.0 / 1023.0)}, {"vlog", rgbOfCv(0.423289)},
                           {"clog3", 0.343391f},          // Canon Log 3 is defined on IRE itself
                           {"log3g10", rgbOfCv(1.0 / 3.0)}, {"bmdfilm5", rgbOfCv(0.383562)}};
        for (const G &g : greys)
        {
            const auto t = colour::Transform::input(g.id, "acescct");
            assert(!t.identity());
            for (int c = 0; c < 3; ++c)
            {
                const float v = mapOne(t, g.x, g.x, g.x, c);
                if (std::fabs(v - want) > 2e-3f) std::printf("  %s grey: channel %d = %.5f, want %.5f\n", g.id, c, v, want);
                assert(std::fabs(v - want) < 2e-3f);
            }
        }
        // the gamut matrix: linear Rec.709 red is ACES's published (0.6131, 0.0702, 0.0206) in AP1
        const auto lin = colour::Transform::input("linear", "acescct");
        const float red[3] = {mapOne(lin, 1, 0, 0, 0), mapOne(lin, 1, 0, 0, 1), mapOne(lin, 1, 0, 0, 2)};
        auto dec = [](float y) { return y <= 0.155251141552511f ? (y - 0.0729055341958355f) / 10.5402377416545f : std::exp2(y * 17.52f - 9.72f); };
        assert(std::fabs(dec(red[0]) - 0.6131f) < 2e-3f && std::fabs(dec(red[1]) - 0.0702f) < 2e-3f && std::fabs(dec(red[2]) - 0.0206f) < 2e-3f);
        // in the Rec.709 working space a log source is tone-mapped for Cosmo: grey stays 18 % (sRGB 0.4614)
        const auto toRec = colour::Transform::input("slog3", "rec709");
        assert(std::fabs(mapOne(toRec, rgbOfCv(420.0 / 1023.0), rgbOfCv(420.0 / 1023.0), rgbOfCv(420.0 / 1023.0), 1) - 0.4614f) < 2e-3f);
        // and its brightest code value stays below white, with the curve still rising
        assert(mapOne(toRec, 1, 1, 1, 0) < 1.0f && mapOne(toRec, 1, 1, 1, 0) > mapOne(toRec, 0.9f, 0.9f, 0.9f, 0));
        std::printf("[PASS] colour: seven camera curves put 18%% grey at ACEScct 0.4136; Rec.709 red is ACES's AP1 red; S-Log3 grey stays 18%% in Rec.709\n");
    }

    void test_colour_identities_round_trips_and_hdr_levels()
    {
        // Rec.709 everywhere is nothing at all — today's pictures are untouched
        assert(colour::Transform::input("rec709", "rec709").identity() && colour::Transform::input("srgb", "rec709").identity());
        assert(colour::Transform::output("rec709", "rec709").identity() && colour::Transform::output("rec709", "srgb").identity());
        // a Rec.709 source through ACEScct and back out to Rec.709 is itself (the inverse tone map)
        const auto in = colour::Transform::input("rec709", "acescct"), out = colour::Transform::output("acescct", "rec709");
        float worst = 0;
        for (float r = 0.0f; r <= 0.95f; r += 0.05f)
            for (float g = 0.0f; g <= 0.95f; g += 0.19f)
                for (float b = 0.0f; b <= 0.95f; b += 0.19f)
                {
                    const float p[3] = {r, g, b};
                    float a[3], o[3];
                    in.map(p, a);
                    out.map(a, o);
                    for (int c = 0; c < 3; ++c) worst = std::max(worst, std::fabs(o[c] - p[c]));
                }
        assert(worst < 1.0f / 512.0f);
        // HDR levels: SDR white at 203 cd/m² is PQ 0.5806 and HLG 75 %
        assert(std::fabs(mapOne(colour::Transform::output("rec709", "pq"), 1, 1, 1, 0) - 0.5806f) < 3e-3f);
        assert(std::fabs(mapOne(colour::Transform::output("rec709", "hlg"), 1, 1, 1, 0) - 0.75f) < 5e-3f);
        // a scene-referred highlight rolls off to the mastering peak and never past it (PQ 1000 = 0.7518)
        const auto pq = colour::Transform::output("acescct", "pq", 1000.0);
        const float lo = mapOne(pq, 0.6f, 0.6f, 0.6f, 0), hi = mapOne(pq, 0.9f, 0.9f, 0.9f, 0), top = mapOne(pq, 1.2f, 1.2f, 1.2f, 0);
        assert(lo < hi && hi <= top && top <= 0.7519f && top > 0.70f);
        assert(colour::Transform::output("acescct", "pq", 1000.0).key() != colour::Transform::output("acescct", "pq", 4000.0).key());
        // deep and 8-bit apply the same function
        Raster r8;
        r8.allocate(16, 2, 255);
        for (int x = 0; x < 16; ++x) for (int c = 0; c < 3; ++c) r8.rgba[(size_t)x * 4 + c] = (uint8_t)(x * 16 + c * 5);
        Raster r16;
        toDeep(r8, r16);
        const auto t = colour::Transform::input("vlog", "rec709");
        t.apply(r8);
        t.apply(r16);
        Raster back;
        toShallow(r16, back);
        for (size_t i = 0; i < r8.rgba.size(); ++i) assert(std::abs(r8.rgba[i] - back.rgba[i]) <= 1);
        std::printf("[PASS] colour: Rec.709 is untouched; Rec.709 → ACEScct → Rec.709 round-trips within 1/512; PQ 203 = 0.5806, HLG 75%%; highlights stop at the peak\n");
    }

    void test_hostile_geometry_stays_in_bounds()
    {
        // Raw-pointer inner loops earn a hostile sweep: off-screen, sub-pixel, huge, extreme crops,
        // every fit and blend. Under ASan (NOTES.md) this is an out-of-bounds hunt; in the normal
        // build it checks the cheap invariants — a layer either draws in its box or not at all.
        uint32_t rng = 12345;
        auto next = [&rng] { rng = rng * 1664525u + 1013904223u; return rng >> 8; };
        auto uni = [&](double lo, double hi) { return lo + (hi - lo) * (next() / 16777216.0); };
        Raster src;
        src.allocate(13, 7, 255);
        for (size_t i = 0; i < src.rgba.size(); ++i) src.rgba[i] = (uint8_t)next();
        const Blend modes[] = {Blend::Normal, Blend::Multiply, Blend::Screen, Blend::Overlay,
                               Blend::Add, Blend::Subtract, Blend::Difference};
        const Fit fits[] = {Fit::Contain, Fit::Cover, Fit::Stretch, Fit::None};
        for (int i = 0; i < 3000; ++i)
        {
            Layer l = layerOf(&src, fits[next() % 4]);
            l.blend = modes[next() % 7];
            l.geom.scale = std::pow(10.0, uni(-3.0, 2.0));
            l.geom.rotation = uni(-720, 720);
            l.geom.x = uni(-60, 60);
            l.geom.y = uni(-60, 60);
            l.geom.anchorX = uni(-1, 2);
            l.geom.anchorY = uni(-1, 2);
            l.geom.cropX = uni(-0.2, 1.2);
            l.geom.cropY = uni(-0.2, 1.2);
            l.geom.cropW = uni(-0.1, 1.3);
            l.geom.cropH = uni(-0.1, 1.3);
            l.opacity = uni(-0.2, 1.2);
            Layer under = layerOf(&src, Fit::Stretch);
            l.dissolveWithPrevious = (next() & 1) != 0;
            Raster out;
            compose({under, l}, 1 + (int)(next() % 23), 1 + (int)(next() % 17), out);
            assert(out.rgba.size() == (size_t)out.width * out.height * 4);
        }
        // NaN anywhere in the geometry draws nothing rather than garbage.
        Layer bad = layerOf(&src, Fit::Contain);
        bad.geom.rotation = std::nan("");
        Raster out;
        compose({bad}, 8, 8, out);
        for (size_t i = 0; i < out.rgba.size(); ++i) assert(out.rgba[i] == 0);
        std::printf("[PASS] hostile geometry (3000 random layers, NaN) stays in bounds\n");
    }

    void test_serial_and_parallel_are_byte_identical()
    {
        // R-RENDER-2: the thread count must not be an input to the picture.
        Raster src;
        src.allocate(320, 180, 255);
        for (size_t i = 0; i < src.rgba.size(); ++i)
            if (i % 4 != 3) src.rgba[i] = (uint8_t)((i * 2654435761u) >> 13);
        Raster base = solid(64, 64, 30, 60, 90);
        std::vector<Layer> layers;
        layers.push_back(layerOf(&base, Fit::Cover));
        Layer l = layerOf(&src, Fit::Contain);
        l.geom.rotation = 17.5;
        l.geom.scale = 0.7;
        l.geom.x = 13.25;
        l.geom.y = -7.5;
        l.opacity = 0.6;
        l.blend = Blend::Overlay;
        layers.push_back(l);

        arstro::par::setThreads(1);
        Raster serial;
        compose(layers, 640, 360, serial);
        arstro::par::setThreads(8);
        Raster parallel;
        compose(layers, 640, 360, parallel);
        arstro::par::setThreads(0);
        assert(serial.rgba == parallel.rgba);
        std::printf("[PASS] serial and parallel composites are byte-identical\n");
    }

    // ── ActiveSet ────────────────────────────────────────────────────────────────────────────

    ClipSpan clip(const std::string &id, int track, double at, double in, double out, double speed = 1.0,
                  bool audio = false)
    {
        ClipSpan c;
        c.id = id;
        c.trackOrder = track;
        c.audio = audio;
        c.at = at;
        c.in = in;
        c.out = out;
        c.speed = speed;
        return c;
    }

    void test_active_source_frame_at_speed_2x()
    {
        const double fps = 24.0;
        // At 1 s, source 10..20 s at 2x: five timeline seconds.
        std::vector<ClipSpan> clips = {clip("fast", 0, 1.0, 10.0, 20.0, 2.0)};
        auto a = activeAt(clips, {}, 2.0, fps);
        assert(a.size() == 1 && a[0].id == "fast");
        assert(near(a[0].localTime, 12.0));
        assert(a[0].sourceFrame == 288);              // 12 s * 24
        a = activeAt(clips, {}, 3.5, fps);
        assert(a[0].sourceFrame == 360);              // (2.5 * 2 + 10) * 24
        // One timeline frame later is TWO source frames later — the reason R-VOL-5 indexes the
        // volume by source frame.
        const long long f0 = activeAt(clips, {}, 2.0, fps)[0].sourceFrame;
        const long long f1 = activeAt(clips, {}, 2.0 + 1.0 / fps, fps)[0].sourceFrame;
        assert(f1 - f0 == 2);
        assert(near(activeAt(clips, {}, 3.5, fps)[0].progress, 0.5));
        assert(activeAt(clips, {}, 0.99, fps).empty());
        assert(activeAt(clips, {}, 6.0, fps).empty());   // 1 + 5: half-open end

        // Frame boundaries are the authority: t = k/fps must read frame k, never k-1.
        std::vector<ClipSpan> one = {clip("c", 0, 0.0, 0.0, 1000.0)};
        for (int k = 0; k < 5000; ++k) assert(activeAt(one, {}, k / fps, fps)[0].sourceFrame == k);
        for (int k = 0; k < 5000; ++k) assert(activeAt(one, {}, k / 29.97, 29.97)[0].sourceFrame == k);

        // Audio is not video.
        std::vector<ClipSpan> mixed = {clip("v", 0, 0, 0, 10), clip("snd", 1, 0, 0, 10, 1.0, true)};
        a = activeAt(mixed, {}, 1.0, fps);
        assert(a.size() == 1 && a[0].id == "v");

        // Bottom track first, whatever the input order.
        std::vector<ClipSpan> stack = {clip("top", 2, 0, 0, 10), clip("bottom", 0, 0, 0, 10), clip("mid", 1, 0, 0, 10)};
        a = activeAt(stack, {}, 1.0, fps);
        assert(a.size() == 3 && a[0].id == "bottom" && a[1].id == "mid" && a[2].id == "top");
        std::printf("[PASS] activeAt: the right source frame at 2x, frame-exact at k/fps, audio excluded, bottom first\n");
    }

    void test_active_transition_holds_outgoing_and_weights_sum_to_one()
    {
        const double fps = 24.0;
        // A: 0..2 s on the timeline, source 0..2 (with handles beyond). B cuts in at 2 s.
        std::vector<ClipSpan> clips = {clip("shotB", 0, 2.0, 5.0, 10.0), clip("shotA", 0, 0.0, 0.0, 2.0)};
        std::vector<TransitionSpan> xs = {TransitionSpan{"shotA", "shotB", 0.5, true}};

        auto a = activeAt(clips, xs, 1.0, fps);
        assert(a.size() == 1 && a[0].id == "shotA" && near(a[0].weight, 1.0) && !a[0].held);

        // AT the cut both are active: A held past its out-point at full weight, B in at zero.
        a = activeAt(clips, xs, 2.0, fps);
        assert(a.size() == 2);
        assert(a[0].id == "shotA" && a[1].id == "shotB");   // outgoing under incoming
        assert(a[0].held && !a[1].held);
        assert(near(a[0].weight, 1.0) && near(a[1].weight, 0.0));
        assert(near(a[0].localTime, 2.0));                    // reading its handle
        assert(a[1].dissolveWithPrevious && !a[0].dissolveWithPrevious);

        // Mid-transition: the weights sum to 1, and the ramp is LINEAR.
        a = activeAt(clips, xs, 2.25, fps);
        assert(a.size() == 2);
        assert(near(a[0].weight + a[1].weight, 1.0, 1e-12));
        assert(near(a[0].weight, 0.5, 1e-12) && near(a[1].weight, 0.5, 1e-12));
        assert(a[0].held && near(a[0].localTime, 2.25));
        assert(a[0].sourceFrame == 54);                        // 2.25 s * 24, inside the handle
        for (double t = 2.0; t < 2.5; t += 0.01)
        {
            auto s = activeAt(clips, xs, t, fps);
            assert(s.size() == 2);
            assert(near(s[0].weight + s[1].weight, 1.0, 1e-12));
            assert(near(s[1].weight, (t - 2.0) / 0.5, 1e-9));  // linear alpha ramp
        }

        // After it, only the incoming clip, at full weight.
        a = activeAt(clips, xs, 2.5, fps);
        assert(a.size() == 1 && a[0].id == "shotB" && near(a[0].weight, 1.0) && !a[0].dissolveWithPrevious);

        // An eased transition still sums to 1.
        std::vector<TransitionSpan> eased = {TransitionSpan{"shotA", "shotB", 0.5, false}};
        a = activeAt(clips, eased, 2.125, fps);
        assert(a.size() == 2 && near(a[1].weight, 0.15625, 1e-12));
        assert(near(a[0].weight + a[1].weight, 1.0, 1e-12));

        // A transition naming a clip that is not there is ignored, not half-applied.
        std::vector<TransitionSpan> broken = {TransitionSpan{"ghost", "shotB", 0.5, true}};
        a = activeAt(clips, broken, 2.25, fps);
        assert(a.size() == 1 && a[0].id == "shotB" && near(a[0].weight, 1.0));
        std::printf("[PASS] activeAt: a transition holds the outgoing clip; weights 1->0 / 0->1 sum to 1\n");
    }

    // ── FrameCache ───────────────────────────────────────────────────────────────────────────

    FrameCache::Key key(const std::string &src, long long f, uint64_t h = 7, int level = 0)
    {
        FrameCache::Key k;
        k.source = src;
        k.sourceFrame = f;
        k.paramHash = h;
        k.level = level;
        return k;
    }

    void test_frame_cache()
    {
        const Raster frame = solid(8, 8, 1, 2, 3);   // 256 bytes
        const size_t fb = frame.bytes();

        // Repeat hit, and the copy is a copy.
        {
            FrameCache c(10 * fb);
            Raster out;
            assert(!c.get(key("a.mov", 1), out) && c.misses() == 1);
            c.put(key("a.mov", 1), frame);
            assert(c.get(key("a.mov", 1), out) && c.hits() == 1);
            assert(out.rgba == frame.rgba);
            out.rgba[0] = 99;
            Raster again;
            assert(c.get(key("a.mov", 1), again) && c.hits() == 2 && again.rgba[0] == 1);
            // Every key field matters.
            assert(!c.get(key("a.mov", 1, 8), out));
            assert(!c.get(key("a.mov", 1, 7, 1), out));
            assert(!c.get(key("b.mov", 1), out));
            assert(!c.get(key("a.mov", 2), out));
            assert(c.misses() == 5);
        }
        // The cap is respected, in bytes, and the LEAST recently used goes first.
        {
            FrameCache c(3 * fb);
            for (int f = 0; f < 3; ++f) c.put(key("a.mov", f), frame);
            Raster out;
            assert(c.get(key("a.mov", 0), out));   // promote 0, so 1 is now the oldest
            c.put(key("a.mov", 3), frame);
            c.put(key("a.mov", 4), frame);
            assert(c.entries() == 3 && c.residentBytes() == 3 * fb && c.residentBytes() <= c.capBytes());
            assert(c.evictions() == 2);
            assert(c.get(key("a.mov", 0), out) && c.get(key("a.mov", 3), out) && c.get(key("a.mov", 4), out));
            assert(!c.get(key("a.mov", 1), out) && !c.get(key("a.mov", 2), out));
            c.setCapBytes(fb);   // lowering the cap evicts now
            assert(c.entries() == 1 && c.residentBytes() == fb);
            // Replacing a key does not double-count it.
            c.put(key("a.mov", 9), frame);
            c.put(key("a.mov", 9), frame);
            assert(c.entries() == 1 && c.residentBytes() == fb);
        }
        // A frame larger than the whole cap is not cached — and does not empty the cache.
        {
            FrameCache c(2 * fb);
            c.put(key("a.mov", 0), frame);
            const Raster huge = solid(16, 16, 0, 0, 0);   // 1024 bytes > 512
            c.put(key("a.mov", 1), huge);
            Raster out;
            assert(c.entries() == 1 && c.residentBytes() == fb && c.evictions() == 0);
            assert(!c.get(key("a.mov", 1), out) && c.get(key("a.mov", 0), out));
        }
        // Range invalidation: only that source, only that range, inclusive.
        {
            FrameCache c(100 * fb);
            for (int f = 0; f < 10; ++f)
            {
                c.put(key("a.mov", f), frame);
                c.put(key("b.mov", f), frame);
            }
            c.put(key("a.mov", 4, 99, 2), frame);   // another grade/level of a frame in range
            assert(c.entries() == 21);
            c.invalidate("a.mov", 3, 6);
            assert(c.entries() == 16 && c.residentBytes() == 16 * fb);
            Raster out;
            for (int f = 3; f <= 6; ++f) assert(!c.get(key("a.mov", f), out) && c.get(key("b.mov", f), out));
            assert(!c.get(key("a.mov", 4, 99, 2), out));
            assert(c.get(key("a.mov", 2), out) && c.get(key("a.mov", 7), out));
            const FrameCache::Stats s = c.stats();
            assert(s.entries == 16 && s.residentBytes == 16 * fb && s.evictions == 0);
        }
        std::printf("[PASS] FrameCache: repeat hit, byte cap + LRU, oversize not cached, range invalidation\n");
    }

    // ── Grade + hash ─────────────────────────────────────────────────────────────────────────

    Raster gradient(int w, int h)
    {
        Raster r;
        r.allocate(w, h, 255);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                uint8_t *p = &r.rgba[((size_t)y * w + x) * 4];
                p[0] = (uint8_t)(30 + x * 150 / w);
                p[1] = (uint8_t)(40 + y * 120 / h);
                p[2] = (uint8_t)(90 + ((x + y) % 50));
            }
        return r;
    }

    void test_grade_identity_is_a_byte_identical_passthrough()
    {
        GradeEngine g;
        const Raster in = gradient(64, 48);
        Raster out;
        assert(g.render(in, arstro::EditParams{}, false, 0, out));
        assert(out.width == in.width && out.height == in.height && out.rgba == in.rgba);
        assert(g.render(in, arstro::EditParams{}, true, 0, out));
        assert(out.rgba == in.rgba);
        assert(g.render(in, arstro::EditParams{}, true, 32, out));   // a proxy edge does not resize identity
        assert(out.width == 64 && out.rgba == in.rgba);
        assert(GradeEngine::isIdentity(arstro::EditParams{}));
        arstro::EditParams p;
        p.exposure = 0.5f;
        assert(!GradeEngine::isIdentity(p));
        p = arstro::EditParams{};
        p.curve = {arstro::CurvePoint{0.f, 0.f}, arstro::CurvePoint{0.5f, 0.6f}, arstro::CurvePoint{1.f, 1.f}};
        assert(!GradeEngine::isIdentity(p));   // a curve is a change too, not just the scalars
        Raster empty;
        assert(!g.render(empty, arstro::EditParams{}, true, 0, out));
        std::printf("[PASS] grade: identity is a byte-identical passthrough\n");
    }

    void test_grade_exposure_raises_the_mean()
    {
        GradeEngine g;
        const Raster in = gradient(64, 48);
        arstro::EditParams up;
        up.exposure = 1.0f;
        Raster out;
        assert(g.render(in, up, true, 0, out));
        assert(out.width == 64 && out.height == 48);
        const double before = meanRgb(in), after = meanRgb(out);
        assert(after > before + 20.0);

        arstro::EditParams down;
        down.exposure = -1.0f;
        assert(g.render(in, down, true, 0, out));
        assert(meanRgb(out) < before - 20.0);

        // The proxy path: graded at the asked-for long edge, still brighter.
        assert(g.render(in, up, true, 32, out));
        assert(out.width == 32 && out.height == 24);
        assert(meanRgb(out) > before + 20.0);

        // Pure function: the same frame and params give the same bytes, call after call.
        Raster again;
        assert(g.render(in, up, true, 32, again) && again.rgba == out.rgba);
        std::printf("[PASS] grade: +exposure raises the mean channel value (-exposure lowers it), full and proxy\n");
    }

    void test_grade_matches_the_slot_sequence_byte_for_byte()
    {
        // GradeEngine calls renderImage (it keeps its scratch; renderFull frees it every frame and
        // was 3x slower per video frame). That is only allowed because it is the SAME picture as
        // the slot sequence — clearImages, addImage, selectImage, applyParams, renderFull or
        // setPreviewSize + renderPreview. This holds it to that, at full size and at a proxy edge.
        Raster in;
        in.allocate(96, 64, 255);
        for (size_t i = 0; i < in.rgba.size(); ++i)
            if (i % 4 != 3) in.rgba[i] = (uint8_t)(40 + ((i * 2654435761u) >> 25));
        arstro::EditParams p;
        p.exposure = 0.5f;
        p.contrast = 20.f;
        p.temp = 5200.f;
        p.vibrance = 15.f;
        p.curve = {arstro::CurvePoint{0.f, 0.f}, arstro::CurvePoint{0.25f, 0.19f},
                   arstro::CurvePoint{0.75f, 0.83f}, arstro::CurvePoint{1.f, 1.f}};
        GradeEngine g;
        for (int edge : {0, 40})
        {
            arstro::EditEngine e;
            e.clearImages();
            const int slot = e.addImage(in.rgba.data(), in.width, in.height, 4);
            e.selectImage(slot);
            e.applyParams(p);
            arstro::PreviewBuffer ref;
            if (edge > 0)
            {
                e.setPreviewSize(edge);
                ref = e.renderPreview();
            }
            else
                ref = e.renderFull();
            assert(ref.rgba && ref.width > 0);

            Raster out;
            assert(g.render(in, p, true, edge, out));
            assert(out.width == ref.width && out.height == ref.height);
            assert(std::equal(out.rgba.begin(), out.rgba.end(), ref.rgba));
            assert(out.rgba != in.rgba);   // and it really did grade

            g.releaseScratch();            // a fresh engine after a release: same bytes again
            Raster again;
            assert(g.render(in, p, true, edge, again) && again.rgba == out.rgba);
        }
        std::printf("[PASS] grade: renderImage path is byte-identical to the slot sequence (full and proxy)\n");
    }

    void test_param_hash()
    {
        // Pinned to the published FNV-1a 64 vectors, so the function cannot drift unnoticed.
        assert(fnv1a64("") == 0xcbf29ce484222325ULL);
        assert(fnv1a64("a") == 0xaf63dc4c8601ec8cULL);
        assert(fnv1a64("foobar") == 0x85944171f73967e8ULL);

        const arstro::EditParams d;
        assert(hashParams(d) == hashParams(arstro::EditParams{}));
        assert(hashParams(d) != FrameCache::kUngraded);
        arstro::EditParams p;
        p.exposure = 0.5f;
        assert(hashParams(p) != hashParams(d));
        arstro::EditParams q = p;
        assert(hashParams(q) == hashParams(p));
        q.curve = {arstro::CurvePoint{0.f, 0.f}, arstro::CurvePoint{0.5f, 0.6f}, arstro::CurvePoint{1.f, 1.f}};
        assert(hashParams(q) != hashParams(p));
        q = p;
        q.temp = 5200.f;
        assert(hashParams(q) != hashParams(p));
        std::printf("[PASS] param hash: FNV-1a vectors, equal grades equal, any change differs, never 0\n");
    }

    // ── Effects (R-FX-6) ─────────────────────────────────────────────────────────────────────

    EffectRun runOf(const std::string &type, std::map<std::string, double> p = {}, double mix = 1.0)
    {
        EffectRun e;
        e.type = type;
        e.mix = mix;
        for (const auto &d : effectType(type)->params) e.p[d.key] = d.def;
        for (const auto &kv : p) e.p[kv.first] = kv.second;
        return e;
    }

    void test_every_blur_leaves_a_flat_field_flat()
    {
        for (const auto &t : effectCatalog())
        {
            Raster r = solid(64, 40, 120, 60, 200);
            assert(applyEffect(runOf(t.type), 1.0, r));
            for (int y = 0; y < 40; y += 7)
                for (int x = 0; x < 64; x += 7) assert(is(r, x, y, 120, 60, 200, 255, 1));
        }
        std::printf("[PASS] effects: every blur leaves a flat field flat (no drift, no edge darkening)\n");
    }

    void test_gaussian_spreads_symmetrically_and_keeps_energy()
    {
        Raster r = solid(81, 81, 0, 0, 0);
        for (int y = 36; y < 45; ++y)
            for (int x = 36; x < 45; ++x) { uint8_t *p = &r.rgba[((size_t)y * 81 + x) * 4]; p[0] = p[1] = p[2] = 255; }
        const double before = meanRgb(r);
        assert(applyEffect(runOf("blur.gaussian", {{"radius", 8.0}}), 1.0, r));
        const double after = meanRgb(r);
        assert(std::fabs(after - before) / before < 0.03);                        // energy kept
        assert(px(r, 40, 40)[0] < 255 && px(r, 40, 40)[0] > px(r, 40, 52)[0]);     // the centre fell, the edge rose
        assert(std::abs(px(r, 30, 40)[0] - px(r, 50, 40)[0]) <= 1);              // symmetric
        assert(std::abs(px(r, 40, 30)[0] - px(r, 40, 50)[0]) <= 1);
        assert(px(r, 2, 2)[0] == 0);                                              // far away: untouched
        // scale: a half-size proxy blurs by half the pixels, the same share of the picture
        Raster small = solid(41, 41, 0, 0, 0);
        for (int y = 18; y < 23; ++y)
            for (int x = 18; x < 23; ++x) { uint8_t *p = &small.rgba[((size_t)y * 41 + x) * 4]; p[0] = p[1] = p[2] = 255; }
        Raster small2 = small;
        applyEffect(runOf("blur.gaussian", {{"radius", 8.0}}), 0.5, small);
        applyEffect(runOf("blur.gaussian", {{"radius", 8.0}}), 1.0, small2);
        assert(px(small, 20, 20)[0] > px(small2, 20, 20)[0]);                     // less spread at half scale
        std::printf("[PASS] effects: Gaussian spreads symmetrically, keeps energy, scales with the proxy\n");
    }

    void test_directional_blur_follows_its_angle()
    {
        // a vertical white line: an angle-0 (horizontal) blur widens it, never lengthens it
        Raster r = solid(60, 60, 0, 0, 0);
        for (int y = 20; y < 40; ++y) { uint8_t *p = &r.rgba[((size_t)y * 60 + 30) * 4]; p[0] = p[1] = p[2] = 255; }
        Raster v = r;
        assert(applyEffect(runOf("blur.directional", {{"length", 12.0}, {"angle", 0.0}}), 1.0, r));
        assert(px(r, 34, 30)[0] > 0 && px(r, 26, 30)[0] > 0);                     // spread sideways
        assert(px(r, 30, 15)[0] == 0 && px(r, 30, 45)[0] == 0);                    // not along the line
        assert(applyEffect(runOf("blur.directional", {{"length", 12.0}, {"angle", 90.0}}), 1.0, v));
        assert(px(v, 34, 30)[0] == 0 && px(v, 30, 16)[0] > 0);                      // 90°: along the line only
        std::printf("[PASS] effects: a directional blur smears along its angle and nowhere else\n");
    }

    void test_zoom_and_spin_keep_their_centre()
    {
        Raster r = solid(61, 61, 0, 0, 0);
        for (int y = 0; y < 61; ++y)
            for (int x = 0; x < 61; ++x) { uint8_t *p = &r.rgba[((size_t)y * 61 + x) * 4]; p[0] = (uint8_t)(x * 4); p[1] = (uint8_t)(y * 4); }
        Raster z = r, sp = r;
        assert(applyEffect(runOf("blur.zoom", {{"amount", 0.5}}), 1.0, z));
        assert(applyEffect(runOf("blur.spin", {{"angle", 30.0}}), 1.0, sp));
        assert(is(z, 30, 30, px(r, 30, 30)[0], px(r, 30, 30)[1], 0, 255, 1));      // the centre does not move
        assert(is(sp, 30, 30, px(r, 30, 30)[0], px(r, 30, 30)[1], 0, 255, 1));
        assert(z.rgba != r.rgba && sp.rgba != r.rgba);                              // elsewhere it smears
        std::printf("[PASS] effects: zoom and spin smear about a fixed centre\n");
    }

    void test_prescale_is_gamma_correct_and_preview_only()
    {
        assert(prescaleFactor(3840, 2160, 640) == 3 && prescaleFactor(1920, 1080, 640) == 1 && prescaleFactor(3840, 2160, 0) == 1);
        Raster out;
        Raster big = solid(3840, 2160, 90, 140, 200);
        assert(!prescale(big, 0, out) && out.empty());                       // a full-size render never prescales
        assert(prescale(big, 640, out) && out.width == 1280 && out.height == 720);
        assert(is(out, 100, 100, 90, 140, 200, 255, 1));                     // a flat field stays itself
        // a 0/255 checker averages in LINEAR light: half power is ~188, not the 128 an 8-bit mean gives
        Raster chk = solid(1920, 1080, 0, 0, 0);
        for (int y = 0; y < 1080; ++y)
            for (int x = 0; x < 1920; ++x)
                if ((x + y) % 2) { uint8_t *p = &chk.rgba[((size_t)y * 1920 + x) * 4]; p[0] = p[1] = p[2] = 255; }
        assert(prescale(chk, 480, out) && out.width == 960);   // factor 2: each block is half white
        std::printf("      checker → %d\n", px(out, 10, 10)[0]);
        assert(std::abs(px(out, 10, 10)[0] - 188) <= 2);
        std::printf("[PASS] prescale: integer factor keeping 2x the preview edge, linear-light average, never at full size\n");
    }

    void test_effect_mix_zero_is_identity_and_half_is_between()
    {
        Raster a = solid(40, 40, 0, 0, 0);
        for (int x = 0; x < 20; ++x)
            for (int y = 0; y < 40; ++y) { uint8_t *p = &a.rgba[((size_t)y * 40 + x) * 4]; p[0] = p[1] = p[2] = 255; }
        Raster none = a, half = a, full = a;
        applyEffect(runOf("blur.box", {{"radius", 5.0}}, 0.0), 1.0, none);
        applyEffect(runOf("blur.box", {{"radius", 5.0}}, 0.5), 1.0, half);
        applyEffect(runOf("blur.box", {{"radius", 5.0}}, 1.0), 1.0, full);
        assert(none.rgba == a.rgba);
        const int o = px(a, 21, 20)[0], f = px(full, 21, 20)[0], hlf = px(half, 21, 20)[0];
        assert(f > o && std::abs(hlf - (o + f) / 2) <= 1);
        EffectRun bad;
        bad.type = "blur.nonsense";
        assert(!applyEffect(bad, 1.0, a));
        std::printf("[PASS] effects: mix 0 is the input, mix 0.5 is half way; an unknown type is refused\n");
    }
}

int main()
{
    test_every_blur_leaves_a_flat_field_flat();
    test_gaussian_spreads_symmetrically_and_keeps_energy();
    test_directional_blur_follows_its_angle();
    test_zoom_and_spin_keep_their_centre();
    test_effect_mix_zero_is_identity_and_half_is_between();
    test_prescale_is_gamma_correct_and_preview_only();
    test_blend_modes_on_known_operands();
    test_half_scale_contain_lands_centred();
    test_rotation_90_moves_a_known_pixel();
    test_crop_selects_the_right_region();
    test_each_fit_mode();
    test_dissolve_group_sums_to_one_on_screen();
    test_sampling_matches_a_reference_bilinear();
    test_hostile_geometry_stays_in_bounds();
    test_serial_and_parallel_are_byte_identical();
    test_active_source_frame_at_speed_2x();
    test_active_transition_holds_outgoing_and_weights_sum_to_one();
    test_frame_cache();
    test_grade_identity_is_a_byte_identical_passthrough();
    test_grade_exposure_raises_the_mean();
    test_grade_matches_the_slot_sequence_byte_for_byte();
    test_param_hash();
    test_deep_compose_places_layers_on_the_same_pixels();
    test_deep_keeps_what_8_bits_cannot();
    test_camera_curves_put_grey_where_the_vendors_say();
    test_colour_identities_round_trips_and_hdr_levels();
    std::printf("interstellar_render: all tests passed\n");
    return 0;
}
