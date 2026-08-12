/*
 *  Genesis — headless screenshot harness.
 *
 *  Renders the real app through the real Cairo adapter into a PNG, with no display, so a
 *  design change can be INSPECTED rather than assumed. Drives the app to a given state
 *  (selection, panel, modal, window size) before the shot, because "draw every state" is
 *  only checkable if every state can be rendered.
 */
#include "App.h"
#include "adapter/native/CairoTarget.h"
#include "widgets/HomeScreen.h"
#include "widgets/Modal.h"
#include "widgets/SplashScreen.h"
#include <cairo/cairo.h>
#include <cstdio>
#include <functional>
#include <string>

namespace
{
    void shoot(const std::string &name, int w, int h,
               const std::function<void(genesis::ui::App &, double &)> &drive)
    {
        genesis::ui::App app;
        app.setSize(w, h);
        double now = 0.0;
        drive(app, now);

        cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cairo_t *cr = cairo_create(surface);
        artboard::CairoTarget target;
        target.setContext(cr);
        app.advance(now);
        app.render(target);
        cairo_surface_write_to_png(surface, name.c_str());
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        std::printf("wrote %s (%dx%d)\n", name.c_str(), w, h);
    }

    /** Settle animations by ticking real frames, the way the host does. */
    void settle(genesis::ui::App &app, double &now, double ms)
    {
        for (double t = 0; t < ms; t += 16.0)
        {
            now += 16.0;
            app.advance(now);
        }
    }
}

int main(int argc, char **argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";

    // The launcher, which is what opens first.
    shoot(dir + "/genesis-home.png", 1360, 860, [](genesis::ui::App &a, double &now) {
        settle(a, now, 900.0);
    });
    shoot(dir + "/genesis-1360x860.png", 1360, 860, [](genesis::ui::App &a, double &now) {
        a.showEditor();
        settle(a, now, 1200.0);   // the starter loop has faded in and is spinning
    });
    // Mid-transition between the two screens: they cross-fade, neither pops.
    shoot(dir + "/genesis-transition.png", 1360, 860, [](genesis::ui::App &a, double &now) {
        settle(a, now, 400.0);
        a.showEditor();
        settle(a, now, 150.0);
    });
    shoot(dir + "/genesis-1024x640.png", 1024, 640, [](genesis::ui::App &a, double &now) {
        a.showEditor();
        settle(a, now, 1200.0);   // the same app, reflowed for a smaller window (R4)
    });
    shoot(dir + "/genesis-empty.png", 1200, 780, [](genesis::ui::App &a, double &now) {
        a.showEditor();
        a.doc().shapes.clear();
        a.doc().reactions.clear();
        a.selectShape("");
        a.documentChanged();
        settle(a, now, 400.0);   // the empty state: no shapes, no reactions
    });
    shoot(dir + "/genesis-error.png", 1200, 780, [](genesis::ui::App &a, double &now) {
        a.showEditor();
        a.doc().findShape("ring")->setField("w", "mystery * 2");
        a.documentChanged();
        settle(a, now, 400.0);   // an authoring error: the preview says so instead of lying
    });
    shoot(dir + "/genesis-modal.png", 1200, 780, [](genesis::ui::App &a, double &now) {
        a.showEditor();
        settle(a, now, 600.0);
        a.modal()->openNew();
        settle(a, now, 400.0);   // the New dialog, over a dimmed app
    });
    shoot(dir + "/genesis-progress.png", 1200, 780, [](genesis::ui::App &a, double &now) {
        a.showEditor();
        a.newDocument("ProgressIndicator", "SweepBar");
        a.selectShape("fill");
        a.runtime().setProgress(0.65);
        settle(a, now, 1200.0);   // a different base, driven to a partial value
    });
    shoot(dir + "/genesis-button.png", 1200, 780, [](genesis::ui::App &a, double &now) {
        a.showEditor();
        a.newDocument("Button", "PillButton");
        a.selectShape("body");
        a.setPreviewHover(true);
        settle(a, now, 400.0);
        a.runtime().setPressed(true);
        settle(a, now, 120.0);    // a Button starter, hovered and caught mid-press
    });
    shoot(dir + "/genesis-arc.png", 1200, 780, [&dir](genesis::ui::App &a, double &now) {
        a.showEditor();
        settle(a, now, 600.0);
        a.openDocument("genesis/samples/ArcSpinner.genesis");
        a.selectShape("arc");
        settle(a, now, 1400.0);   // a trimmed circle: an arc, mid-spin
    });
    shoot(dir + "/genesis-pacman.png", 1200, 780, [](genesis::ui::App &a, double &now) {
        a.showEditor();
        settle(a, now, 600.0);
        a.openDocument("genesis/samples/PacmanLoader.genesis");
        a.selectShape("body");
        settle(a, now, 900.0);   // a sector: a disk with a wedge cut out by two rays
    });
    shoot(dir + "/genesis-donut.png", 1200, 780, [](genesis::ui::App &a, double &now) {
        a.showEditor();
        settle(a, now, 600.0);
        a.openDocument("genesis/samples/DonutGauge.genesis");
        a.runtime().setProgress(0.68);
        a.selectShape("fill");
        settle(a, now, 1400.0);   // a ring segment following base.display
    });
    // The launch splash, rendered on its own (the host shows it in its own window).
    {
        genesis::ui::SplashScreen splash;
        const int w = (int)genesis::ui::SplashScreen::kWidth;
        const int h = (int)genesis::ui::SplashScreen::kHeight;
        splash.setStatus("Preparing the preview");
        splash.setProgress(0.7);
        cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cairo_t *cr = cairo_create(surface);
        artboard::CairoTarget target;
        target.setContext(cr);
        for (double t = 0; t <= 700.0; t += 16.0)
            splash.advance(t);
        splash.render(target);
        const std::string name = dir + "/genesis-splash.png";
        cairo_surface_write_to_png(surface, name.c_str());
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        std::printf("wrote %s (%dx%d)\n", name.c_str(), w, h);
    }
    return 0;
}
