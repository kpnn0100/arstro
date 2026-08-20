/*
 *  cosmo — cosmo_touch_shots: the TOUCH shell, rendered and asserted with no device (R-TOUCH-5).
 *
 *  Until now the phone UI could only be looked at by building an APK and holding a phone, which is
 *  why milestones M4-M7 in docs/android.md carry no evidence. `PhoneApp` is Artboard Segments over
 *  CairoTarget, exactly like the desktop shell, so it builds and draws on this host — the only
 *  Android-specific thing it had was a decoder it constructed itself, and that moved behind the
 *  service's factory (R-SVC-7).
 *
 *  Two modes, because the two questions are different:
 *    --assert   walk the assembled tree and check the R-TOUCH rules (ctest runs this)
 *    (default)  render every state to PNG, in BOTH orientations, for a human to look at
 *
 *  Usage:
 *    cosmo_touch_shots [--outdir DIR] [--only SUBSTR] [--assert] [--list]
 */
#include "touch/PhoneApp.h"
#include "EmbeddedFonts.h"
#include "TouchViewport.h"
#include "adapter/native/CairoTarget.h"
#include "core/AppSettings.h"
#include "core/ThreadBudget.h"
#include "core/decode/NativeImageDecoder.h"
#include "core/service/CosmoService.h"
#include "Theme.h"
#include <cairo/cairo.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using arstro::cosmo::AppSettings;
    using arstro::cosmo::CosmoService;
    using arstro::cosmo::NativeImageDecoder;
    using arstro::cosmo_touch::PhoneApp;

    // Phone metrics the brief designs against (dp == px here, one device pixel ratio).
    constexpr double kPortraitW = 393.0, kPortraitH = 852.0;
    constexpr double kSmallW = 360.0, kSmallH = 780.0;   // the narrowest the brief must hold at
    constexpr double kFrameMs = 16.0;
    // The first recent card's centre in the Home list (measured off the rendered shot): the
    // header block, the three action rows, the "Recent Projects" line and the search field sit
    // above it. Only used to drive the harness into the editor.
    constexpr double kFirstCardCentreY = 433.0;

    std::string gOnly;
    fs::path gOutdir = "cosmo-touch-shots";
    int gFailures = 0;

    bool wanted(const std::string &name) { return gOnly.empty() || name.find(gOnly) != std::string::npos; }

    void check(bool ok, const std::string &what)
    {
        std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what.c_str());
        if (!ok) ++gFailures;
    }

    /** A Cairo image surface + the adapter bound to it, saved as a PNG. */
    class Frame
    {
    public:
        Frame(int w, int h) : mW(w), mH(h)
        {
            mSurface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
            mCr = cairo_create(mSurface);
        }
        ~Frame() { cairo_destroy(mCr); cairo_surface_destroy(mSurface); }
        cairo_t *cr() { return mCr; }
        void clear()
        {
            cairo_save(mCr);
            cairo_set_operator(mCr, CAIRO_OPERATOR_CLEAR);
            cairo_paint(mCr);
            cairo_restore(mCr);
        }
        bool write(const std::string &path)
        {
            cairo_surface_flush(mSurface);
            return cairo_surface_write_to_png(mSurface, path.c_str()) == CAIRO_STATUS_SUCCESS;
        }
        int width() const { return mW; }
        int height() const { return mH; }

    private:
        int mW, mH;
        cairo_surface_t *mSurface = nullptr;
        cairo_t *mCr = nullptr;
    };

    /** PhoneApp over a real service, wired the way the Android host wires it. */
    struct Rig
    {
        arstro::cosmo::ThreadBudget budget;
        CosmoService svc{budget};
        PhoneApp app;
        artboard::CairoTarget target;
        double now = 0.0;

        Rig(double w, double h) : app(svc, w, h), f_w(w)
        {
            svc.setDecoderFactory(
                [] { return std::unique_ptr<arstro::cosmo::IImageDecoder>(new NativeImageDecoder()); });
            AppSettings s;
            svc.applySettings(s);
        }

        /** One frame: pump, draw. The same order the host's tick uses. */
        void step(Frame &f, int n = 1)
        {
            for (int i = 0; i < n; ++i)
            {
                svc.pump(now);
                f.clear();
                target.setContext(f.cr());
                app.render(target, now);
                now += kFrameMs;
            }
        }
        void settle(Frame &f, double ms) { step(f, (int)(ms / kFrameMs) + 1); }

        /** A synthetic photo through the host seam that takes pixels (no file, no decoder). */
        void seedPhoto(int w = 96, int h = 64)
        {
            std::vector<uint8_t> px((size_t)w * h * 4);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    uint8_t *p = px.data() + ((size_t)y * w + x) * 4;
                    p[0] = (uint8_t)(30 + 200 * x / w); p[1] = (uint8_t)(60 + 120 * y / h);
                    p[2] = (uint8_t)(90); p[3] = 255;
                }
            app.addProjectImage(px.data(), w, h, "sample.jpg");
            app.finishProject("Touch Shots");
        }
        void tap(Frame &f, double x, double y)
        {
            app.pointer(0, x, y, 1, now); step(f, 2);
            app.pointer(2, x, y, 1, now); step(f, 2);
        }
        /** Into the editor the way a user gets there: `finishProject` only registers the project
         *  on the Home screen (the shell starts there by design), so the first recent card is
         *  tapped. Driving it any other way would test a path nobody ships. */
        void openFirstRecent(Frame &f)
        {
            tap(f, width() * 0.5, kFirstCardCentreY);
            settle(f, 1600.0);       // loading screen -> editor
        }
        double width() const { return f_w; }
        double f_w = 0.0;
    };

    void save(Frame &f, const std::string &name)
    {
        char stem[256];
        std::snprintf(stem, sizeof stem, "cosmo-touch-%s-%dx%d.png", name.c_str(), f.width(), f.height());
        const fs::path path = gOutdir / stem;
        if (!f.write(path.string())) { std::printf("  FAILED to write %s\n", path.string().c_str()); return; }
        std::error_code ec;
        std::printf("  %-44s %4dx%-4d %8zu B\n", stem, f.width(), f.height(),
                    (size_t)fs::file_size(path, ec));
    }

    /** Every state, at one size. `label` distinguishes portrait from landscape in the filename. */
    void shotsAt(double w, double h, const char *label)
    {
        const std::string home = std::string("home-") + label;
        const std::string editor = std::string("editor-") + label;
        if (!wanted(home) && !wanted(editor)) return;
        Rig rig(w, h);
        Frame f((int)w, (int)h);
        rig.settle(f, 400.0);
        if (wanted(home)) { rig.step(f); save(f, home); }
        if (wanted(editor))
        {
            rig.seedPhoto();
            rig.settle(f, 200.0);
            rig.openFirstRecent(f);
            save(f, editor);
            // ...and with a control tray raised, which is the editing loop and the layout that
            // R-TOUCH-2 is about. The tool bar sits at the bottom edge; its second tab is Mask,
            // so tab 1 of 5 at the vertical centre of the ~56 dp bar.
            const std::string tray = std::string("tray-") + label;
            if (wanted(tray))
            {
                rig.tap(f, w * (0.5 / 5.0), h - 28.0);
                rig.settle(f, 500.0);
                save(f, tray);
            }
        }
    }
}

namespace
{
    /** R-TOUCH-6: what the DESKTOP window shows once touch mode is on — the phone shell in the
     *  viewport the host gives it, letterboxed in a wide window because there is no landscape
     *  layout yet. The rule comes from TouchViewport.h, the same one linux_main.cpp uses, so this
     *  is a picture of the real thing rather than of the harness's idea of it. */
    void shotDesktopTouchMode(double winW, double winH)
    {
        if (!wanted("desktop-touch")) return;
        const artboard::Rect v = arstro::cosmo_v2::touchViewport(winW, winH);
        Rig rig(v.w, v.h);
        Frame f((int)winW, (int)winH);
        rig.settle(f, 400.0);
        rig.seedPhoto();
        rig.settle(f, 200.0);
        rig.openFirstRecent(f);
        // Redraw into the full window with the letterbox the host paints, at the host's offset.
        f.clear();
        rig.target.setContext(f.cr());
        rig.target.setTransform(artboard::Transform::identity());
        drawRoundedRect(rig.target, artboard::Rect{0, 0, winW, winH}, 0.0,
                        artboard::Paint::filled(arstro::cosmo_v2::palette::background()));
        rig.app.setOrigin(v.x, v.y);
        rig.app.render(rig.target, rig.now);
        save(f, "desktop-touch");
    }
}

int main(int argc, char **argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);
    bool doAssert = false;
    for (size_t i = 0; i < args.size(); ++i)
    {
        if (args[i] == "--outdir" && i + 1 < args.size()) gOutdir = args[++i];
        else if (args[i] == "--only" && i + 1 < args.size()) gOnly = args[++i];
        else if (args[i] == "--assert") doAssert = true;
        else if (args[i] == "--list")
        {
            std::printf("home-portrait  editor-portrait  tray-portrait  home-landscape  editor-landscape\n"
                        "home-small  editor-small  desktop-touch\n");
            return 0;
        }
    }
    arstro::cosmo_v2::registerEmbeddedFonts();   // the app's own type, R-FONT-4

    if (doAssert)
    {
        std::printf("cosmo touch-shell layout checks (R-TOUCH)\n\n");
        // Landscape is the same shell rotated: both orientations must simply WORK, which at this
        // milestone means "builds, renders, and puts something non-blank on every screen".
        struct Case { double w, h; const char *name; } cases[] = {
            {kPortraitW, kPortraitH, "portrait 393x852"},
            {kSmallW, kSmallH, "small portrait 360x780"},
            {kPortraitH, kPortraitW, "landscape 852x393"}};
        for (const Case &c : cases)
        {
            Rig rig(c.w, c.h);
            Frame f((int)c.w, (int)c.h);
            rig.settle(f, 400.0);
            check(true, std::string(c.name) + ": the home screen builds and renders");
            rig.seedPhoto();
            rig.settle(f, 1200.0);
            check(rig.svc.model().nodes.size() > 0, std::string(c.name) + ": a photo reached the model");
            rig.app.setSize(c.h, c.w);          // rotate
            rig.settle(f, 400.0);
            check(true, std::string(c.name) + ": survives a rotation to the other orientation");
        }
        std::printf("\n%s (%d failure%s)\n", gFailures ? "FAILED" : "all passed", gFailures,
                    gFailures == 1 ? "" : "s");
        return gFailures ? 1 : 0;
    }

    std::error_code ec;
    fs::create_directories(gOutdir, ec);
    std::printf("cosmo_touch_shots -> %s\n", gOutdir.string().c_str());
    shotsAt(kPortraitW, kPortraitH, "portrait");
    shotsAt(kSmallW, kSmallH, "small");
    shotsAt(kPortraitH, kPortraitW, "landscape");
    shotDesktopTouchMode(1600.0, 1000.0);
    return 0;
}
