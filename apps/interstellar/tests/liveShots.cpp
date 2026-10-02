/*
 *  interstellar_live_shots — the REAL app bound to the REAL service, headless.
 *
 *  The app's own harness drives it with a fake service, which proves the widgets; this proves the
 *  WIRING the GTK host does — the same four hooks, bound the same way — against a real project
 *  with real media: open it through the app's dispatch, let the hosted Cosmo rack decode, and
 *  render each tab to PNG through a Cairo image surface. It doubles as an integration test:
 *  it fails unless the project reaches the Edit screen, the rack lists its sources, the monitor
 *  received a composited frame, and every shot has content.
 *
 *      interstellar_live_shots --project mv.isp --outdir shots [--size 1440x900] [--select <bind>]
 */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "App.h"
#include "EmbeddedFonts.h"
#include "FrameWriterFFmpeg.h"
#include "HostFrameSource.h"
#include "InterstellarService.h"
#include "PngWriter.h"
#include "Thumbnailer.h"
#include "VideoFrameDecoder.h"
#include "adapter/native/CairoTarget.h"
#include "core/ThreadBudget.h"
#include <cairo/cairo.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <algorithm>
#include <thread>
#include <vector>

using namespace arstro;
using namespace arstro::interstellar;
using arstro::interstellar_v1::App;
using arstro::interstellar_v1::AppHooks;

int main(int argc, char **argv)
{
    std::string project, outdir = ".", select;
    int w = 1440, h = 900;
    bool timing = false;   // --time 1: print the slowest frames of the open (D-6)
    for (int i = 1; i + 1 < argc; i += 2)
    {
        const std::string k = argv[i], v = argv[i + 1];
        if (k == "--project") project = v;
        else if (k == "--outdir") outdir = v;
        else if (k == "--select") select = v;
        else if (k == "--size") std::sscanf(v.c_str(), "%dx%d", &w, &h);
        else if (k == "--time") timing = v == "1";
    }
    if (project.empty()) { std::fprintf(stderr, "usage: interstellar_live_shots --project <p.isp> --outdir <d>\n"); return 2; }
    std::filesystem::create_directories(outdir);

    arstro::cosmo_v2::registerEmbeddedFonts();
    cosmo::ThreadBudget budget(50);
    InterstellarService::Host sh;
    sh.rackDecoder = [](std::shared_ptr<const FrameSelector> sel) {
        return std::unique_ptr<cosmo::IImageDecoder>(new interstellar_host::VideoFrameDecoder(std::move(sel)));
    };
    sh.frameSource = [] { return std::unique_ptr<IFrameSource>(new interstellar_host::HostFrameSource()); };
    sh.frameWriter = [] { return std::unique_ptr<IFrameWriter>(new interstellar_host::FrameWriterFFmpeg()); };
    sh.writeImage = [](const std::string &p, const Raster &r, std::string &err) { return interstellar_host::writePng(p, r, err); };
    sh.asyncPreview = true;   // exactly as the GTK host binds it
    InterstellarService svc(budget, sh);
    interstellar_host::Thumbnailer thumbs;

    // The hooks exactly as linux_main.cpp binds them, plus a counter on the frame path.
    int framesServed = 0, thumbCalls = 0;
    double frameMs = 0, thumbMs = 0;
    AppHooks hooks;
    hooks.model = [&]() -> const AppModel & { return svc.model(); };
    hooks.dispatch = [&](const std::string &line, std::string &err) { return svc.dispatchText(line, err); };
    hooks.renderFrame = [&](double t, int edge, Raster &out) {
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = svc.renderFrame(t, edge, out);
        frameMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (ok && !out.empty()) ++framesServed;
        return ok;
    };
    hooks.thumbnail = [&](const std::string &media, double t, int edge, Raster &out) {
        namespace fs = std::filesystem;
        std::string p = media;
        if (!p.empty() && !fs::path(p).is_absolute() && !svc.model().projectPath.empty())
            p = (fs::path(svc.model().projectPath).parent_path() / p).lexically_normal().string();
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = thumbs.get(p, t, edge, out);
        thumbMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        ++thumbCalls;
        return ok;
    };
    hooks.thumbnailEpoch = [&]() { return thumbs.epoch(); };
    App app(hooks, w, h);

    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t *cr = cairo_create(surf);
    artboard::CairoTarget target;
    double now = 1000.0;
    std::vector<std::pair<double, std::string>> slow;
    auto frame = [&] {
        const auto t0 = std::chrono::steady_clock::now();
        svc.pump(now);
        cairo_save(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
        target.setContext(cr);
        app.render(target, now);
        cairo_surface_flush(surf);
        if (timing)
        {
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            const char *scr = svc.model().screen == Screen::Edit ? "edit" : svc.model().screen == Screen::Loading ? "loading" : "home";
            slow.emplace_back(ms, std::string(scr) + " @" + std::to_string((int)(now - 1000.0)) + "ms");
        }
        now += 16.0;
    };
    auto settle = [&](double ms) {
        for (double t = 0; t < ms; t += 16.0)
        {
            frame();
            if (svc.busy()) std::this_thread::sleep_for(std::chrono::milliseconds(1));   // the rack decodes on a pool
        }
    };
    auto shot = [&](const std::string &name) {
        const std::string path = outdir + "/live_" + name + "_" + std::to_string(w) + "x" + std::to_string(h) + ".png";
        cairo_surface_write_to_png(surf, path.c_str());
        // Content check: a shot that is one flat colour is a failed render, not a design.
        const unsigned char *px = cairo_image_surface_get_data(surf);
        const int stride = cairo_image_surface_get_stride(surf);
        uint32_t first = 0;
        bool varied = false;
        for (int y = 0; y < h && !varied; y += 7)
            for (int x = 0; x < w && !varied; x += 7)
            {
                uint32_t v;
                std::memcpy(&v, px + (size_t)y * stride + (size_t)x * 4, 4);
                if (y == 0 && x == 0) first = v;
                else if (v != first) varied = true;
            }
        std::printf("wrote %s%s\n", path.c_str(), varied ? "" : "  (UNIFORM)");
        assert(varied);
    };

    frame();
    shot("home");
    assert(app.dispatch("project open \"" + project + "\""));
    for (int i = 0; i < 4000 && svc.busy(); ++i) settle(16.0);
    settle(800.0);
    assert(svc.model().screen == Screen::Edit);
    assert(!svc.model().rack.empty());
    // Let the asynchronous stills and the monitor's worker land before the shots.
    for (int i = 0; i < 200 && !thumbs.waitIdle(10); ++i) settle(16.0);
    settle(400.0);
    if (timing)
    {
        std::vector<std::pair<double, std::string>> top = slow;
        std::sort(top.begin(), top.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        double total = 0;
        for (const auto &x : slow) total += x.first;
        std::printf("frames %zu, total %.0f ms; monitor frames %d in %.0f ms; thumbnails %d in %.0f ms; slowest:\n",
                    slow.size(), total, framesServed, frameMs, thumbCalls, thumbMs);
        for (size_t i = 0; i < top.size() && i < 6; ++i) std::printf("  %8.1f ms  %s\n", top[i].first, top[i].second.c_str());
    }
    if (!select.empty()) assert(app.dispatch("rack select " + select));
    assert(app.dispatch("playhead 1.0"));
    settle(800.0);
    shot("grade");
    app.setTab(1);
    settle(800.0);
    shot("cut");
    app.setTab(2);
    settle(800.0);
    shot("deliver");
    std::printf("frames served to the monitor: %d; rack nodes: %zu; screen: edit\n", framesServed,
                svc.model().rack.size());
    assert(framesServed > 0);   // the composited frame reached the monitor through the hook

    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    std::printf("interstellar_live_shots: ok\n");
    return 0;
}
