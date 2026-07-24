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
#include "FrameClock.h"
#include "ShellState.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

using arstro::androidshell::FrameClock;
using arstro::androidshell::SurfaceConfig;
using arstro::androidshell::SurfaceHost;
using arstro::androidshell::defaultSurfaces;
using arstro::androidshell::ShellState;
using arstro::androidshell::ThemeMode;
using arstro::androidshell::NullBridge;
using arstro::androidshell::FakeSystemServices;

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

    // Paint a host into a throwaway 4x4 image (headless), so a test can model "this frame
    // reached the screen" — it clears the dirty flag and bumps the paint counter.
    void flush(SurfaceHost &h)
    {
        cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 4, 4);
        cairo_t *cr = cairo_create(s);
        h.paint(cr, 4, 4);
        cairo_destroy(cr);
        cairo_surface_destroy(s);
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

        // ---- M1.3: frame-clock + dirty-gating logic (headless L0 check) ----
        bool clockOk = true;

        // (A) A static surface goes idle after its first paint: dirty for frame 1, then a paint
        //     clears it, and subsequent ticks report NOT dirty (zero further redraws).
        {
            SurfaceHost h(defaultSurfaces()[0]);
            h.setRoot(makeRoot(defaultSurfaces()[0]));
            const bool d1 = h.frameTick(0.0);   // initial dirty
            flush(h);                            // painted -> dirty cleared, count = 1
            const bool d2 = h.frameTick(16.0);  // static -> not dirty
            clockOk = clockOk && d1 && !d2 && h.paintCount() == 1;
        }
        // (B) An animating surface stays dirty (keeps redrawing) until its predicate settles.
        {
            SurfaceHost h(defaultSurfaces()[0]);
            h.setRoot(makeRoot(defaultSurfaces()[0]));
            h.setAnimatingQuery([](double now) { return now < 50.0; });
            flush(h);                            // count = 1
            const bool a1 = h.frameTick(16.0);   // animating -> dirty
            flush(h);                            // count = 2
            const bool a2 = h.frameTick(48.0);   // animating -> dirty
            flush(h);                            // count = 3
            const bool a3 = h.frameTick(60.0);   // settled -> not dirty (no more redraws)
            clockOk = clockOk && a1 && a2 && !a3 && h.paintCount() == 3;
        }
        // (C) markDirty() forces a one-shot redraw request even for static content.
        {
            SurfaceHost h(defaultSurfaces()[0]);
            h.setRoot(makeRoot(defaultSurfaces()[0]));
            flush(h);
            const bool idle = h.frameTick(16.0);  // static -> not dirty
            h.markDirty();
            const bool redraw = h.frameTick(32.0);  // dirty again
            clockOk = clockOk && !idle && redraw;
        }
        // (D) ONE shared clock fans a single tick out to every registered surface.
        {
            FrameClock clock;
            int a = 0, b = 0;
            clock.add([&](double) { ++a; });
            clock.add([&](double) { ++b; });
            clock.tick(0.0);
            clockOk = clockOk && clock.count() == 2 && a == 1 && b == 1;
        }

        // ---- M1.4: input plumbing (GDK->RawPointer path is exercised via feedPointer) ----
        bool inputOk = true;
        {
            // A root that records the gestures the recognizer routes to it.
            struct GestureCounter : artboard::Segment
            {
                int downs = 0, clicks = 0, moves = 0;
                bool lastTouch = false;
                bool handleGesture(const artboard::Gesture &g, const artboard::Point &) override
                {
                    using T = artboard::Gesture::Type;
                    if (g.type == T::Down) ++downs;
                    else if (g.type == T::Click) ++clicks;
                    else if (g.type == T::Move) ++moves;
                    lastTouch = g.touch;
                    return true;
                }
            };
            auto root = std::make_shared<GestureCounter>();
            root->width.set(200);
            root->height.set(200);
            SurfaceHost h(defaultSurfaces()[0]);
            h.setRoot(root);
            flush(h);  // clear the initial dirty so we can observe input re-dirtying it

            // A touch tap: Down then Up at the same point -> the recognizer emits Down + Click,
            // both carrying touch=true, routed to the root; the surface goes dirty.
            artboard::RawPointer down;
            down.kind = artboard::RawPointer::Kind::Down;
            down.pos = {10, 10};
            down.timeMs = 100.0;
            down.touch = true;
            artboard::RawPointer up = down;
            up.kind = artboard::RawPointer::Kind::Up;
            up.timeMs = 120.0;
            h.feedPointer(down);
            h.feedPointer(up);

            inputOk = root->downs == 1 && root->clicks == 1 && root->lastTouch && h.dirty();
        }

        // ---- M1.5: ShellState (Observable) + NullBridge + FakeSystemServices (L0) ----
        bool stateOk = true;
        {
            NullBridge bridge;
            FakeSystemServices services;
            ShellState state(bridge, services);

            // (F) An Observable notifies bound observers on change, and fireNow initialises in sync.
            ThemeMode seen = ThemeMode::Light;
            int fires = 0;
            state.themeMode.observe([&](const ThemeMode &m) { seen = m; ++fires; });  // fireNow -> 1
            const bool init = (fires == 1 && seen == ThemeMode::Dark);
            state.themeMode.set(ThemeMode::Light);                                     // change -> 2
            const bool changed = (fires == 2 && seen == ThemeMode::Light);
            state.themeMode.set(ThemeMode::Light);                                     // no-op, no fire
            const bool noRefire = (fires == 2);

            // (G) FakeSystemServices returns canned values and round-trips a setter.
            const bool canned = services.battery().percent == 72 && services.wifi().strength == 3;
            services.setWifiEnabled(false);
            const bool roundTrip = !services.wifi().enabled && services.wifi().ssid.empty();
            services.setAirplaneMode(true);  // also turns wifi + bt off
            const bool airplane = services.airplaneMode() && !services.bluetooth().powered;
            services.suspend();
            const bool power = services.suspendCalls == 1;

            // (H) NullBridge is callable and reports no windows; ShellState mirrors it.
            bridge.onWindowsChanged();  // triggers windows.set(listWindows()) == empty (no change)
            const bool nullBridge = bridge.listWindows().empty() && state.windows.get().empty();

            stateOk = init && changed && noRefire && canned && roundTrip && airplane && power && nullBridge;
        }

        const bool ok = paintOk && clockOk && inputOk && stateOk;
        const bool haveLayerShell =
#ifdef HAVE_GTK_LAYER_SHELL
            true;
#else
            false;
#endif
        std::printf("arstro-android-shell: self-test %s\n", ok ? "OK" : "FAILED");
        std::printf("  GTK %d.%d.%d, gtk-layer-shell: %s, surfaces: %zu\n",
                    gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version(),
                    haveLayerShell ? "yes" : "no (plain-window mode)", defaultSurfaces().size());
        std::printf("  draw-path: %s, frame-clock: %s, input: %s, shell-state: %s\n",
                    paintOk ? "ok" : "error", clockOk ? "ok" : "error",
                    inputOk ? "ok" : "error", stateOk ? "ok" : "error");
        return ok ? 0 : 2;
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

    // One shared ~60Hz clock drives every surface's advance() + dirty-gated redraw (M1.3).
    // With one static placeholder surface it paints once and then goes idle (zero redraws).
    FrameClock clock;
    clock.add([&host](double nowMs) { host.frameTick(nowMs); });
    clock.start();

    gtk_main();

    clock.stop();
    return 0;
}
