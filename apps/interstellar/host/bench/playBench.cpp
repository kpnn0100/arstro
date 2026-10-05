/*
 *  interstellar_play_bench — what one frame of PLAYBACK costs, and where (R-PLAY). Not a test.
 *
 *      interstellar_play_bench <project.isp> [seconds] [previewEdge]
 *
 *  Opens the project through the real service with the real decoders, then asks for the timeline
 *  frame by frame at the project's rate — exactly what the monitor asks while playing — at the
 *  preview edge, timing each call. Then the same frames again with the grade switched off (every
 *  source bypassed) to split decode + composite from grading.
 */
#include "FrameSourceFFmpeg.h"
#include "FrameWriterFFmpeg.h"
#include "HostFrameSource.h"
#include "InterstellarService.h"
#include "VideoFrameDecoder.h"
#include "core/ThreadBudget.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

using namespace arstro;
using namespace arstro::interstellar;
using Clock = std::chrono::steady_clock;

int main(int argc, char **argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: interstellar_play_bench <project.isp> [seconds] [edge]\n"); return 2; }
    const double seconds = argc > 2 ? std::atof(argv[2]) : 3.0;
    const int edge = argc > 3 ? std::atoi(argv[3]) : 1600;
    cosmo::ThreadBudget budget(100);
    InterstellarService::Host h;
    h.rackDecoder = [](std::shared_ptr<const FrameSelector> sel) {
        return std::unique_ptr<cosmo::IImageDecoder>(new interstellar_host::VideoFrameDecoder(std::move(sel)));
    };
    h.frameSource = [] { return std::unique_ptr<IFrameSource>(new interstellar_host::HostFrameSource()); };
    h.frameWriter = [] { return std::unique_ptr<IFrameWriter>(new interstellar_host::FrameWriterFFmpeg()); };
    if (std::getenv("BENCH_PLAY")) h.asyncPreview = true;
    InterstellarService svc(budget, h);
    std::string err;
    if (!svc.dispatchText("project open \"" + std::string(argv[1]) + "\"", err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    if (!svc.pumpUntilIdle(60000)) { std::fprintf(stderr, "load timed out\n"); return 1; }
    const NodeId tl = svc.model().currentTimeline;
    const double fps = svc.model().fps > 0 ? svc.model().fps : 24.0;
    auto pass = [&](const char *what, double offset = 0.0) {
        std::vector<double> ms;
        Raster r;
        for (int i = 0; i < (int)(seconds * fps); ++i)
        {
            const auto t0 = Clock::now();
            svc.renderTimelineFrame(tl, 0.5 + offset + i / fps, edge, r);
            ms.push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
        }
        std::vector<double> s = ms;
        std::sort(s.begin(), s.end());
        double sum = 0;
        for (double x : ms) sum += x;
        std::printf("%-26s %3zu frames  mean %6.1f ms  median %6.1f  p95 %6.1f  max %6.1f  → %.1f fps (needs %.0f)  %dx%d\n",
                    what, ms.size(), sum / ms.size(), s[s.size() / 2], s[s.size() * 95 / 100], s.back(), 1000.0 / (sum / ms.size()), fps,
                    r.width, r.height);
    };
    if (std::getenv("BENCH_PLAY"))
    {
        if (std::getenv("BENCH_GPU")) svc.dispatchText("settings set useGpu=1", err);   // R-GPU-1
        // R-PLAY-1: BENCH_CACHE=1 builds the graded preview cache first, then plays from it
        if (std::getenv("BENCH_CACHE"))
        {
            const auto c0 = Clock::now();
            if (!svc.dispatchText("cache build", err) || !svc.dispatchText("wait cache.done --timeout 600s", err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
            std::printf("cache: %d of %d frames in %.1f s\n", svc.model().previewCacheFrames, svc.model().previewCacheTotal,
                        std::chrono::duration<double>(Clock::now() - c0).count());
        }
        // R-PLAY-2: play for `seconds` against the wall clock, the monitor asking every 8 ms
        const auto t0 = Clock::now();
        // `wait` pumps simulated time: start this clock after wherever the service's has got to
        const double base = 1e9;
        auto nowMs = [&] { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count() + base; };
        svc.pump(nowMs());
        if (!svc.dispatchText("play", err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
        Raster r;
        double lastT = -1;
        int shown = 0;
        while (nowMs() - base < seconds * 1000.0 && svc.model().playing)
        {
            svc.pump(nowMs());
            if (svc.model().playhead != lastT)
            {
                lastT = svc.model().playhead;
                svc.renderFrame(lastT, edge, r);
                ++shown;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
        const auto st = svc.playbackStats();
        std::printf("playing %.1f s: %d frames due, %lld exact, %lld shown (%lld from the cache), mean lag %.2f frames, edge %d, pool %.1f fps\n",
                    seconds, shown, st.hits, st.shown, st.fromCache, st.shown ? st.lagFrames / st.shown : 0.0, st.edge, st.rate);
        svc.dispatchText("pause", err);
        return 0;
    }
    pass("graded");
    if (std::getenv("BENCH_GPU"))
    {
        svc.dispatchText("settings set useGpu=1", err);
        std::printf("gpu: %s\n", err.empty() ? "requested" : err.c_str());
        pass("graded, GPU preferred", 2.5);   // other frames: the first pass's are cached
    }
    for (int e : {960, 640})
    {
        const int keep = edge;
        (void)keep;
        std::vector<double> ms;
        Raster r;
        for (int i = 0; i < (int)(seconds * fps); ++i)
        {
            const auto t0 = Clock::now();
            svc.renderTimelineFrame(tl, 0.5 + i / fps, e, r);
            ms.push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
        }
        double sum = 0;
        for (double x : ms) sum += x;
        std::printf("graded at %4d             mean %6.1f ms → %.1f fps  %dx%d\n", e, sum / ms.size(), 1000.0 / (sum / ms.size()), r.width, r.height);
    }
    for (const auto &n : svc.model().rack)
        if (!n.group && !n.bindName.empty()) svc.dispatchText("set " + n.bindName + ".bypass=1", err);
    pass("ungraded (decode+compose)");
    return 0;
}
