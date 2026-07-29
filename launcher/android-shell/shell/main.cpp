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
#include "StatusBar.h"
#include "NotificationPanel.h"
#include "FrameClock.h"
#include "ShellState.h"
#include "system/DbusSystemServices.h"
#include "notifyd/NotificationStore.h"
#include "notifyd/NotifyService.h"
#include "theme/AndroidColors.h"
#include "theme/Type.h"
#include "theme/Shape.h"
#include "theme/Motion.h"
#include "theme/Fonts.h"
#include "theme/IconDrawable.h"
#include "theme/IconMask.h"
#include "theme/SampleSheet.h"
#include "theme/icons/GeneratedIcons.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

using arstro::androidshell::FrameClock;
using arstro::androidshell::SurfaceConfig;
using arstro::androidshell::SurfaceHost;
using arstro::androidshell::defaultSurfaces;
using arstro::androidshell::ShellState;
using arstro::androidshell::ThemeMode;
using arstro::androidshell::SampleSheet;
using arstro::androidshell::NullBridge;
using arstro::androidshell::FakeSystemServices;
using arstro::androidshell::DbusSystemServices;
using arstro::androidshell::registerBundledFonts;

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
        bool probeServices = false;
        bool notifyd = false;
        bool windowed = false;
        int surface = 0;
        bool surfaceGiven = false;      // --surface=N passed -> single-surface mode; else all surfaces
        int width = 0, height = 0;      // 0 = use config default
        std::string renderPng;          // non-empty = headless PNG render mode
        std::string sampleSheet;        // "light"/"dark": render the token sample sheet instead of a surface
        std::string statusBar;          // a state name: render the status bar in that state
        std::string notifPanel;         // a state name: render the notification panel
    };

    Options parseArgs(int argc, char **argv)
    {
        Options o;
        for (int i = 1; i < argc; ++i)
        {
            const std::string a = argv[i];
            if (a == "--self-test") o.selfTest = true;
            else if (a == "--probe-services") o.probeServices = true;
            else if (a == "--notifyd") o.notifyd = true;
            else if (a == "--windowed") o.windowed = true;
            else if (a.rfind("--surface=", 0) == 0) { o.surface = std::atoi(a.c_str() + 10); o.surfaceGiven = true; }
            else if (a.rfind("--render-png=", 0) == 0) o.renderPng = a.substr(13);
            else if (a.rfind("--sample-sheet=", 0) == 0) o.sampleSheet = a.substr(15);
            else if (a.rfind("--status-bar=", 0) == 0) o.statusBar = a.substr(13);
            else if (a.rfind("--notif-panel=", 0) == 0) o.notifPanel = a.substr(14);
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

    // Render the android_theme token sample sheet (light/dark) to a PNG with no display — the M2.5
    // L1 golden. Returns 0 on success.
    int renderSampleSheet(const Options &o)
    {
        const int w = o.width > 0 ? o.width : 620;
        const int h = o.height > 0 ? o.height : 960;
        SampleSheet sheet;
        sheet.mode = (o.sampleSheet == "light") ? ThemeMode::Light : ThemeMode::Dark;
        sheet.width.set((double)w);
        sheet.height.set((double)h);

        cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cairo_t *cr = cairo_create(s);
        artboard::CairoTarget target;
        target.setContext(cr);
        sheet.render(target);
        const cairo_status_t drawStatus = cairo_status(cr);
        const cairo_status_t writeStatus = cairo_surface_write_to_png(s, o.renderPng.c_str());
        cairo_destroy(cr);
        cairo_surface_destroy(s);
        if (drawStatus != CAIRO_STATUS_SUCCESS || writeStatus != CAIRO_STATUS_SUCCESS)
        {
            std::fprintf(stderr, "arstro-android-shell: sample-sheet render failed\n");
            return 2;
        }
        std::printf("arstro-android-shell: rendered %s sample sheet (%dx%d) -> %s\n",
                    sheet.mode == ThemeMode::Dark ? "dark" : "light", w, h, o.renderPng.c_str());
        return 0;
    }

    // Live D-Bus smoke check (M3.1): construct the real backend and print battery/wifi/bt readings.
    // Kept OUT of --self-test so that stays daemon-free (L0). Returns 0 always (it is a probe).
    // Run notifyd standalone: own org.freedesktop.Notifications, print each notification as it
    // arrives, and spin a GLib main loop. For a live round-trip test inside dbus-run-session.
    int runNotifyd()
    {
        arstro::androidshell::NotificationStore store;
        store.observe([&] {
            const auto &it = store.items();
            if (!it.empty())
                std::printf("notifyd: [%u] %s — %s | %s (total %d)\n", it.back().id,
                            it.back().appName.c_str(), it.back().summary.c_str(),
                            it.back().body.c_str(), store.count());
            std::fflush(stdout);
        });
        arstro::androidshell::NotifyService svc(store);
        if (!svc.start()) { std::fprintf(stderr, "notifyd: no session bus\n"); return 1; }
        GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
        g_main_loop_run(loop);
        g_main_loop_unref(loop);
        return 0;
    }

    int probeServices()
    {
        arstro::androidshell::DbusSystemServices svc;
        std::printf("arstro-android-shell: system bus %s\n", svc.connected() ? "connected" : "NOT connected");
        const auto b = svc.battery();
        const auto w = svc.wifi();
        const auto bt = svc.bluetooth();
        std::printf("  battery: %d%% charging=%d present=%d\n", b.percent, b.charging, b.present);
        std::printf("  wifi:    enabled=%d ssid=\"%s\" bars=%d/4\n", w.enabled, w.ssid.c_str(), w.strength);
        std::printf("  bt:      powered=%d connected=%d\n", bt.powered, bt.connected);
        return 0;
    }

    // Fill a StatusBar's snapshot from live services + the wall clock (runtime refresh).
    void refreshStatusBar(arstro::androidshell::StatusBar &bar, arstro::androidshell::SystemServices &sv)
    {
        std::time_t now = std::time(nullptr);
        std::tm tmv{};
        localtime_r(&now, &tmv);
        char buf[16];
        std::strftime(buf, sizeof buf, "%H:%M", &tmv);  // TODO(M5): honour 12/24h locale pref
        bar.data.clock = buf;
        const auto b = sv.battery();
        bar.data.batteryPercent = b.percent;
        bar.data.charging = b.charging;
        const auto w = sv.wifi();
        bar.data.wifiBars = w.enabled ? w.strength : -1;
        bar.data.bluetooth = sv.bluetooth().connected;
        bar.data.dnd = sv.doNotDisturb();
        bar.data.airplane = sv.airplaneMode();
    }

    // Fill a store with a fixed set of notifications for a named golden state.
    void fillNotifStore(arstro::androidshell::NotificationStore &s, const std::string &state)
    {
        using N = arstro::androidshell::Notification;
        if (state == "empty") return;
        N a; a.appName = "Messages"; a.summary = "Alex"; a.body = "See you at 6?"; s.addOrReplace(a);
        N b; b.appName = "Calendar"; b.summary = "Standup"; b.body = "in 15 minutes"; s.addOrReplace(b);
        N d; d.appName = "System"; d.summary = "Update available"; d.body = "critical security patch";
        d.urgency = 2; s.addOrReplace(d);
        if (state == "expanded")
        {
            N p; p.appName = "Files"; p.summary = "Copying…"; p.body = "photos/"; p.hasProgress = true;
            p.progress = 62; s.addOrReplace(p);
        }
    }

    int renderNotifPanel(const Options &o)
    {
        static arstro::androidshell::NotificationStore store;
        fillNotifStore(store, o.notifPanel);
        arstro::androidshell::NotificationPanel panel;
        panel.store = &store;
        panel.mode = ThemeMode::Dark;
        const int w = o.width > 0 ? o.width : 480;
        const int h = o.height > 0 ? o.height : 700;
        panel.width.set((double)w); panel.height.set((double)h);
        cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cairo_t *cr = cairo_create(s);
        cairo_set_source_rgb(cr, 0.14, 0.11, 0.18); cairo_paint(cr);  // wallpaper behind the scrim
        artboard::CairoTarget tgt; tgt.setContext(cr);
        panel.render(tgt);
        cairo_surface_write_to_png(s, o.renderPng.c_str());
        cairo_destroy(cr); cairo_surface_destroy(s);
        std::printf("arstro-android-shell: rendered notif panel '%s' -> %s\n", o.notifPanel.c_str(),
                    o.renderPng.c_str());
        return 0;
    }

    // Build a StatusBar snapshot for a named golden state (deterministic, no live services).
    arstro::androidshell::StatusBar makeStatusBar(const std::string &state)
    {
        arstro::androidshell::StatusBar bar;
        bar.width.set(720.0);
        bar.height.set(24.0);
        auto &d = bar.data;
        d.clock = "12:30";
        d.wifiBars = 3;
        d.batteryPercent = 72;
        if (state == "light") { d.darkIcons = true; bar.mode = ThemeMode::Light; }
        else if (state == "dark") { d.darkIcons = false; bar.mode = ThemeMode::Dark; }
        else if (state == "charging") { d.charging = true; d.batteryPercent = 45; }
        else if (state == "nowifi") { d.wifiBars = -1; }
        else if (state == "dnd") { d.dnd = true; d.bluetooth = true; d.airplane = false; }
        return bar;
    }

    int renderStatusBar(const Options &o)
    {
        arstro::androidshell::StatusBar bar = makeStatusBar(o.statusBar);
        const int w = o.width > 0 ? o.width : (int)bar.width.value();
        const int h = o.height > 0 ? o.height : (int)bar.height.value();
        bar.width.set((double)w);
        bar.height.set((double)h);
        cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cairo_t *cr = cairo_create(s);
        // paint a backdrop so white-icon (wallpaper) mode is visible: dark for dark, light for light
        if (bar.data.darkIcons) { cairo_set_source_rgb(cr, 0.97, 0.94, 0.98); }
        else { cairo_set_source_rgb(cr, 0.22, 0.16, 0.28); }
        cairo_paint(cr);
        artboard::CairoTarget target;
        target.setContext(cr);
        bar.render(target);
        cairo_surface_write_to_png(s, o.renderPng.c_str());
        cairo_destroy(cr);
        cairo_surface_destroy(s);
        std::printf("arstro-android-shell: rendered status bar '%s' -> %s\n", o.statusBar.c_str(),
                    o.renderPng.c_str());
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

        // ---- M1.6: all surfaces share ONE clock + ONE ShellState and each renders (L0+L1) ----
        bool multiOk = true;
        {
            NullBridge bridge;
            FakeSystemServices services;
            ShellState state(bridge, services);
            FrameClock clock;
            std::vector<std::unique_ptr<SurfaceHost>> hosts;
            for (const auto &cfg : defaultSurfaces())
            {
                auto h = std::make_unique<SurfaceHost>(cfg);  // heap-stable (sink captures `this`)
                h->setRoot(makeRoot(cfg));
                h->setShellState(&state);
                SurfaceHost *hp = h.get();
                clock.add([hp](double now) { hp->frameTick(now); });
                hosts.push_back(std::move(h));
            }
            clock.tick(0.0);  // one shared tick advances every surface

            bool allPaint = true;  // every surface renders (paint path) without a display
            for (auto &h : hosts)
            {
                cairo_surface_t *cs = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 32, 32);
                cairo_t *ccr = cairo_create(cs);
                h->paint(ccr, 32, 32);
                allPaint = allPaint && cairo_status(ccr) == CAIRO_STATUS_SUCCESS && h->shellState() == &state;
                cairo_destroy(ccr);
                cairo_surface_destroy(cs);
            }
            const int n = (int)defaultSurfaces().size();
            multiOk = n == 7 && clock.count() == n && (int)hosts.size() == n && allPaint;
        }

        // ---- M2.1: Android M3 colour role table (L0) ----
        bool themeOk = true;
        {
            using arstro::androidshell::colors;
            const auto &light = colors(ThemeMode::Light);
            const auto &dark = colors(ThemeMode::Dark);
            // exact role values (plan §2.2) + light != dark for a key surface role
            themeOk = light.surface == artboard::Color::hex(0xFEF7FF) &&
                      dark.surface == artboard::Color::hex(0x141218) &&
                      light.primary == artboard::Color::hex(0x6750A4) &&
                      dark.primary == artboard::Color::hex(0xD0BCFF) &&
                      dark.onSurface == artboard::Color::hex(0xE6E0E9) &&
                      !(light.surface == dark.surface) &&
                      !(light.primary == dark.primary);
        }

        // ---- M2.2: type ramp + radius scale + motion aliases (L0) ----
        bool typeShapeMotionOk = true;
        {
            namespace ty = arstro::androidshell::type;
            namespace sh = arstro::androidshell::shape;
            namespace mo = arstro::androidshell::motion;
            typeShapeMotionOk =
                // type ramp (plan §2.2): sizes + weight-as-family
                ty::displayLarge.sizePx == 45.0 && ty::displayLarge.fontFamily == "Roboto" &&
                ty::titleMedium.sizePx == 16.0 && ty::titleMedium.fontFamily == "Roboto Medium" &&
                ty::bodyMedium.sizePx == 14.0 && ty::labelSmall.sizePx == 11.0 &&
                ty::styled(ty::labelSmall, artboard::Color::hex(0x123456)).color == artboard::Color::hex(0x123456) &&
                // radius scale
                sh::kRadiusXL == 28.0 && sh::kRadiusSM == 8.0 && sh::radiusFull(48.0) == 24.0 &&
                // motion aliases map to the AB-6 tokens
                mo::kPanelOpenMs == 350.0 && mo::kPressMs == 100.0 &&
                mo::kEmphasizedDecel == artboard::Easing::EmphasizedDecel &&
                mo::kSpatialDefault == artboard::motion::kSpatialDefault;
        }

        // ---- M2.3: icon codegen + IconDrawable replay (L0) ----
        bool iconsOk = true;
        {
            namespace ic = arstro::androidshell::icons;
            // generated tables: a line icon and the arc-flattened solid icon
            const bool tables =
                ic::kCheck.count == 3 && ic::kCheck.stroke &&
                ic::kCheck.ops[0].kind == ic::IconOp::Move && ic::kCheck.ops[1].kind == ic::IconOp::Line &&
                ic::kDot.count == 6 && !ic::kDot.stroke &&      // 2 SVG arcs -> 4 cubics + Move + Close
                ic::kDot.ops[1].kind == ic::IconOp::Cubic;

            // IconDrawable replays kCheck (rect 0,0,24,24 -> scale 1, coords unchanged) into a
            // RecordingTarget: beginPath, moveTo, 2x lineTo, setStroke, strokePath — nothing else.
            arstro::androidshell::IconDrawable icon(ic::kCheck);
            icon.rect = artboard::Rect{0, 0, 24, 24};
            artboard::RecordingTarget irec;
            icon.render(irec);
            const auto &ops = irec.ops();
            using K = artboard::DrawOp::Kind;
            const bool replay =
                irec.count(K::BeginPath) == 1 && irec.count(K::MoveTo) == 1 &&
                irec.count(K::LineTo) == 2 && irec.count(K::StrokePath) == 1 &&
                irec.count(K::FillPath) == 0 && irec.count(K::CubicTo) == 0;
            // first Move op at the icon's (5,13)
            bool coordOk = false;
            for (const auto &op : ops)
                if (op.kind == K::MoveTo)
                {
                    coordOk = std::fabs(op.args[0] - 5.0) < 1e-6 && std::fabs(op.args[1] - 13.0) < 1e-6;
                    break;
                }
            iconsOk = tables && replay && coordOk;
        }

        // ---- M2.4: adaptive-icon masker (clipPath + drawImage) + themed icon (L0) ----
        bool maskOk = true;
        {
            using arstro::androidshell::MaskShape;
            using arstro::androidshell::drawMaskedImage;
            using arstro::androidshell::emitMaskPath;
            using arstro::androidshell::drawThemedIcon;
            using K = artboard::DrawOp::Kind;
            const artboard::Rect r{0, 0, 48, 48};

            // circle mask outline: beginPath + 4 cubics + close, no fill/stroke on its own
            {
                artboard::RecordingTarget rec;
                emitMaskPath(rec, r, MaskShape::Circle);
                maskOk = maskOk && rec.count(K::BeginPath) == 1 && rec.count(K::CubicTo) == 4 &&
                         rec.count(K::ClosePath) == 1;
            }
            // squircle mask == the generated kSquircle table (scaled), so it emits its op count
            {
                artboard::RecordingTarget rec;
                emitMaskPath(rec, r, MaskShape::Squircle);
                maskOk = maskOk && rec.count(K::BeginPath) == 1 &&
                         (rec.count(K::MoveTo) + rec.count(K::LineTo) + rec.count(K::CubicTo) +
                          rec.count(K::ClosePath)) == arstro::androidshell::icons::kSquircle.count;
            }
            // drawMaskedImage: Save, (mask path), ClipPath, DrawImage, Restore — clip BEFORE draw.
            {
                artboard::RecordingTarget rec;
                const uint8_t px[4] = {200, 100, 50, 255};  // 1x1 image
                const int id = rec.registerImage(px, 1, 1);
                drawMaskedImage(rec, id, r, MaskShape::Circle);
                const auto &ops = rec.ops();
                int clipIdx = -1, drawIdx = -1, saveIdx = -1, restoreIdx = -1;
                for (int i = 0; i < (int)ops.size(); ++i)
                {
                    if (ops[i].kind == K::ClipPath) clipIdx = i;
                    if (ops[i].kind == K::DrawImage) drawIdx = i;
                    if (ops[i].kind == K::Save && saveIdx < 0) saveIdx = i;
                    if (ops[i].kind == K::Restore) restoreIdx = i;
                }
                maskOk = maskOk && saveIdx >= 0 && clipIdx > saveIdx && drawIdx > clipIdx &&
                         restoreIdx > drawIdx && rec.count(K::ClipPath) == 1 &&
                         rec.count(K::DrawImage) == 1;
            }
            // themed icon: a filled background (FillPath) then the glyph painted on top.
            {
                artboard::RecordingTarget rec;
                drawThemedIcon(rec, arstro::androidshell::icons::kCheck, r, MaskShape::Circle,
                               artboard::Color::hex(0x4F378B), artboard::Color::hex(0xEADDFF), 10.0);
                // bg fill (circle) + glyph stroke (kCheck is a line icon)
                maskOk = maskOk && rec.count(K::FillPath) == 1 && rec.count(K::StrokePath) == 1;
            }
        }

        // ---- M3.2: status bar draws clock + battery + wifi + conditional icons (L0) ----
        bool statusBarOk = true;
        {
            using K = artboard::DrawOp::Kind;
            using K2 = artboard::Gesture::Type;
            // charging + bt + dnd + airplane state -> expect clock text, battery %, and several icons.
            arstro::androidshell::StatusBar bar;
            bar.width.set(720.0);
            bar.height.set(24.0);
            bar.data.charging = true;
            bar.data.bluetooth = true;
            bar.data.dnd = true;
            bar.data.airplane = true;
            bar.data.wifiBars = 3;
            artboard::RecordingTarget rec;
            bar.render(rec);
            // two text runs at least (clock + battery %), the wifi arcs (strokes), icon fills/strokes
            const bool hasText = rec.count(K::DrawText) >= 2;
            bool clockDrawn = false;
            for (const auto &op : rec.ops())
                if (op.kind == K::DrawText && op.text == "12:30") clockDrawn = true;
            const bool hasStrokes = rec.count(K::StrokePath) >= 3;  // wifi arcs + battery outline + bt
            // no-wifi vs full-wifi differ (dimmed colours change the SetStroke count/colours)
            arstro::androidshell::StatusBar nowifi;
            nowifi.width.set(720.0); nowifi.height.set(24.0); nowifi.data.wifiBars = -1;
            artboard::RecordingTarget rec2;
            nowifi.render(rec2);
            statusBarOk = hasText && clockDrawn && hasStrokes && rec2.count(K::DrawText) >= 2;

            // M3.3: a top-edge drag drives the dual-shade Observables (left=notif, right=QS).
            NullBridge nb; FakeSystemServices fs; ShellState st(nb, fs);
            arstro::androidshell::StatusBar bar2;
            bar2.width.set(720.0); bar2.height.set(24.0); bar2.setState(&st);
            bar2.onGesture({K2::DragStart, {100, 2}, {100, 2}, artboard::PointerButton::Left}); // left half
            bar2.onGesture({K2::Drag, {100, 302}, {100, 2}, artboard::PointerButton::Left});      // pull 300
            const bool leftShade = std::fabs(st.notificationsExpansion.get() - 0.5) < 1e-6 &&
                                   st.quickSettingsExpansion.get() == 0.0;
            arstro::androidshell::StatusBar bar3;
            bar3.width.set(720.0); bar3.height.set(24.0); bar3.setState(&st);
            bar3.onGesture({K2::DragStart, {600, 2}, {600, 2}, artboard::PointerButton::Left}); // right half
            bar3.onGesture({K2::Drag, {600, 602}, {600, 2}, artboard::PointerButton::Left});      // pull 600 -> 1.0
            const bool rightShade = std::fabs(st.quickSettingsExpansion.get() - 1.0) < 1e-6;
            statusBarOk = statusBarOk && leftShade && rightShade;
        }

        // ---- M4.1: NotificationStore add/replace/close/clear + observe (L0) ----
        bool notifOk = true;
        {
            arstro::androidshell::NotificationStore store;
            int fires = 0;
            store.observe([&] { ++fires; });
            arstro::androidshell::Notification n;
            n.appName = "Mail"; n.summary = "New message"; n.urgency = 1;
            const uint32_t id = store.addOrReplace(n);
            const bool added = store.count() == 1 && id == 1 && fires == 1;
            arstro::androidshell::Notification n2; n2.id = id; n2.summary = "edited";
            store.addOrReplace(n2);  // replace by id
            const bool replaced = store.count() == 1 && fires == 2 &&
                                  store.items()[0].summary == "edited";
            arstro::androidshell::Notification crit; crit.urgency = 2; crit.resident = true;
            store.addOrReplace(crit);  // id 2
            const bool dismissible = store.count() == 2 && store.dismissibleCount() == 1;
            store.clearAll();  // removes non-resident only
            const bool cleared = store.count() == 1 && store.items()[0].resident;
            const bool closed = store.close(id) == false;  // already cleared
            notifOk = added && replaced && dismissible && cleared && closed;
        }

        const bool ok = paintOk && clockOk && inputOk && stateOk && multiOk && themeOk &&
                        typeShapeMotionOk && iconsOk && maskOk && statusBarOk && notifOk;
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
        std::printf("  draw-path: %s, frame-clock: %s, input: %s, shell-state: %s, all-surfaces: %s\n",
                    paintOk ? "ok" : "error", clockOk ? "ok" : "error",
                    inputOk ? "ok" : "error", stateOk ? "ok" : "error", multiOk ? "ok" : "error");
        std::printf("  colors: %s, type/shape/motion: %s, icons: %s, icon-mask: %s, status-bar: %s, notif-store: %s\n",
                    themeOk ? "ok" : "error", typeShapeMotionOk ? "ok" : "error",
                    iconsOk ? "ok" : "error", maskOk ? "ok" : "error", statusBarOk ? "ok" : "error",
                    notifOk ? "ok" : "error");
        return ok ? 0 : 2;
    }
}

int main(int argc, char **argv)
{
    const Options o = parseArgs(argc, argv);

    // Register the app-private bundled fonts so the type-ramp family names resolve. No-op-safe
    // if the TTFs are absent (logs, falls back to generic sans). Done before any rendering.
    registerBundledFonts();

    // Headless modes need no display.
    if (o.notifyd) return runNotifyd();
    if (o.probeServices) return probeServices();
    if (!o.renderPng.empty() && !o.sampleSheet.empty()) return renderSampleSheet(o);
    if (!o.renderPng.empty() && !o.statusBar.empty()) return renderStatusBar(o);
    if (!o.renderPng.empty() && !o.notifPanel.empty()) return renderNotifPanel(o);
    if (!o.renderPng.empty()) return renderToPng(o);

    if (!gtk_init_check(&argc, &argv))
    {
        std::fprintf(stderr, "arstro-android-shell: no display available (GTK could not init).\n");
        return o.selfTest ? selfTest() : 1;
    }
    if (o.selfTest) return selfTest();

    // One shared ~60Hz clock drives every surface's advance() + dirty-gated redraw (M1.3).
    FrameClock clock;

    // --surface=N: single-surface mode (goldens / focused debugging). Default: the real shell —
    // ALL surfaces (M1.6), each a layer-shell surface where available, else a plain window.
    if (o.surfaceGiven)
    {
        const SurfaceConfig &cfg = pickSurface(o.surface);
        static SurfaceHost host(cfg);  // static: fixed address for the sink's captured `this`
        host.create(o.windowed);
        host.setRoot(makeRoot(cfg));
        host.show();
        clock.add([](double nowMs) { host.frameTick(nowMs); });
        clock.start();
        gtk_main();
        clock.stop();
        return 0;
    }

    // All surfaces. ShellState + the seams live for the whole run; SurfaceHosts are heap-stable
    // (unique_ptr) because each one's recognizer sink captures `this`. The live shell uses the
    // real D-Bus backend (tests use the fake).
    static NullBridge bridge;
    static DbusSystemServices services;
    static ShellState state(bridge, services);
    static std::vector<std::unique_ptr<SurfaceHost>> hosts;
    static auto statusBar = std::make_shared<arstro::androidshell::StatusBar>();
    statusBar->mode = state.themeMode.get() == ThemeMode::Light ? ThemeMode::Light : ThemeMode::Dark;

    // Notifications: our daemon + store, and the panel bound to both (M4).
    static arstro::androidshell::NotificationStore notifStore;
    static arstro::androidshell::NotifyService notifSvc(notifStore);
    notifSvc.start();
    static auto notifPanel = std::make_shared<arstro::androidshell::NotificationPanel>();
    notifPanel->store = &notifStore;
    notifPanel->state = &state;

    const auto &cfgs = defaultSurfaces();
    for (size_t i = 0; i < cfgs.size(); ++i)
    {
        auto h = std::make_unique<SurfaceHost>(cfgs[i]);
        h->create(o.windowed);
        // bind the real surfaces; the rest keep placeholders for now.
        if (cfgs[i].name == "statusbar") { statusBar->setState(&state); h->setRoot(statusBar); }
        else if (cfgs[i].name == "shade-notifications")
        {
            h->setRoot(notifPanel);
            // redraw the shade while it is open (drag animation + arriving notifications), idle closed.
            h->setAnimatingQuery([](double) { return state.notificationsExpansion.get() > 0.001; });
        }
        else h->setRoot(makeRoot(cfgs[i]));
        h->setShellState(&state);
        h->show();
        SurfaceHost *hp = h.get();
        const bool isBar = (cfgs[i].name == "statusbar");
        clock.add([hp, isBar](double nowMs) {
            // Refresh the status bar from services + clock ~once a second (not per frame).
            static double lastRefresh = -1e9;
            if (isBar && nowMs - lastRefresh > 1000.0)
            {
                lastRefresh = nowMs;
                refreshStatusBar(*statusBar, services);
                hp->markDirty();
            }
            hp->frameTick(nowMs);
        });
        hosts.push_back(std::move(h));
    }
    refreshStatusBar(*statusBar, services);  // initial fill so the first frame is live
    clock.start();
    gtk_main();
    clock.stop();
    return 0;
}
