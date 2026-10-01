/*
 *  volume_bench — ms per output frame for the temporal ops, for the project ledger.
 *
 *  ── Why the op is timed on a hand-built view ──
 *
 *  The ledger wants the filter's cost with the provider's cost EXCLUDED: decoding is the host's
 *  bill and varies by codec. So the headline numbers point a VolumeView straight at frames that
 *  were generated once up front and time only op.process() — exactly what renderTemporal runs
 *  after the window is resident. A second table walks a real CachedVolume with a memcpy
 *  provider and reports the provider's share separately, so the cache's own overhead is visible
 *  too rather than assumed to be zero.
 *
 *  Frames are noisy, not flat: a flat frame would put every denoise weight in the same branch of
 *  the gate and flatter the numbers. Each timing is the median of many runs after warm-up (the
 *  first call faults in the output pages and spins up the par:: pool).
 */
#include "volume/TemporalOps.h"
#include "volume/Volume.h"
#include "base/Parallel.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace arstro;
using Clock = std::chrono::steady_clock;

namespace
{
    double msSince(Clock::time_point a) { return std::chrono::duration<double, std::milli>(Clock::now() - a).count(); }

    /** A static scene plus cheap per-frame noise (xorshift; quality is irrelevant here). */
    std::vector<FrameRGBA> makeFrames(int w, int h, int n)
    {
        std::vector<FrameRGBA> fr(n);
        for (int f = 0; f < n; ++f)
        {
            fr[f].width = w;
            fr[f].height = h;
            fr[f].rgba.resize((std::size_t)w * h * 4);
            uint32_t s = 0x9e3779b9u ^ (uint32_t)(f * 7919 + 1);
            uint8_t *p = fr[f].rgba.data();
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x, p += 4)
                {
                    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
                    const int nz = (int)(s & 31) - 16;
                    const int base[3] = {60 + (x & 127), 90 + (y & 127), 120 + ((x + y) & 63)};
                    for (int c = 0; c < 3; ++c) p[c] = (uint8_t)std::min(255, std::max(0, base[c] + nz));
                    p[3] = 255;
                }
        }
        return fr;
    }

    struct Timing { double median, min, mean; };

    Timing timeOp(const TemporalOp &op, const VolumeView &v, int centre, int reps)
    {
        FrameRGBA out;
        for (int i = 0; i < 3; ++i) op.process(v, centre, out);   // warm-up
        std::vector<double> ms(reps);
        for (int i = 0; i < reps; ++i)
        {
            const auto t = Clock::now();
            op.process(v, centre, out);
            ms[i] = msSince(t);
        }
        std::vector<double> sorted = ms;
        std::sort(sorted.begin(), sorted.end());
        double sum = 0;
        for (double m : ms) sum += m;
        return {sorted[reps / 2], sorted[0], sum / reps};
    }

    void benchOp(const char *name, const TemporalOp &op, int w, int h, int reps)
    {
        const TemporalFootprint fp = op.footprint();
        const int n = fp.before + fp.after + 1;
        const std::vector<FrameRGBA> fr = makeFrames(w, h, n);
        VolumeView v;
        v.frames = n;
        v.width = w;
        v.height = h;
        v.t0 = 0;
        v.rowStride = (std::ptrdiff_t)w * 4;
        for (int i = 0; i < n; ++i) v.frame[i] = fr[i].rgba.data();
        const Timing t = timeOp(op, v, fp.before, reps);
        const double mb = (double)(n + 1) * w * h * 4 / 1e6;
        std::printf("  %-22s %5dx%-5d  median %7.2f ms  min %7.2f  mean %7.2f   (%4.0f MB moved, %5.1f GB/s at median)\n",
                    name, w, h, t.median, t.min, t.mean, mb, mb / t.median);
    }

    /** The whole path: CachedVolume + renderTemporal over a forward walk, provider = memcpy from
     *  a pre-generated pool. Provider time is measured inside the provider and reported apart. */
    void benchWalk(const char *name, const TemporalOp &op, int w, int h, int frames)
    {
        const std::vector<FrameRGBA> pool = makeFrames(w, h, 8);
        double providerMs = 0;
        CachedVolume vol({w, h, frames, 24.0}, [&](long long f, FrameRGBA &out) {
            const auto t = Clock::now();
            const FrameRGBA &src = pool[(std::size_t)(f % 8)];
            out.width = w;
            out.height = h;
            out.rgba.resize(src.rgba.size());
            std::memcpy(out.rgba.data(), src.rgba.data(), src.rgba.size());
            providerMs += msSince(t);
            return true;
        }, (std::size_t)VolumeView::kMaxWindow * w * h * 4);
        FrameRGBA out;
        // warm-up: the first few frames fault in the cache buffers and the output
        for (long long t = 0; t < 5; ++t) renderTemporal(vol, op, t, out);
        providerMs = 0;
        const long long d0 = vol.decodes();
        const auto t0 = Clock::now();
        for (long long t = 5; t < frames; ++t) renderTemporal(vol, op, t, out);
        const double total = msSince(t0);
        const long long walked = frames - 5;
        const double wd = (double)walked;
        std::printf("  %-22s %5dx%-5d  %7.2f ms/frame total = %6.2f provider + %6.2f cache+op   (%lld decodes / %lld frames)\n",
                    name, w, h, total / wd, providerMs / wd, (total - providerMs) / wd,
                    vol.decodes() - d0, walked);
    }
}

int main()
{
    std::printf("volume_bench — par::threads() = %d, hardware_concurrency-driven (0 = auto)\n", par::threads());
    std::printf("\nop only (pre-decoded frames, provider excluded):\n");
    const TemporalDenoise dn2(2, 0.5f);
    const FrameBlend fb2(2);
    benchOp("TemporalDenoise r2", dn2, 1920, 1080, 60);
    benchOp("TemporalDenoise r2", dn2, 3840, 2160, 30);
    benchOp("FrameBlend r2", fb2, 1920, 1080, 60);
    benchOp("FrameBlend r2", fb2, 3840, 2160, 30);

    std::printf("\nsame ops, single thread (par::setThreads(1)) for scale:\n");
    par::setThreads(1);
    benchOp("TemporalDenoise r2", dn2, 1920, 1080, 10);
    benchOp("FrameBlend r2", fb2, 1920, 1080, 10);
    par::setThreads(0);

    std::printf("\nfull path (CachedVolume walk, memcpy provider timed separately):\n");
    benchWalk("TemporalDenoise r2", dn2, 1920, 1080, 65);
    benchWalk("TemporalDenoise r2", dn2, 3840, 2160, 35);
    benchWalk("FrameBlend r2", fb2, 1920, 1080, 65);
    return 0;
}
