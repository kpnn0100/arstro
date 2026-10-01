/*
 *  interstellar_render — Composite implementation. See Composite.h for the contract.
 *
 *  The shape of the work, per layer:
 *
 *    1. makePlan: crop -> fit -> scale -> anchor -> rotate -> translate, folded into ONE inverse
 *       affine (output pixel centre -> source coordinate) plus the destination bounding box. All
 *       trigonometry happens here, once.
 *    2. rowSpan: for each row of the box, the run of pixels whose centre maps inside the crop
 *       rectangle, solved from the two linear inequalities and then checked at its ends. The inner
 *       loop therefore never asks "am I inside?".
 *    3. runLayer<Blend, Dissolve, Kind>: the inner loop, one instantiation per combination, over
 *       row bands in parallel. Rows are independent and the band split is deterministic, so a
 *       serial and a parallel composite are byte-identical (R-RENDER-2) — a test holds it to that.
 *
 *  Three sampling kinds, chosen per layer, all giving the bilinear answer:
 *    Copy     unit scale, no rotation, integer offset — the source pixel itself;
 *    Axis     scaled but unrotated — the x taps are the same on every row, so they are computed
 *             once per layer into a table and the y taps once per row (the common case: footage
 *             whose resolution is not the project's, or an unrotated picture-in-picture);
 *    General  rotated — both coordinates stepped per pixel in fixed point.
 *
 *  Arithmetic is integer: source coordinates in 32.32 fixed point, bilinear weights in 8 bits,
 *  alpha in 16 bits (so the two weights of a dissolve still sum to 1 to within 1/65535, not 1/255).
 */
#include "Composite.h"
#include "base/Parallel.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace arstro
{
namespace interstellar
{
namespace render
{
    double blendChannel(Blend mode, double b, double o)
    {
        switch (mode)
        {
            case Blend::Normal: return o;
            case Blend::Multiply: return b * o;
            case Blend::Screen: return 1.0 - (1.0 - b) * (1.0 - o);
            case Blend::Overlay: return b < 0.5 ? 2 * b * o : 1.0 - 2 * (1.0 - b) * (1.0 - o);
            case Blend::Add: return std::min(1.0, b + o);
            case Blend::Subtract: return std::max(0.0, b - o);
            case Blend::Difference: return std::fabs(b - o);
        }
        return o;
    }

    namespace
    {
        constexpr double kFix = 4294967296.0;   // 2^32: source coordinates are 32.32 fixed point
        constexpr int kOpaque = 65535;          // alpha is carried in 16 bits

        enum class Kind { Copy, Axis, General };

        /** A lattice coordinate (centre - 0.5) as integer tap + 8-bit weight. Rounded to the
         *  nearest 1/256 with the carry folded into the integer part, so 2.999 is tap 3, weight 0. */
        inline void tap(long long fixed, int &i, int &w)
        {
            const long long r = fixed + (1LL << 23);
            i = (int)(r >> 32);
            w = (int)((r >> 24) & 0xFF);
        }

        /** Everything the inner loop needs, resolved once per layer. */
        struct Plan
        {
            const uint8_t *src = nullptr;
            int sw = 0, sh = 0;
            // Crop rectangle in continuous source pixels: coverage is "centre maps inside this".
            double cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;
            // Inclusive sample clamp: bilinear taps never read outside the crop.
            int sx0 = 0, sy0 = 0, sx1 = 0, sy1 = 0;
            // Inverse affine of output pixel (X, Y)'s CENTRE: u = a00*X + a01*Y + b0, v likewise.
            double a00 = 0, a01 = 0, b0 = 0, a10 = 0, a11 = 0, b1 = 0;
            // Destination bounding box, half-open, already clipped to the raster.
            int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
            Kind kind = Kind::General;
            // Copy: source pixel = (X + offX, Y + offY).
            int offX = 0, offY = 0;
            // Axis: per output column (index X - bx0), the two source byte offsets and the weight.
            std::vector<int> colA, colB;
            std::vector<uint8_t> colW;
            int opQ = kOpaque;                  // opacity in 16 bits
        };

        inline bool finite(double v) { return std::isfinite(v); }

        bool makePlan(const Layer &l, int W, int H, Plan &p)
        {
            if (!l.src || l.src->empty() || W <= 0 || H <= 0) return false;
            const Raster &s = *l.src;
            if (s.rgba.size() < (size_t)s.width * s.height * 4) return false;
            const Geom &g = l.geom;
            const double fields[] = {g.x, g.y, g.scale, g.rotation, g.anchorX, g.anchorY,
                                     g.cropX, g.cropY, g.cropW, g.cropH, l.opacity};
            for (double f : fields)
                if (!finite(f)) return false;   // a NaN in geometry draws nothing rather than garbage

            const double op = std::min(1.0, std::max(0.0, l.opacity));
            p.opQ = (int)std::lround(op * kOpaque);
            if (p.opQ <= 0) return false;

            // The crop is normalised on the graded frame. A zero-area crop draws nothing: the first
            // version clamped it to 1e-6 and drew a one-pixel sliver stretched across the frame.
            const double cx = std::min(1.0, std::max(0.0, g.cropX));
            const double cy = std::min(1.0, std::max(0.0, g.cropY));
            const double cw = std::min(1.0 - cx, g.cropW);
            const double ch = std::min(1.0 - cy, g.cropH);
            if (!(cw > 0) || !(ch > 0)) return false;
            const double srcW = s.width * cw, srcH = s.height * ch;

            double fx = 1.0, fy = 1.0;
            switch (l.fit)
            {
                case Fit::Contain: fx = fy = std::min(W / srcW, H / srcH); break;
                case Fit::Cover: fx = fy = std::max(W / srcW, H / srcH); break;
                case Fit::Stretch: fx = W / srcW; fy = H / srcH; break;
                case Fit::None: fx = fy = 1.0; break;
            }
            const double scX = fx * g.scale, scY = fy * g.scale;
            // Below a millionth the layer is invisible, and the 32.32 step would overflow.
            if (!(scX >= 1e-6) || !(scY >= 1e-6) || !finite(scX) || !finite(scY)) return false;

            const double drawW = srcW * scX, drawH = srcH * scY;
            const double ax = g.anchorX * drawW, ay = g.anchorY * drawH;
            // The anchor lands at the raster centre plus the offset, so x = y = 0 is centred.
            const double ox = W * 0.5 + g.x, oy = H * 0.5 + g.y;

            const double th = std::fmod(g.rotation, 360.0) * 3.14159265358979323846 / 180.0;
            double c = std::cos(th), sn = std::sin(th);
            // cos(90 deg) is 6e-17, not 0. Snapping makes a quarter turn an exact permutation of
            // pixels instead of a resample that is off by a rounding error everywhere.
            if (std::fabs(c) < 1e-12) c = 0;
            if (std::fabs(sn) < 1e-12) sn = 0;
            if (sn == 0) c = c > 0 ? 1.0 : -1.0;
            if (c == 0) sn = sn > 0 ? 1.0 : -1.0;

            p.src = s.rgba.data();
            p.sw = s.width;
            p.sh = s.height;
            p.cx0 = cx * s.width;
            p.cy0 = cy * s.height;
            p.cx1 = std::min((double)s.width, (cx + cw) * s.width);
            p.cy1 = std::min((double)s.height, (cy + ch) * s.height);
            p.sx0 = std::min(s.width - 1, std::max(0, (int)std::floor(p.cx0)));
            p.sy0 = std::min(s.height - 1, std::max(0, (int)std::floor(p.cy0)));
            p.sx1 = std::min(s.width - 1, std::max(p.sx0, (int)std::ceil(p.cx1) - 1));
            p.sy1 = std::min(s.height - 1, std::max(p.sy0, (int)std::ceil(p.cy1) - 1));

            // Inverse map of a destination point P: d = P - o; q = R(-th) d; u = q + anchor;
            // source = crop origin + u / scale. Evaluated at pixel centres (X + 0.5, Y + 0.5).
            p.a00 = c / scX;
            p.a01 = sn / scX;
            p.b0 = p.cx0 + (c * (0.5 - ox) + sn * (0.5 - oy) + ax) / scX;
            p.a10 = -sn / scY;
            p.a11 = c / scY;
            p.b1 = p.cy0 + (-sn * (0.5 - ox) + c * (0.5 - oy) + ay) / scY;

            // Forward-map the four corners of the drawn rectangle for the bounding box, so only
            // the pixels the layer can touch are ever visited.
            double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
            const double cornersX[4] = {0, drawW, 0, drawW}, cornersY[4] = {0, 0, drawH, drawH};
            for (int i = 0; i < 4; ++i)
            {
                const double px = cornersX[i] - ax, py = cornersY[i] - ay;
                const double X = c * px - sn * py + ox, Y = sn * px + c * py + oy;
                minX = std::min(minX, X); maxX = std::max(maxX, X);
                minY = std::min(minY, Y); maxY = std::max(maxY, Y);
            }
            p.bx0 = (int)std::max(0.0, std::floor(minX));
            p.by0 = (int)std::max(0.0, std::floor(minY));
            p.bx1 = (int)std::min((double)W, std::ceil(maxX));
            p.by1 = (int)std::min((double)H, std::ceil(maxY));
            if (p.bx0 >= p.bx1 || p.by0 >= p.by1) return false;

            // Copy and Axis are pure speed: bilinear at exact pixel centres returns the pixel, and a
            // tap table holds the same taps the general loop would step to.
            if (p.a01 == 0.0 && p.a10 == 0.0)
            {
                const double ex = p.b0 - 0.5, ey = p.b1 - 0.5;
                if (p.a00 == 1.0 && p.a11 == 1.0 && std::fabs(ex - std::round(ex)) < 1e-9 &&
                    std::fabs(ey - std::round(ey)) < 1e-9)
                {
                    p.kind = Kind::Copy;
                    p.offX = (int)std::lround(ex);
                    p.offY = (int)std::lround(ey);
                    return true;
                }
                p.kind = Kind::Axis;
                const int n = p.bx1 - p.bx0;
                p.colA.resize(n);
                p.colB.resize(n);
                p.colW.resize(n);
                for (int k = 0; k < n; ++k)
                {
                    int ix, wx;
                    tap(std::llround((p.a00 * (p.bx0 + k) + p.b0 - 0.5) * kFix), ix, wx);
                    p.colA[k] = std::min(p.sx1, std::max(p.sx0, ix)) * 4;
                    p.colB[k] = std::min(p.sx1, std::max(p.sx0, ix + 1)) * 4;
                    p.colW[k] = (uint8_t)wx;
                }
            }
            return true;
        }

        /** Tighten [lo, hi) — real-valued bounds on integer X — by `c0 <= r + k*X < c1`. */
        inline void clipSpan(double r, double k, double c0, double c1, double &lo, double &hi)
        {
            if (k == 0.0)
            {
                if (!(r >= c0 && r < c1)) hi = lo;   // the whole row is in or out
                return;
            }
            double a = (c0 - r) / k, b = (c1 - r) / k;
            if (k < 0) std::swap(a, b);
            lo = std::max(lo, a);
            hi = std::min(hi, b);
        }

        /** The run of row Y whose pixel centres map inside the crop rectangle, as [x0, x1).
         *  Solved analytically, then each end checked against the exact definition, because a
         *  rounding error in the division must never decide coverage differently from the rule. */
        bool rowSpan(const Plan &p, int Y, int &x0, int &x1)
        {
            const double uRow = p.a01 * Y + p.b0, vRow = p.a11 * Y + p.b1;
            double lo = p.bx0 - 1.0, hi = p.bx1 + 1.0;
            clipSpan(uRow, p.a00, p.cx0, p.cx1, lo, hi);
            clipSpan(vRow, p.a10, p.cy0, p.cy1, lo, hi);
            if (!(hi > lo)) return false;
            x0 = (int)std::max((double)p.bx0, std::min((double)p.bx1, std::ceil(lo)));
            x1 = (int)std::max((double)p.bx0, std::min((double)p.bx1, std::ceil(hi)));
            auto inside = [&](int X) {
                const double u = uRow + p.a00 * X, v = vRow + p.a10 * X;
                return u >= p.cx0 && u < p.cx1 && v >= p.cy0 && v < p.cy1;
            };
            // The covered set along a row is convex, so trimming and growing from the ends is
            // enough. Each loop runs zero or one times in practice.
            while (x0 < x1 && !inside(x0)) ++x0;
            while (x0 < x1 && !inside(x1 - 1)) --x1;
            if (x0 >= x1) return false;
            while (x0 > p.bx0 && inside(x0 - 1)) --x0;
            while (x1 < p.bx1 && inside(x1)) ++x1;
            return true;
        }

        template <Blend M>
        inline int blend8(int b, int o)
        {
            switch (M)
            {
                case Blend::Normal: return o;
                case Blend::Multiply: return (b * o + 127) / 255;
                case Blend::Screen: return b + o - (b * o + 127) / 255;
                case Blend::Overlay:
                    return b < 128 ? (2 * b * o + 127) / 255 : 255 - (2 * (255 - b) * (255 - o) + 127) / 255;
                case Blend::Add: return std::min(255, b + o);
                case Blend::Subtract: return std::max(0, b - o);
                case Blend::Difference: return b > o ? b - o : o - b;
            }
            return o;
        }

        inline uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

        /** round(delta / 65535), symmetric about zero. */
        inline int roundDiv65535(int delta)
        {
            return delta >= 0 ? (delta + 32767) / 65535 : -((-delta + 32767) / 65535);
        }

        /** Composite one sampled pixel `s` into `d`. `bp` is the base the layer mixes AGAINST:
         *  `d` itself normally, the pre-transition snapshot when Dissolve. */
        template <Blend M, bool Dissolve>
        inline void pixel(int s0, int s1, int s2, int s3, uint8_t *d, const uint8_t *bp, int opQ)
        {
            const int a = (s3 * opQ + 127) / 255;   // effective alpha, 16-bit
            if (a == 0) return;
            if (M == Blend::Normal && !Dissolve && a == kOpaque)
            {
                // The overwhelmingly common pixel — an opaque frame at full opacity.
                d[0] = (uint8_t)s0; d[1] = (uint8_t)s1; d[2] = (uint8_t)s2; d[3] = 255;
                return;
            }
            const int ab = bp[3];
            const int sc[3] = {s0, s1, s2};
            for (int c = 0; c < 3; ++c)
            {
                const int b = bp[c], o = sc[c];
                int m = o;
                // W3C separable blending: the mode applies in proportion to the backdrop's
                // coverage, so over nothing a layer is just itself.
                if (M != Blend::Normal) m = (o * (255 - ab) + blend8<M>(b, o) * ab + 127) / 255;
                d[c] = clamp8(d[c] + roundDiv65535((m - b) * a));
            }
            d[3] = clamp8(d[3] + roundDiv65535(a * (255 - ab)));
        }

        template <Blend M, bool Dissolve, Kind K>
        void runLayer(const Plan &p, const Raster *base, Raster &out)
        {
            const int W = out.width;
            uint8_t *dst = out.rgba.data();
            const uint8_t *bse = base ? base->rgba.data() : nullptr;
            const uint8_t *src = p.src;
            const int sw = p.sw;
            par::parallelFor(p.by1 - p.by0, [&](int r0, int r1) {
                for (int r = r0; r < r1; ++r)
                {
                    const int Y = p.by0 + r;
                    int x0, x1;
                    if (!rowSpan(p, Y, x0, x1)) continue;
                    uint8_t *d = dst + ((size_t)Y * W + x0) * 4;
                    const uint8_t *b = Dissolve ? bse + ((size_t)Y * W + x0) * 4 : d;
                    if (K == Kind::Copy)
                    {
                        const uint8_t *s = src + ((size_t)(Y + p.offY) * sw + (x0 + p.offX)) * 4;
                        for (int X = x0; X < x1; ++X, s += 4, d += 4, b += 4)
                            pixel<M, Dissolve>(s[0], s[1], s[2], s[3], d, b, p.opQ);
                        continue;
                    }
                    if (K == Kind::Axis)
                    {
                        // The y taps are the row's; the x taps come from the per-layer table.
                        int iy, wy;
                        tap(std::llround((p.a11 * Y + p.b1 - 0.5) * kFix), iy, wy);
                        const uint8_t *ra = src + (size_t)std::min(p.sy1, std::max(p.sy0, iy)) * sw * 4;
                        const uint8_t *rb = src + (size_t)std::min(p.sy1, std::max(p.sy0, iy + 1)) * sw * 4;
                        const int *ca = p.colA.data() + (x0 - p.bx0), *cb = p.colB.data() + (x0 - p.bx0);
                        const uint8_t *cw = p.colW.data() + (x0 - p.bx0);
                        const int wy1 = wy, wy0 = 256 - wy;
                        for (int X = x0; X < x1; ++X, ++ca, ++cb, ++cw, d += 4, b += 4)
                        {
                            const uint8_t *p00 = ra + *ca, *p10 = ra + *cb, *p01 = rb + *ca, *p11 = rb + *cb;
                            const int wx1 = *cw, wx0 = 256 - wx1;
                            int sm[4];
                            // Separable, and exactly equal to the four-weight form in integers.
                            for (int c = 0; c < 4; ++c)
                                sm[c] = ((p00[c] * wx0 + p10[c] * wx1) * wy0 + (p01[c] * wx0 + p11[c] * wx1) * wy1 +
                                         32768) >> 16;
                            pixel<M, Dissolve>(sm[0], sm[1], sm[2], sm[3], d, b, p.opQ);
                        }
                        continue;
                    }
                    // General (rotated): lattice coordinates (centre - 0.5) in 32.32, stepped per pixel.
                    const double u = p.a01 * Y + p.b0 + p.a00 * x0 - 0.5;
                    const double v = p.a11 * Y + p.b1 + p.a10 * x0 - 0.5;
                    long long U = std::llround(u * kFix), V = std::llround(v * kFix);
                    const long long dU = std::llround(p.a00 * kFix), dV = std::llround(p.a10 * kFix);
                    const int sx0 = p.sx0, sx1 = p.sx1, sy0 = p.sy0, sy1 = p.sy1;
                    for (int X = x0; X < x1; ++X, U += dU, V += dV, d += 4, b += 4)
                    {
                        int ix, iy, wx, wy;
                        tap(U, ix, wx);
                        tap(V, iy, wy);
                        const int xa = std::min(sx1, std::max(sx0, ix)) * 4;
                        const int xb = std::min(sx1, std::max(sx0, ix + 1)) * 4;
                        const int ya = std::min(sy1, std::max(sy0, iy));
                        const int yb = std::min(sy1, std::max(sy0, iy + 1));
                        const uint8_t *ra = src + (size_t)ya * sw * 4, *rb = src + (size_t)yb * sw * 4;
                        const uint8_t *p00 = ra + xa, *p10 = ra + xb, *p01 = rb + xa, *p11 = rb + xb;
                        const int w00 = (256 - wx) * (256 - wy), w10 = wx * (256 - wy);
                        const int w01 = (256 - wx) * wy, w11 = wx * wy;
                        int sm[4];
                        for (int c = 0; c < 4; ++c)
                            sm[c] = (p00[c] * w00 + p10[c] * w10 + p01[c] * w01 + p11[c] * w11 + 32768) >> 16;
                        pixel<M, Dissolve>(sm[0], sm[1], sm[2], sm[3], d, b, p.opQ);
                    }
                }
            });
        }

        template <Blend M, bool Dissolve>
        void runKind(const Plan &p, const Raster *base, Raster &out)
        {
            switch (p.kind)
            {
                case Kind::Copy: runLayer<M, Dissolve, Kind::Copy>(p, base, out); break;
                case Kind::Axis: runLayer<M, Dissolve, Kind::Axis>(p, base, out); break;
                case Kind::General: runLayer<M, Dissolve, Kind::General>(p, base, out); break;
            }
        }

        template <Blend M>
        void runMode(const Plan &p, const Raster *base, Raster &out)
        {
            if (base) runKind<M, true>(p, base, out);
            else runKind<M, false>(p, nullptr, out);
        }

        void placeLayerAgainst(const Layer &l, const Raster *base, Raster &out)
        {
            if (out.empty() || out.rgba.size() < (size_t)out.width * out.height * 4) return;
            if (base && (base->width != out.width || base->height != out.height)) base = nullptr;
            Plan p;
            if (!makePlan(l, out.width, out.height, p)) return;
            switch (l.blend)
            {
                case Blend::Normal: runMode<Blend::Normal>(p, base, out); break;
                case Blend::Multiply: runMode<Blend::Multiply>(p, base, out); break;
                case Blend::Screen: runMode<Blend::Screen>(p, base, out); break;
                case Blend::Overlay: runMode<Blend::Overlay>(p, base, out); break;
                case Blend::Add: runMode<Blend::Add>(p, base, out); break;
                case Blend::Subtract: runMode<Blend::Subtract>(p, base, out); break;
                case Blend::Difference: runMode<Blend::Difference>(p, base, out); break;
            }
        }
    }

    void placeLayer(const Layer &l, Raster &out) { placeLayerAgainst(l, nullptr, out); }

    void compose(const std::vector<Layer> &bottomToTop, int width, int height, Raster &out)
    {
        out.allocate(width, height, 0);
        if (out.empty()) return;
        // A dissolve group mixes every member against the composite as it stood BEFORE the group,
        // so the snapshot is taken once, at the group's first layer, and only during a transition —
        // the cost is one frame copy for the frames that need it and nothing for the rest.
        Raster snapshot;
        bool haveSnapshot = false;
        const size_t n = bottomToTop.size();
        for (size_t i = 0; i < n; ++i)
        {
            const Layer &l = bottomToTop[i];
            const bool member = l.dissolveWithPrevious && i > 0 && haveSnapshot;
            if (!member)
            {
                haveSnapshot = false;
                if (i + 1 < n && bottomToTop[i + 1].dissolveWithPrevious)
                {
                    snapshot = out;
                    haveSnapshot = true;
                }
            }
            placeLayerAgainst(l, member ? &snapshot : nullptr, out);
        }
    }
}
}
}
