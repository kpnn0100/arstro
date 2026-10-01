/*
 *  volume_tests — the lazy volume and the temporal ops (R-VOL-1..7, R-FX-2).
 *
 *  ── Why the synthetic provider encodes the frame index in the pixels ──
 *
 *  Every claim about the volume is a claim about WHICH source frame ended up WHERE: the right
 *  frames in the window, the edge frame replicated at both clip ends, a shared frame kept across
 *  windows. If the pixel IS the frame index (R,G = index, B = x, A = y), each of those is an
 *  exact integer comparison and no tolerance can hide an off-by-one. The decode counter and the
 *  provider's call log make "decoded once, in order" a number, not an impression.
 *
 *  The two filter tests are measurements, not golden images: denoise must cut the measured noise
 *  stddev on a static sequence by a real factor, and must leave a measured trailing ghost behind
 *  a moving square below a threshold that a plain frame average (also measured, in the same test)
 *  blows straight through — so the ghost probe is shown to be capable of failing.
 *
 *  Plain assert(), with NDEBUG undefined first: a Release build otherwise turns every assertion
 *  into ((void)0) and the suite prints PASS while checking nothing (cosmo's D-43, gene_tests).
 *  main() also refuses to run if assertions are somehow still compiled out.
 */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "volume/TemporalOps.h"
#include "volume/Volume.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace arstro;

namespace
{
    // ── synthetic sources ──────────────────────────────────────────────────────────────────

    /** Frame f: every pixel carries R,G = f (16 bits), B = x, A = y (low bytes). */
    void fillIndexFrame(long long f, int w, int h, FrameRGBA &out)
    {
        out.width = w;
        out.height = h;
        out.rgba.resize((std::size_t)w * h * 4);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                uint8_t *p = out.rgba.data() + ((std::size_t)y * w + x) * 4;
                p[0] = (uint8_t)(f & 0xFF);
                p[1] = (uint8_t)((f >> 8) & 0xFF);
                p[2] = (uint8_t)(x & 0xFF);
                p[3] = (uint8_t)(y & 0xFF);
            }
    }

    long long indexOf(const uint8_t *px) { return (long long)px[0] | ((long long)px[1] << 8); }

    /** An index-encoding provider that logs every call, in order. */
    CachedVolume::Provider indexProvider(int w, int h, std::vector<long long> *log)
    {
        return [w, h, log](long long f, FrameRGBA &out) {
            if (log) log->push_back(f);
            fillIndexFrame(f, w, h, out);
            return true;
        };
    }

    uint64_t mix64(uint64_t z)
    {
        z += 0x9e3779b97f4a7c15ULL;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }

    /** Standard normal sample, a pure function of `key` (Box-Muller over two hashed uniforms),
     *  so a noisy provider is still a pure function of the frame index (R-VOL-7). */
    double gauss(uint64_t key)
    {
        const uint64_t a = mix64(key), b = mix64(key ^ 0xa5a5a5a5deadbeefULL);
        const double u1 = ((double)(a >> 11) + 0.5) / 9007199254740992.0;
        const double u2 = (double)(b >> 11) / 9007199254740992.0;
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
    }

    uint8_t quant(double v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : std::lround(v))); }

    uint64_t noiseKey(long long f, int x, int y, int c)
    {
        return (((uint64_t)f * 1000003ULL + (uint64_t)y) * 1000003ULL + (uint64_t)x) * 4ULL + (uint64_t)c;
    }

    /** The static scene under the noise: a smooth pattern kept well inside [0,255] so ±4σ of
     *  noise never clips (clipping would bias the measured stddev). */
    double staticBase(int x, int y, int c)
    {
        switch (c)
        {
        case 0: return 70.0 + (x % 64);
        case 1: return 110.0 + (y % 48);
        default: return 150.0 + ((x + y) % 40);
        }
    }

    constexpr double kNoiseSigma = 8.0;

    CachedVolume::Provider staticNoisyProvider(int w, int h)
    {
        return [w, h](long long f, FrameRGBA &out) {
            out.width = w;
            out.height = h;
            out.rgba.resize((std::size_t)w * h * 4);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    uint8_t *p = out.rgba.data() + ((std::size_t)y * w + x) * 4;
                    for (int c = 0; c < 3; ++c)
                        p[c] = quant(staticBase(x, y, c) + kNoiseSigma * gauss(noiseKey(f, x, y, c)));
                    p[3] = 255;
                }
            return true;
        };
    }

    // The moving-square scene: a 16x16 square of 220 on 40, moving +4 px per frame, light noise.
    constexpr int kSqW = 96, kSqH = 48, kSqSize = 16, kSqY = 16, kSqStep = 4;
    constexpr double kSqBg = 40.0, kSqFg = 220.0, kSqSigma = 4.0;
    int squareX(long long f) { return 8 + (int)f * kSqStep; }

    CachedVolume::Provider movingSquareProvider()
    {
        return [](long long f, FrameRGBA &out) {
            out.width = kSqW;
            out.height = kSqH;
            out.rgba.resize((std::size_t)kSqW * kSqH * 4);
            const int x0 = squareX(f);
            for (int y = 0; y < kSqH; ++y)
                for (int x = 0; x < kSqW; ++x)
                {
                    const bool in = x >= x0 && x < x0 + kSqSize && y >= kSqY && y < kSqY + kSqSize;
                    uint8_t *p = out.rgba.data() + ((std::size_t)y * kSqW + x) * 4;
                    for (int c = 0; c < 3; ++c)
                        p[c] = quant((in ? kSqFg : kSqBg) + kSqSigma * gauss(noiseKey(f, x, y, c)));
                    p[3] = 255;
                }
            return true;
        };
    }

    /** Mean of channels 0..2 over a rectangle [x0,x1) x [y0,y1). */
    double meanRGB(const FrameRGBA &f, int x0, int x1, int y0, int y1)
    {
        double s = 0;
        long n = 0;
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                for (int c = 0; c < 3; ++c)
                {
                    s += f.rgba[((std::size_t)y * f.width + x) * 4 + c];
                    ++n;
                }
        return s / (double)n;
    }

    /** A test-only op that records exactly what the driver handed it: which source frame sits
     *  in each slot (read back from the pixels), the centre, and t0. */
    class ProbeOp : public TemporalOp
    {
    public:
        explicit ProbeOp(TemporalFootprint fp) : mFp(fp) {}
        TemporalFootprint footprint() const override { return mFp; }
        void process(const VolumeView &v, int centre, FrameRGBA &out) const override
        {
            seen.clear();
            for (int i = 0; i < v.frames; ++i) seen.push_back(indexOf(v.row(i, 0)));
            seenCentre = centre;
            seenT0 = v.t0;
            out.width = v.width;
            out.height = v.height;
            out.rgba.assign((std::size_t)v.width * v.height * 4, 0);
        }
        mutable std::vector<long long> seen;
        mutable int seenCentre = -1;
        mutable long long seenT0 = 0;

    private:
        TemporalFootprint mFp;
    };

    std::size_t frameBytesOf(int w, int h) { return (std::size_t)w * h * 4; }

    // ── tests ──────────────────────────────────────────────────────────────────────────────

    /** window(t0, t1) is inclusive, hands back the right frames at the right t0, with a row
     *  stride that addresses every pixel of every frame correctly. */
    void test_window_returns_right_frames_and_t0()
    {
        const int w = 37, h = 23;   // odd sizes so a wrong stride cannot line up by luck
        CachedVolume vol({w, h, 100, 24.0}, indexProvider(w, h, nullptr), 64 * frameBytesOf(w, h));
        VolumeView v;
        assert(vol.window(10, 14, v));
        assert(v.frames == 5 && v.t0 == 10 && v.width == w && v.height == h);
        assert(v.rowStride == (std::ptrdiff_t)w * 4);
        for (int i = 0; i < v.frames; ++i)
            for (int y = 0; y < h; ++y)
            {
                const uint8_t *r = v.row(i, y);
                for (int x = 0; x < w; ++x)
                {
                    assert(indexOf(r + 4 * x) == 10 + i);
                    assert(r[4 * x + 2] == (uint8_t)x && r[4 * x + 3] == (uint8_t)y);
                }
            }
        // a single-frame window is legal: t0 == t1
        assert(vol.window(99, 99, v) && v.frames == 1 && v.t0 == 99 && indexOf(v.row(0, 0)) == 99);
    }

    /** The volume is strict: anything off the clip, inverted, wider than kMaxWindow, or over the
     *  cap is refused, the view comes back empty, and the volume still works afterwards. */
    void test_window_refuses_what_it_cannot_serve()
    {
        const int w = 8, h = 4;
        CachedVolume vol({w, h, 50, 24.0}, indexProvider(w, h, nullptr), 5 * frameBytesOf(w, h));
        VolumeView v;
        assert(!vol.window(-1, 2, v) && v.frames == 0 && v.frame[0] == nullptr);
        assert(!vol.window(48, 50, v) && v.frames == 0);
        assert(!vol.window(5, 4, v));
        assert(!vol.window(0, 5, v));   // 6 frames > a 5-frame cap
        CachedVolume wide({w, h, 100, 24.0}, indexProvider(w, h, nullptr), 64 * frameBytesOf(w, h));
        assert(!wide.window(0, VolumeView::kMaxWindow, v));   // 34 frames
        assert(wide.window(0, VolumeView::kMaxWindow - 1, v) && v.frames == VolumeView::kMaxWindow);
        assert(vol.window(0, 4, v) && v.frames == 5);
        assert(vol.decodes() == 5);   // the refusals decoded nothing

        // a provider that fails, or returns the wrong size, fails the window cleanly
        CachedVolume failing({w, h, 50, 24.0}, [&](long long f, FrameRGBA &out) {
            fillIndexFrame(f, w, h, out);
            return f != 7;
        }, 8 * frameBytesOf(w, h));
        assert(!failing.window(5, 9, v) && v.frames == 0);
        assert(failing.window(0, 4, v) && indexOf(v.row(4, 0)) == 4);
        CachedVolume wrongSize({w, h, 50, 24.0}, indexProvider(w + 1, h, nullptr), 8 * frameBytesOf(w + 1, h));
        assert(!wrongSize.window(0, 0, v));
    }

    /** The driver clamps at BOTH clip ends by replicating the edge frame, keeps the centre slot
     *  fixed at `before`, and refuses a t that is not in the clip. */
    void test_driver_clamps_at_both_clip_ends()
    {
        const int w = 4, h = 2;
        CachedVolume vol({w, h, 100, 24.0}, indexProvider(w, h, nullptr), 64 * frameBytesOf(w, h));
        ProbeOp p({2, 2});
        FrameRGBA out;
        typedef std::vector<long long> V;

        assert(renderTemporal(vol, p, 0, out));
        assert(p.seen == V({0, 0, 0, 1, 2}) && p.seenCentre == 2 && p.seenT0 == -2);
        assert(renderTemporal(vol, p, 1, out) && p.seen == V({0, 0, 1, 2, 3}));
        assert(renderTemporal(vol, p, 50, out) && p.seen == V({48, 49, 50, 51, 52}) && p.seenT0 == 48);
        assert(renderTemporal(vol, p, 98, out) && p.seen == V({96, 97, 98, 99, 99}));
        assert(renderTemporal(vol, p, 99, out) && p.seen == V({97, 98, 99, 99, 99}));
        assert(!renderTemporal(vol, p, 100, out) && out.rgba.empty());
        assert(!renderTemporal(vol, p, -1, out));

        // an asymmetric footprint keeps its centre at `before`
        ProbeOp q({3, 1});
        assert(renderTemporal(vol, q, 1, out) && q.seen == V({0, 0, 0, 1, 2}) && q.seenCentre == 3);
        assert(renderTemporal(vol, q, 99, out) && q.seen == V({96, 97, 98, 99, 99}));

        // a one-frame clip: every slot is the only frame
        CachedVolume one({w, h, 1, 24.0}, indexProvider(w, h, nullptr), 64 * frameBytesOf(w, h));
        assert(renderTemporal(one, p, 0, out) && p.seen == V({0, 0, 0, 0, 0}));
        assert(one.decodes() == 1);

        // a footprint wider than one view is refused, not truncated
        ProbeOp tooWide({16, 17});
        assert(!renderTemporal(vol, tooWide, 50, out));
    }

    /** Residency is bounded by the footprint, never by clip length: walking 1000 frames keeps
     *  it ≤ the cap AND ≤ (before+after+1) frames, and a 1e12-frame clip peaks at exactly the
     *  same number as a 1002-frame one. */
    void test_residency_bounded_by_footprint_not_clip_length()
    {
        const int w = 64, h = 36;
        const std::size_t fb = frameBytesOf(w, h);
        const std::size_t cap = 64 * fb;   // generous: the bound must come from the footprint
        const TemporalFootprint fps[] = {{2, 2}, {4, 4}, {3, 0}};
        for (const TemporalFootprint &fp : fps)
        {
            const std::size_t bound = (std::size_t)(fp.before + fp.after + 1) * fb;
            std::size_t peak[2] = {0, 0};
            const long long lengths[2] = {1002, 1000000000000LL};
            for (int c = 0; c < 2; ++c)
            {
                CachedVolume vol({w, h, lengths[c], 24.0}, indexProvider(w, h, nullptr), cap);
                ProbeOp p(fp);
                FrameRGBA out;
                for (long long t = 0; t < 1000; ++t)
                {
                    assert(renderTemporal(vol, p, t, out));
                    assert(vol.residentBytes() <= cap);
                    assert(vol.residentBytes() <= bound);
                    assert(vol.residentFrames() <= (std::size_t)(fp.before + fp.after + 1));
                    if (vol.residentBytes() > peak[c]) peak[c] = vol.residentBytes();
                }
            }
            assert(peak[0] == peak[1] && peak[0] == bound);
        }
    }

    /** A forward walk decodes each source frame exactly once, in ascending order — even with the
     *  cap at exactly one window — so the sequential decoder never seeks. A backward walk is
     *  also once per frame; a single backward STEP costs exactly one decode. */
    void test_forward_walk_decodes_each_frame_once()
    {
        const int w = 16, h = 9;
        const long long N = 1000;
        for (std::size_t capFrames : {std::size_t(5), std::size_t(64)})
        {
            std::vector<long long> log;
            CachedVolume vol({w, h, N, 24.0}, indexProvider(w, h, &log), capFrames * frameBytesOf(w, h));
            TemporalDenoise dn(2, 0.5f);
            FrameRGBA out;
            for (long long t = 0; t < N; ++t) assert(renderTemporal(vol, dn, t, out));
            assert(vol.decodes() == N);
            assert((long long)log.size() == N);
            for (long long i = 0; i < N; ++i) assert(log[i] == i);
        }
        {
            std::vector<long long> log;
            CachedVolume vol({w, h, N, 24.0}, indexProvider(w, h, &log), 5 * frameBytesOf(w, h));
            FrameBlend fb(2);
            FrameRGBA out;
            for (long long t = N - 1; t >= 0; --t) assert(renderTemporal(vol, fb, t, out));
            assert(vol.decodes() == N);
            const long long before = vol.decodes();
            assert(renderTemporal(vol, fb, 1, out));   // one step forward from t = 0 ...
            assert(renderTemporal(vol, fb, 0, out));   // ... and one back
            assert(vol.decodes() == before + 1);       // [0..3] ⊃ [0..2]: one new frame, then none
        }
    }

    /** Materialising a window never evicts a frame inside it, and frames shared with the previous
     *  window are the SAME buffers (pointers into the cache, never copies). */
    void test_cache_never_evicts_inside_the_window()
    {
        const int w = 8, h = 4;
        std::vector<long long> log;
        CachedVolume vol({w, h, 100, 24.0}, indexProvider(w, h, &log), 5 * frameBytesOf(w, h));
        VolumeView a;
        assert(vol.window(8, 12, a));
        const uint8_t *p10 = a.frame[2], *p11 = a.frame[3], *p12 = a.frame[4];
        VolumeView b;
        assert(vol.window(10, 14, b));   // cap is one window: 8,9 must go, 10..12 must stay
        assert(vol.decodes() == 7);
        assert(log == std::vector<long long>({8, 9, 10, 11, 12, 13, 14}));
        assert(b.frame[0] == p10 && b.frame[1] == p11 && b.frame[2] == p12);
        for (int i = 0; i < 5; ++i) assert(indexOf(b.row(i, h - 1)) == 10 + i);
        // a jump replaces the whole window by recycling its buffers: residency does not grow
        assert(vol.window(60, 64, b) && vol.decodes() == 12);
        assert(vol.residentBytes() == 5 * frameBytesOf(w, h));
        for (int i = 0; i < 5; ++i) assert(indexOf(b.row(i, 0)) == 60 + i);
    }

    /** R-VOL-7: same t, same pixels, whatever the access order. */
    void test_residency_is_invisible()
    {
        const int w = 48, h = 32;
        const TemporalDenoise dn(2, 1.f);
        CachedVolume walked({w, h, 200, 24.0}, staticNoisyProvider(w, h), 8 * frameBytesOf(w, h));
        FrameRGBA viaWalk, fresh, viaJump;
        for (long long t = 0; t <= 120; ++t) assert(renderTemporal(walked, dn, t, viaWalk));
        CachedVolume direct({w, h, 200, 24.0}, staticNoisyProvider(w, h), 8 * frameBytesOf(w, h));
        assert(renderTemporal(direct, dn, 120, fresh));
        assert(viaWalk.rgba == fresh.rgba);
        assert(renderTemporal(direct, dn, 3, viaJump) && renderTemporal(direct, dn, 120, viaJump));
        assert(viaJump.rgba == fresh.rgba);
    }

    /** Denoise cuts the measured noise on a STATIC noisy sequence by a real factor, without
     *  bias; strength 0 and radius 0 are exact copies of the centre frame. */
    void test_denoise_reduces_noise_on_static_sequence()
    {
        const int w = 160, h = 120;
        // 9 frames: the radius-4 pass needs a 9-frame window (an 8-frame cap is refused — rightly)
        CachedVolume vol({w, h, 40, 24.0}, staticNoisyProvider(w, h), 9 * frameBytesOf(w, h));
        const long long t = 20;
        auto stats = [&](const uint8_t *px, double &mean, double &sd) {
            double s = 0, s2 = 0;
            long n = 0;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    for (int c = 0; c < 3; ++c)
                    {
                        const double d = px[((std::size_t)y * w + x) * 4 + c] - staticBase(x, y, c);
                        s += d;
                        s2 += d * d;
                        ++n;
                    }
            mean = s / (double)n;
            sd = std::sqrt(s2 / (double)n - mean * mean);
        };

        VolumeView v;
        assert(vol.window(t, t, v));
        double inMean, inSd;
        stats(v.row(0, 0), inMean, inSd);

        FrameRGBA out;
        assert(renderTemporal(vol, TemporalDenoise(2, 1.f), t, out));
        double outMean, outSd;
        stats(out.rgba.data(), outMean, outSd);
        FrameRGBA out4;
        assert(renderTemporal(vol, TemporalDenoise(4, 1.f), t, out4));
        double out4Mean, out4Sd;
        stats(out4.rgba.data(), out4Mean, out4Sd);
        std::printf("      static noise stddev: input %.3f -> radius 2: %.3f (x%.2f), radius 4: %.3f (x%.2f)\n",
                    inSd, outSd, inSd / outSd, out4Sd, inSd / out4Sd);

        assert(inSd > 7.0 && inSd < 9.0);   // the fixture really is sigma-8 noise
        assert(outSd < 0.55 * inSd);        // sqrt(5) is the ceiling: 0.447; demand most of it
        assert(out4Sd < 0.40 * inSd);       // and a wider window must do better (ceiling 0.333)
        assert(std::fabs(outMean) < 0.5 && std::fabs(out4Mean) < 0.5);

        FrameRGBA centre, off;
        assert(renderTemporal(vol, FrameBlend(0), t, centre));
        assert(renderTemporal(vol, TemporalDenoise(2, 0.f), t, off) && off.rgba == centre.rgba);
        assert(renderTemporal(vol, TemporalDenoise(0, 1.f), t, off) && off.rgba == centre.rgba);
    }

    /** Denoise does not smear a MOVING bright square: the trailing (and leading) strip next to it
     *  stays at the background level, while a plain frame average — measured with the same probe
     *  in the same test — leaves a ghost an order of magnitude larger. */
    void test_denoise_does_not_smear_moving_square()
    {
        CachedVolume vol({kSqW, kSqH, 20, 24.0}, movingSquareProvider(), 8 * frameBytesOf(kSqW, kSqH));
        const long long t = 10;
        const int x0 = squareX(t);
        const int y0 = kSqY, y1 = kSqY + kSqSize;
        // where the square was in t-1, t-2 / will be in t+1, t+2, but is not at t
        const int trail0 = x0 - 2 * kSqStep, trail1 = x0;
        const int lead0 = x0 + kSqSize, lead1 = x0 + kSqSize + 2 * kSqStep;
        auto ghost = [&](const FrameRGBA &f, int a, int b) {
            return meanRGB(f, a, b, y0, y1) - meanRGB(f, 0, 16, y0, y1);   // vs far background
        };

        FrameRGBA dn, avg;
        assert(renderTemporal(vol, TemporalDenoise(2, 1.f), t, dn));
        assert(renderTemporal(vol, FrameBlend(2), t, avg));
        const double dnTrail = ghost(dn, trail0, trail1), dnLead = ghost(dn, lead0, lead1);
        const double avgTrail = ghost(avg, trail0, trail1);
        const double dnInside = meanRGB(dn, x0, x0 + kSqSize, y0, y1);
        std::printf("      moving square: trailing ghost denoise %.3f, leading %.3f, frame-average %.3f;"
                    " square interior %.2f (true %.0f)\n",
                    dnTrail, dnLead, avgTrail, dnInside, kSqFg);

        assert(avgTrail > 30.0);              // the probe can see a ghost (analytic: 54)
        assert(std::fabs(dnTrail) < 2.0);     // denoise leaves none
        assert(std::fabs(dnLead) < 2.0);
        assert(std::fabs(dnInside - kSqFg) < 2.0);   // nor drags background into the square
    }

    /** FrameBlend of a known per-frame ramp is the exact (rounded) analytic mean — interior and
     *  at the replicated clip ends — with zero tolerance, on all four channels. */
    void test_frame_blend_equals_analytic_mean()
    {
        const int w = 16, h = 8;
        const long long N = 30;
        // pixel value = 7f + (x + 2y) on RGB, 3f + x on A: no wrap for f < 30
        auto value = [](long long f, int x, int y, int c) {
            return c < 3 ? (int)(7 * f) + x + 2 * y + c : (int)(3 * f) + x;
        };
        CachedVolume vol({w, h, N, 24.0}, [&](long long f, FrameRGBA &out) {
            out.width = w;
            out.height = h;
            out.rgba.resize((std::size_t)w * h * 4);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    for (int c = 0; c < 4; ++c) out.rgba[((std::size_t)y * w + x) * 4 + c] = (uint8_t)value(f, x, y, c);
            return true;
        }, 16 * frameBytesOf(w, h));

        for (int k : {1, 2, 3})
            for (long long t : {0LL, 1LL, 2LL, 15LL, N - 2, N - 1})
            {
                FrameRGBA out;
                assert(renderTemporal(vol, FrameBlend(k), t, out));
                const int n = 2 * k + 1;
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                        for (int c = 0; c < 4; ++c)
                        {
                            long sum = 0;
                            for (long long f = t - k; f <= t + k; ++f)
                                sum += value(f < 0 ? 0 : (f > N - 1 ? N - 1 : f), x, y, c);
                            const long expect = (sum + n / 2) / n;   // n odd: never a tie
                            assert(out.rgba[((std::size_t)y * w + x) * 4 + c] == expect);
                        }
            }
        // and the interior mean of a linear ramp is the centre value, exactly
        FrameRGBA out;
        assert(renderTemporal(vol, FrameBlend(2), 15, out) && out.rgba[0] == 7 * 15);
    }

    /** Freeze is a t-remap: plays to freezeAt, then holds — and a held stretch is byte-identical
     *  to the held frame and costs no decodes after its first frame. */
    void test_freeze_remap()
    {
        assert(freezeRemap(5, 10) == 5);
        assert(freezeRemap(10, 10) == 10);
        assert(freezeRemap(11, 10) == 10);
        assert(freezeRemap(1000000000000LL, 10) == 10);
        assert(freezeRemap(0, 0) == 0);

        const int w = 24, h = 12;
        CachedVolume vol({w, h, 100, 24.0}, staticNoisyProvider(w, h), 8 * frameBytesOf(w, h));
        const FrameBlend blend(2);
        FrameRGBA held, out;
        assert(renderTemporal(vol, blend, 20, held));
        const long long decodes = vol.decodes();
        for (long long t = 20; t < 60; ++t)
        {
            assert(renderTemporal(vol, blend, freezeRemap(t, 20), out));
            assert(out.rgba == held.rgba);
        }
        assert(vol.decodes() == decodes);
        assert(renderTemporal(vol, blend, freezeRemap(19, 20), out) && out.rgba != held.rgba);
    }
}

#define RUN(fn)                           \
    do                                    \
    {                                     \
        fn();                             \
        std::printf("PASS  %s\n", #fn);   \
        ++passed;                         \
    } while (0)

int main()
{
    // Line-buffered, so the PASS lines before a failing assert survive the abort.
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    bool live = false;
    assert((live = true));
    if (!live)
    {
        std::printf("FAIL  assertions are compiled out — this suite would check nothing\n");
        return 1;
    }
    int passed = 0;
    RUN(test_window_returns_right_frames_and_t0);
    RUN(test_window_refuses_what_it_cannot_serve);
    RUN(test_driver_clamps_at_both_clip_ends);
    RUN(test_residency_bounded_by_footprint_not_clip_length);
    RUN(test_forward_walk_decodes_each_frame_once);
    RUN(test_cache_never_evicts_inside_the_window);
    RUN(test_residency_is_invisible);
    RUN(test_denoise_reduces_noise_on_static_sequence);
    RUN(test_denoise_does_not_smear_moving_square);
    RUN(test_frame_blend_equals_analytic_mean);
    RUN(test_freeze_remap);
    std::printf("volume_tests: %d passed (assertions live)\n", passed);
    return 0;
}
