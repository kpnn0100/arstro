/*
 *  arstro-android-shell — entry point.
 *
 *  M1.1 scaffolded the host + build. M1.2 adds SurfaceHost: it opens ONE surface (a
 *  gtk-layer-shell top-level where available, else a plain window under --windowed),
 *  wraps an Artboard CairoTarget + one root segment, and renders it. The shared frame
 *  clock (M1.3), GDK->RawPointer input (M1.4), ShellState (M1.5), and all five surfaces
 *  at once (M1.6) come next. See launcher/docs/android-theme-plan.md §3.1.
 *
 *  Modes:
 *    (default)            open surface --surface=N (default 0) as a GTK window + main loop
 *    --windowed           force a plain top-level even if gtk-layer-shell is available
 *    --surface=N          pick which of defaultSurfaces() to open / render
 *    --size=WxH           window/render size (default: the surface's config size or 1080x2160)
 *    --render-png=PATH    headless: render the surface to a PNG and exit (no display needed)
 *    --self-test          headless self-check (GTK init if present, Artboard + draw path), exit
 */
#include <gtk/gtk.h>
#include <cairo/cairo.h>
#include "artboard/artboard.h"
#include "SurfaceHost.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

using arstro::androidshell::SurfaceConfig;
using arstro::androidshell::SurfaceHost;
using arstro::androidshell::defaultSurfaces;

namespace
{
    // A stand-in surface root until the theme + real surfaces land: draws the surface's
    // name so each --surface=N is identifiable on screen and in golden PNGs.
    struct PlaceholderRoot : artboard::Segment
    {
        std::string label;
        void onPaint(artboard::IRenderTarget &t) const override
        {
            t.setFill(artboard::Color::rgba(230, 224, 233));
            t.drawText(label, 32, 72, 34, "", 1.0);
            t.drawText("arstro-android-shell \xE2\x80\x94 M1.2 placeholder surface", 32, 116, 16, "", 0.0);
        }
    };

    struct Options
    {
        bool selfTest = false;
        bool windowed = false;
        int surface = 0;
        int width = 0, height = 0;      // 0 = use config default
        std::string renderPng;          // non-empty = headless PNG render mode
    };

    Options parseArgs(int argc, char **argv)
    {
        Options o;
        for (int i = 1; i < argc; ++i)
        {
            const std::string a = argv[i];
            if (a == "--self-test") o.selfTest = true;
            else if (a == "--windowed") o.windowed = true;
            else if (a.rfind("--surface=", 0) == 0) o.surface = std::atoi(a.c_str() + 10);
            else if (a.rfind("--render-png=", 0) == 0) o.renderPng = a.substr(13);
            else if (a.rfind("--size=", 0) == 0)
            {
                const char *v = a.c_str() + 7;
                const char *x = std::strchr(v, 'x');
                if (x) { o.width = std::atoi(v); o.height = std::atoi(x + 1); }
            }
        }
        return o;
    }

    const SurfaceConfig &pickSurface(int n)
    {
        const auto &all = defaultSurfaces();
        if (n < 0 || n >= (int)all.size()) n = 0;
        return all[n];
    }

    std::shared_ptr<PlaceholderRoot> makeRoot(const SurfaceConfig &cfg)
    {
        auto root = std::make_shared<PlaceholderRoot>();
        root->label = cfg.name;
        return root;
    }

    // Render a surface to a PNG with no display (L1-style golden path). Returns 0 on success.
    int renderToPng(const Options &o)
    {
        const SurfaceConfig &cfg = pickSurface(o.surface);
        const int w = o.width > 0 ? o.width : (cfg.width > 0 ? cfg.width : 1080);
        const int h = o.height > 0 ? o.height : (cfg.height > 0 ? cfg.height : 2160);

        SurfaceHost host(cfg);
        host.setRoot(makeRoot(cfg));

        cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cairo_t *cr = cairo_create(s);
        host.paint(cr, w, h);
        const cairo_status_t drawStatus = cairo_status(cr);
        const cairo_status_t writeStatus = cairo_surface_write_to_png(s, o.renderPng.c_str());
        cairo_destroy(cr);
        cairo_surface_destroy(s);

        if (drawStatus != CAIRO_STATUS_SUCCESS || writeStatus != CAIRO_STATUS_SUCCESS)
        {
            std::fprintf(stderr, "arstro-android-shell: render-png failed (draw=%s write=%s)\n",
                         cairo_status_to_string(drawStatus), cairo_status_to_string(writeStatus));
            return 2;
        }
        std::printf("arstro-android-shell: rendered surface '%s' (%dx%d) -> %s\n",
                    cfg.name.c_str(), w, h, o.renderPng.c_str());
        return 0;
    }

    int selfTest()
    {
        // artboard_core links + runs.
        artboard::RecordingTarget rec;
        rec.save();
        rec.restore();

        // The SurfaceHost draw path links + runs with no display (paint into a 16x16 image).
        SurfaceHost host(defaultSurfaces()[0]);
        host.setRoot(makeRoot(defaultSurfaces()[0]));
        cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 16, 16);
        cairo_t *cr = cairo_create(s);
        host.paint(cr, 16, 16);
        const bool paintOk = cairo_status(cr) == CAIRO_STATUS_SUCCESS;
        cairo_destroy(cr);
        cairo_surface_destroy(s);

        const bool haveLayerShell =
#ifdef HAVE_GTK_LAYER_SHELL
            true;
#else
            false;
#endif
        std::printf("arstro-android-shell: self-test %s\n", paintOk ? "OK" : "FAILED");
        std::printf("  GTK %d.%d.%d, gtk-layer-shell: %s, surfaces: %zu, draw-path: %s\n",
                    gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version(),
                    haveLayerShell ? "yes" : "no (plain-window mode)",
                    defaultSurfaces().size(), paintOk ? "ok" : "error");
        return paintOk ? 0 : 2;
    }
}

int main(int argc, char **argv)
{
    const Options o = parseArgs(argc, argv);

    // Headless modes need no display.
    if (!o.renderPng.empty()) return renderToPng(o);

    if (!gtk_init_check(&argc, &argv))
    {
        std::fprintf(stderr, "arstro-android-shell: no display available (GTK could not init).\n");
        return o.selfTest ? selfTest() : 1;
    }
    if (o.selfTest) return selfTest();

    const SurfaceConfig &cfg = pickSurface(o.surface);
    SurfaceHost host(cfg);
    host.create(o.windowed);
    host.setRoot(makeRoot(cfg));
    host.show();

    gtk_main();
    return 0;
}
