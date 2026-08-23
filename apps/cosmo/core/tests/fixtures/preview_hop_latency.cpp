/*
 *  D-44 / D-45 fixture: what does "changing photo" actually cost, and which part is the variance?
 *
 *  Reported as "sometime changing photo take too long", with a log showing one hop at 250 ms and
 *  the next at 2537 ms. This times the two things separately, because they have different causes
 *  and different fixes:
 *
 *    * the COLD-HOP PATH, stage by stage — LibRaw decode, the 8-bit -> linear-float conversion,
 *      the proxy downscale, and the render — so the 1.8 s is attributed rather than guessed at;
 *    * a WALK over a real project, timing `select` -> `frame.ready` per hop and printing whether
 *      the engine had to re-decode (`rehydrations`), which is what makes it "sometimes".
 *
 *  Build it against the tree you already built, rather than by hand-compiling the engine:
 *
 *      g++ -O2 -std=c++17 -DARSTRO_ENABLE_THREADS \
 *          -I apps/cosmo -I apps/cosmo/core -I core/ImageProcessing/src \
 *          -o /tmp/hoplat apps/cosmo/core/tests/fixtures/preview_hop_latency.cpp \
 *          build-mingw64/apps/cosmo/core/libcosmo_core.a \
 *          build-mingw64/core/ImageProcessing/libarstro_image.a \
 *          $(pkg-config --cflags --libs gtk+-3.0) -lpthread
 *
 *      /tmp/hoplat <one.ARW>                 # the stage breakdown
 *      /tmp/hoplat <one.ARW> <project.cmp>   # ...and then walk the project
 *
 *  Measured 2026-08-23, 16 cores, Sony 24 MP ARW, previewEdge 1600, cpuPercent 50:
 *
 *      1. LibRaw decode -> RGBA8 6024x4024      963 ms   (92 MB)
 *      2. -> linear float source                129 ms   (369 MB)
 *      3. add + proxy + first render            751 ms   (proxy 26 MB)
 *      4. render again, proxy warm              617 ms   <- the floor, even fully cached
 *
 *      walk over a 120-photo project, after the load:
 *      node=7  1622.9 ms  rehydrated=1     node=6  (revisit)  683.0 ms
 *      node=8  1866.6 ms  rehydrated=1     node=61 (revisit)  660.9 ms
 *      8 of 10 hops re-decoded.
 *
 *  So a hop is ~1.8 s cold and ~0.65 s warm, and the difference is one LibRaw decode the cache
 *  should have made unnecessary (D-44). The 0.65 s that remains is the render itself and is a
 *  separate finding (D-45).
 */
#include "core/service/CosmoService.h"
#include "core/AppSettings.h"
#include "core/ThreadBudget.h"
#include "core/decode/NativeImageDecoder.h"
#include "PixelBudget.h"
#include "engine/EditEngine.h"
#include "base/Parallel.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

using namespace arstro;
using namespace arstro::cosmo;

namespace
{
    using Clock = std::chrono::steady_clock;
    double msSince(Clock::time_point t0)
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    }
    size_t mb(size_t bytes) { return bytes / (1024 * 1024); }

    void stages(const char *path)
    {
        par::setThreads(0);   // auto; pin it so this is not measuring somebody else's budget
        std::printf("the cold-preview path, stage by stage (engine threads=%d)\n", par::threads());

        NativeImageDecoder dec;
        auto t0 = Clock::now();
        DecodedImage d = dec.decodeFile(path);
        if (!d.ok()) { std::printf("  decode failed: %s\n", path); return; }
        std::printf("  1. LibRaw decode -> RGBA8 %dx%d   %8.1f ms  (%zu MB)\n",
                    d.width, d.height, msSince(t0), mb(d.rgba.size()));

        t0 = Clock::now();
        Image src = EditEngine::fromEncodedBytes(d.rgba.data(), d.width, d.height, 4);
        const double convertMs = msSince(t0);
        std::printf("  2. -> linear float source         %8.1f ms  (%zu MB)\n",
                    convertMs, mb(src.pixelCount() * src.channels() * sizeof(Pixel)));

        EditEngine eng;
        eng.setPreviewSize(1600);
        t0 = Clock::now();
        const int slot = eng.addImage(d.rgba.data(), d.width, d.height, 4);
        eng.selectImage(slot);
        PreviewBuffer pb = eng.renderPreview();
        std::printf("  3. add + proxy + first render     %8.1f ms  (proxy %zu MB)\n",
                    msSince(t0), mb(eng.residentProxyBytes()));

        t0 = Clock::now();
        pb = eng.renderPreview();
        std::printf("  4. render again, proxy warm       %8.1f ms   <- the floor, fully cached\n",
                    msSince(t0));
        (void)pb;
    }

    void walk(const char *project)
    {
        std::printf("\nwalking %s\n", project);
        ThreadBudget budget(50, 0);
        CosmoService svc(budget);
        svc.setDecoderFactory([] {
            return std::unique_ptr<IImageDecoder>(new NativeImageDecoder());
        });
        AppSettings st;
        st.previewEdge = 1600; st.cpuPercent = 50; st.useGpu = false;
        svc.applySettings(st);
        // The same pixel-cache policy the hosts apply (R-MEM-1/5). Without it this measures the
        // engine's blind defaults rather than what the app actually does, which is how D-44's
        // first re-measurement still showed the early photos re-decoding.
        const auto caps = cosmo_v2::pixelCapsForThisMachine();
        svc.session().renderService().setMemoryCaps(caps.sourceBytes, caps.proxyBytes);
        std::printf("  caps: %zu MB sources + %zu MB proxies (of %zu MB physical)\n",
                    caps.sourceBytes / (1024 * 1024), caps.proxyBytes / (1024 * 1024),
                    cosmo_v2::physicalMemoryBytes() / (1024 * 1024));

        std::string err;
        if (!svc.dispatchText(std::string("project open ") + project, err))
        { std::printf("  open failed: %s\n", err.c_str()); return; }

        double t = 0;
        auto pump = [&](int ms) {
            for (int i = 0; i < ms / 4; ++i)
            {
                svc.pump(t); t += 4.0;
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
            }
        };
        const auto loadDeadline = Clock::now() + std::chrono::seconds(600);
        while (svc.model().load.active && Clock::now() < loadDeadline) pump(20);
        std::printf("  loaded %d images, resident %zu MB, rehydrations %d\n",
                    svc.model().imageCount, mb(svc.model().budget.residentBytes),
                    svc.model().budget.rehydrations);

        // Hop the way a photographer does: a few steps, a jump, a revisit.
        const int hops[] = {5, 6, 7, 60, 61, 5, 90, 91, 92, 60};
        for (int h : hops)
        {
            if (h >= (int)svc.model().nodes.size()) continue;
            const int node = svc.model().nodes[h].node;
            const unsigned seq0 = svc.model().frameSeq;
            const int rehy0 = svc.model().budget.rehydrations;
            const auto t0 = Clock::now();
            if (!svc.dispatchText("select " + std::to_string(node), err)) continue;
            const auto deadline = Clock::now() + std::chrono::seconds(30);
            while (svc.model().frameSeq == seq0 && Clock::now() < deadline)
            {
                svc.pump(t); t += 4.0;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            std::printf("  node=%-4d slot=%-4d %8.1f ms   rehydrated=%d  resident=%zu MB\n",
                        node, svc.model().currentSlot, msSince(t0),
                        svc.model().budget.rehydrations - rehy0,
                        mb(svc.model().budget.residentBytes));
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::printf("usage: %s <one.ARW> [project.cmp]\n", argv[0]);
        return 2;
    }
    stages(argv[1]);
    if (argc > 2) walk(argv[2]);
    return 0;
}
