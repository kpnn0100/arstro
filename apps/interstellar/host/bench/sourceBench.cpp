/*
 *  interstellar_source_bench — what a source costs, in the shapes the app actually pays it.
 *  Not a test: the numbers behind D-5 and its fix.
 *
 *      interstellar_source_bench <file>…
 *
 *  open        — FrameSourceFFmpeg::open (incl. measuring a stream with no frame count)
 *  sequential  — frameAt(0..N) on one source: playback
 *  scrub       — frameAt at scattered times on ONE source: dragging the playhead
 *  thumbnails  — a FRESH HostFrameSource per still, as the Thumbnailer and the rack decoder do
 */
#include "FrameSourceFFmpeg.h"
#include "HostFrameSource.h"
#include <chrono>
#include <cstdio>
#include <vector>

using namespace arstro;
using Clock = std::chrono::steady_clock;

static double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

int main(int argc, char **argv)
{
    for (int a = 1; a < argc; ++a)
    {
        const char *path = argv[a];
        interstellar_host::FrameSourceFFmpeg src;
        interstellar::IFrameSource::Info info;
        auto t = Clock::now();
        if (!src.open(path, info)) { std::printf("%s: cannot open\n", path); continue; }
        std::printf("%s: %dx%d %.3f fps %lld frames\n", path, info.width, info.height, info.fps, info.frames);
        std::printf("  open                %8.1f ms\n", msSince(t));
        interstellar::Raster r;
        t = Clock::now();
        for (long long f = 0; f < 60; ++f) src.frameAt(f, r);
        std::printf("  sequential 60       %8.1f ms  (%.1f ms/frame)\n", msSince(t), msSince(t) / 60.0);
        const std::vector<double> at = {0.8, 0.25, 0.6, 0.1, 0.9, 0.5, 0.33, 0.75};
        double worst = 0, total = 0;
        for (double f : at)
        {
            t = Clock::now();
            src.frameAt((long long)(f * (info.frames - 1)), r);
            const double ms = msSince(t);
            worst = std::max(worst, ms);
            total += ms;
        }
        std::printf("  scrub 8 seeks       %8.1f ms  (worst %.1f ms)\n", total, worst);
        t = Clock::now();
        for (int k = 0; k < 4; ++k)
        {
            interstellar_host::HostFrameSource h;
            interstellar::IFrameSource::Info hi;
            h.open(path, hi);
            h.frameAt((long long)((0.2 + 0.2 * k) * (hi.frames - 1)), r);
        }
        std::printf("  4 thumbnails        %8.1f ms  (%.1f ms each, fresh source each)\n", msSince(t), msSince(t) / 4.0);
    }
    return 0;
}
