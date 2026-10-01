/*
 *  Arstro ImageProcessing Library
 *
 *  TemporalOps: TemporalDenoise, FrameBlend and the temporal driver. See TemporalOps.h for the
 *  effects' definitions and the edge policy.
 *
 *  ── Why the loops are shaped this way ──
 *
 *  Both ops are memory-bound: a radius-2 output frame reads five input frames and writes one,
 *  and at 4K that is ~200 MB of traffic per output frame against a handful of flops per byte.
 *  So the shape is chosen for streaming, not for cleverness:
 *
 *    * the frame pointers are resolved ONCE per window (gatherTaps), the row pointers ONCE per
 *      row, and the x loop is plain pointer arithmetic — nothing in it calls through anything;
 *    * the loop order is neighbour-outer, x-inner, accumulating into a per-row float (or u16)
 *      buffer. An x-outer / neighbour-inner order has a data-dependent trip count in its
 *      innermost loop and does not vectorise; this order gives the compiler one long,
 *      branch-free, unit-stride loop per neighbour, and the row accumulator (≤ 61 KB at 4K)
 *      stays in L2 across the neighbours;
 *    * rows are split into bands with par::parallelFor, each band touching only its own output
 *      rows, so serial and parallel output are byte-identical (Parallel.h's contract).
 */
#include "TemporalOps.h"
#include "base/Parallel.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace arstro
{
    namespace
    {
        inline int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

        /** Resolve the 2k+1 frame pointers an op of radius k reads around `centre`, clamping
         *  taps into the view (edge replication). taps[k] is the centre. False for a view an op
         *  cannot read at all, so every op rejects garbage the same way. */
        bool gatherTaps(const VolumeView &v, int centre, int k, const uint8_t **taps)
        {
            if (v.width <= 0 || v.height <= 0 || v.frames <= 0 || v.frames > VolumeView::kMaxWindow)
                return false;
            if (centre < 0 || centre >= v.frames || k < 0 || k > kMaxTemporalRadius) return false;
            if (v.rowStride < (std::ptrdiff_t)v.width * VolumeView::kChannels) return false;
            for (int i = -k; i <= k; ++i)
            {
                taps[i + k] = v.frame[clampInt(centre + i, 0, v.frames - 1)];
                if (!taps[i + k]) return false;
            }
            return true;
        }

        void prepareOut(const VolumeView &v, FrameRGBA &out)
        {
            out.width = v.width;
            out.height = v.height;
            // resize, not assign: a reused output keeps its pages and pays no zero-fill.
            out.rgba.resize((std::size_t)v.width * v.height * VolumeView::kChannels);
        }

        void failOut(FrameRGBA &out)
        {
            out.rgba.clear();
            out.width = out.height = 0;
        }

        void copyFrame(const uint8_t *src, std::ptrdiff_t stride, int w, int h, uint8_t *dst)
        {
            const std::size_t rowBytes = (std::size_t)w * VolumeView::kChannels;
            par::parallelFor(h, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y)
                    std::memcpy(dst + (std::size_t)y * rowBytes, src + (std::size_t)y * stride, rowBytes);
            });
        }
    }

    // ── TemporalDenoise ────────────────────────────────────────────────────────────────────

    TemporalDenoise::TemporalDenoise(int radius, float strength)
    {
        setRadius(radius);
        setStrength(strength);
    }

    void TemporalDenoise::setRadius(int k) { mRadius = clampInt(k, 0, kMaxTemporalRadius); }

    void TemporalDenoise::setStrength(float s)
    {
        // NaN fails both comparisons; treat it as "off" rather than poisoning every pixel.
        mStrength = (s > 0.f) ? (s < 1.f ? s : 1.f) : 0.f;
    }

    void TemporalDenoise::process(const VolumeView &v, int centre, FrameRGBA &out) const
    {
        const int k = mRadius;
        const uint8_t *taps[VolumeView::kMaxWindow];
        if (!gatherTaps(v, centre, k, taps)) { failOut(out); return; }
        prepareOut(v, out);
        const int w = v.width, h = v.height;
        const std::ptrdiff_t stride = v.rowStride;
        uint8_t *dst = out.rgba.data();

        if (k == 0 || mStrength <= 0.f)
        {
            copyFrame(taps[k], stride, w, h, dst);
            return;
        }

        // The formula in the header, in the units the loop can vectorise. d is computed as the
        // (1,2,1) SUM, i.e. 4d, and the gate as the integer G = round(4h) in the same units, so
        //     w = (1 − (d/h)²)²  =  ((G − m)(G + m) / G²)²,   m = min(4d, G).
        // Why not the float form with a clamp: a float max() is not a select GCC will if-convert
        // without -ffinite-math-only, and that one branch kept the whole loop scalar (~2.9 ns per
        // neighbour-pixel). The integer min is branch-free, (G − m) is ≥ 0 by construction, and
        // it is EXACTLY zero once 4d ≥ G — so the biweight's hard cutoff survives intact. The cost
        // is that h is quantised to a quarter code value.
        const int gate = std::max(1, (int)std::lround(4.f * mStrength * kGateAtFullStrength));
        const float invGate2 = 1.f / ((float)gate * (float)gate);
        const int taps2 = 2 * k + 1;

        par::parallelFor(h, [&](int y0, int y1) {
            // Copied to locals on purpose: the output is stored through a uint8_t*, which may
            // alias ANYTHING, so a bound or scale read through the lambda's references would be
            // reloaded every iteration and the trip count could not be computed — GCC then
            // refuses to vectorise the loop at all (it did, before this).
            const int W = w;
            const std::ptrdiff_t S = stride;
            const int G = gate;
            const float ig2 = invGate2;
            const int K = k, T = taps2;
            const uint8_t *tp[VolumeView::kMaxWindow];
            for (int j = 0; j < T; ++j) tp[j] = taps[j];
            uint8_t *const D = dst;

            // r, g, b, Σw per pixel: 16 bytes, so one pixel is one SIMD lane group.
            std::vector<float> acc((std::size_t)W * 4);
            float *a = acc.data();
            for (int y = y0; y < y1; ++y)
            {
                const uint8_t *c = tp[K] + (std::size_t)y * S;
                for (int x = 0; x < W; ++x)
                {
                    a[4 * x + 0] = c[4 * x + 0];
                    a[4 * x + 1] = c[4 * x + 1];
                    a[4 * x + 2] = c[4 * x + 2];
                    a[4 * x + 3] = 1.f;   // the centre's own weight
                }
                for (int j = 0; j < T; ++j)
                {
                    if (j == K) continue;
                    const uint8_t *n = tp[j] + (std::size_t)y * S;
                    for (int x = 0; x < W; ++x)
                    {
                        const int nr = n[4 * x + 0], ng = n[4 * x + 1], nb = n[4 * x + 2];
                        // Integer abs and min only: both lower branch-free, and any branch in
                        // this loop stops it vectorising.
                        const int sad = std::abs(nr - c[4 * x + 0]) + 2 * std::abs(ng - c[4 * x + 1]) +
                                        std::abs(nb - c[4 * x + 2]);
                        const int m = std::min(sad, G);
                        const float s = (float)(G - m) * (float)(G + m) * ig2;
                        const float wt = s * s;
                        a[4 * x + 0] += wt * (float)nr;
                        a[4 * x + 1] += wt * (float)ng;
                        a[4 * x + 2] += wt * (float)nb;
                        a[4 * x + 3] += wt;
                    }
                }
                uint8_t *o = D + (std::size_t)y * W * VolumeView::kChannels;
                for (int x = 0; x < W; ++x)
                {
                    // Σw ≥ 1 (the centre), so this never divides by zero; a weighted mean of
                    // values in [0,255] stays in [0,255], so +0.5 and truncate is round-to-nearest.
                    const float inv = 1.f / a[4 * x + 3];
                    o[4 * x + 0] = (uint8_t)(int)(a[4 * x + 0] * inv + 0.5f);
                    o[4 * x + 1] = (uint8_t)(int)(a[4 * x + 1] * inv + 0.5f);
                    o[4 * x + 2] = (uint8_t)(int)(a[4 * x + 2] * inv + 0.5f);
                    o[4 * x + 3] = c[4 * x + 3];
                }
            }
        });
    }

    // ── FrameBlend ─────────────────────────────────────────────────────────────────────────

    FrameBlend::FrameBlend(int radius) { setRadius(radius); }

    void FrameBlend::setRadius(int k) { mRadius = clampInt(k, 0, kMaxTemporalRadius); }

    void FrameBlend::process(const VolumeView &v, int centre, FrameRGBA &out) const
    {
        const int k = mRadius;
        const uint8_t *taps[VolumeView::kMaxWindow];
        if (!gatherTaps(v, centre, k, taps)) { failOut(out); return; }
        prepareOut(v, out);
        const int w = v.width, h = v.height;
        const std::ptrdiff_t stride = v.rowStride;
        uint8_t *dst = out.rgba.data();

        if (k == 0)
        {
            copyFrame(taps[0], stride, w, h, dst);
            return;
        }

        const int taps2 = 2 * k + 1;
        // 33 x 255 = 8415 fits u16, so the sum is exact and the widening add vectorises 16-wide.
        // The float reciprocal is exact enough: with an odd tap count the mean is never a tie,
        // it sits at least 1/(2·33) from one, and the float error is below 1e-3.
        const float inv = 1.f / (float)taps2;
        const std::size_t rowElems = (std::size_t)w * VolumeView::kChannels;

        par::parallelFor(h, [&](int y0, int y1) {
            // Locals for the same aliasing reason as in TemporalDenoise::process.
            const std::size_t E = rowElems;
            const std::ptrdiff_t S = stride;
            const float iv = inv;
            const int T = taps2;
            const uint8_t *tp[VolumeView::kMaxWindow];
            for (int j = 0; j < T; ++j) tp[j] = taps[j];
            uint8_t *const D = dst;

            std::vector<uint16_t> acc(E);
            uint16_t *a = acc.data();
            for (int y = y0; y < y1; ++y)
            {
                const uint8_t *r0 = tp[0] + (std::size_t)y * S;
                for (std::size_t i = 0; i < E; ++i) a[i] = r0[i];
                for (int j = 1; j < T; ++j)
                {
                    const uint8_t *r = tp[j] + (std::size_t)y * S;
                    for (std::size_t i = 0; i < E; ++i) a[i] = (uint16_t)(a[i] + r[i]);
                }
                uint8_t *o = D + (std::size_t)y * E;
                for (std::size_t i = 0; i < E; ++i) o[i] = (uint8_t)(int)((float)a[i] * iv + 0.5f);
            }
        });
    }

    // ── the driver ─────────────────────────────────────────────────────────────────────────

    bool temporalWindow(Volume &vol, long long t, TemporalFootprint fp, VolumeView &out, int &centre)
    {
        out = VolumeView{};
        centre = 0;
        if (fp.before < 0 || fp.after < 0) return false;
        const long long n = (long long)fp.before + fp.after + 1;
        if (n > VolumeView::kMaxWindow) return false;
        const VolumeExtent ext = vol.extent();
        if (ext.frames <= 0 || t < 0 || t >= ext.frames) return false;

        // The strict window is the part of the footprint that exists; the padding is pointers.
        const long long a0 = t - fp.before < 0 ? 0 : t - fp.before;
        const long long a1 = t + fp.after > ext.frames - 1 ? ext.frames - 1 : t + fp.after;
        VolumeView real;
        if (!vol.window(a0, a1, real)) return false;

        out = real;
        out.t0 = t - fp.before;
        out.frames = (int)n;
        for (int i = 0; i < (int)n; ++i)
        {
            long long s = out.t0 + i;
            s = s < a0 ? a0 : (s > a1 ? a1 : s);
            out.frame[i] = real.frame[s - a0];
        }
        centre = fp.before;
        return true;
    }

    bool renderTemporal(Volume &vol, const TemporalOp &op, long long t, FrameRGBA &out)
    {
        VolumeView v;
        int centre = 0;
        if (!temporalWindow(vol, t, op.footprint(), v, centre))
        {
            failOut(out);
            return false;
        }
        op.process(v, centre, out);
        return !out.rgba.empty();
    }
}
