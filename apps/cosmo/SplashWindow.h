/*
 *  cosmo_v2 by arstro — SplashWindow: the small borderless window a `SplashScreen` plays in, for a
 *  host that has NO deferred startup work to report (R-SPLASH-6).
 *
 *  Cosmo's own host does not use this: it has real work to name (scanning projects, decoding
 *  covers — R-SPLASH-2a/3), so it owns its splash window and feeds it progress. Interstellar and
 *  Solaris open instantly, so for them the splash is only the logo's life: show up, a beat of
 *  flight, disappear — then `done()` shows the main window. Header-only and GTK-only, included by
 *  a host's linux_main.cpp; nothing in a core or a widget may include it.
 *
 *  The cost is honest and written down: about 2.0 s of launch (1000 ms show up, 240 ms easing out
 *  of the loop, 720 ms disappear). A host skips it when it is launched to DO something — a file
 *  on the command line, a control socket — exactly as cosmo skips straight through when launched
 *  with images (R-SPLASH-3).
 */
#pragma once
#include "widgets/SplashScreen.h"
#include "../../core/Artboard/src/adapter/native/CairoTarget.h"
#include <cmath>
#include <functional>
#include <gtk/gtk.h>
#include <memory>

namespace arstro
{
namespace cosmo_v2
{
    namespace splashwindow
    {
        struct State
        {
            GtkWidget *window = nullptr, *area = nullptr;
            std::shared_ptr<SplashScreen> splash;
            artboard::CairoTarget target;
            gint64 startUs = 0;
            double scale = 1.0;
            bool asked = false;
            std::function<void()> done;
            double now() const { return (g_get_monotonic_time() - startUs) / 1000.0; }
        };

        inline gboolean onDraw(GtkWidget *, cairo_t *cr, gpointer user)
        {
            auto *s = static_cast<State *>(user);
            if (!s->splash) return FALSE;
            s->target.setContext(cr);
            // Chrome is drawn at the UI scale through the ROOT transform, like cosmo's own splash
            // (R-SCALE-2): the Segment keeps its 420x260 design size and the window is sized to match.
            const artboard::Transform root = artboard::Transform::scaling(s->scale, s->scale);
            s->splash->render(s->target, root);
            s->splash->renderOverlay(s->target, root);
            return FALSE;
        }

        inline gboolean onTick(gpointer user)
        {
            auto *s = static_cast<State *>(user);
            s->splash->advance(s->now());
            // Nothing to load: once the logo has shown up, the bar fills and the splash is asked
            // to leave. The logo then eases out of its loop and plays its disappear.
            if (!s->asked && s->splash->markPlayer().phase() != brand::Phase::In && s->splash->introDone())
            {
                s->asked = true;
                s->splash->setProgress(1.0);
                s->splash->beginExit();
            }
            gtk_widget_queue_draw(s->area);
            if (s->splash->isGone())
            {
                gtk_widget_destroy(s->window);
                auto done = std::move(s->done);
                delete s;
                if (done) done();
                return G_SOURCE_REMOVE;
            }
            return G_SOURCE_CONTINUE;
        }
    }

    /** Play `brandInfo`'s splash in its own small window, then call `done()`. Returns at once. */
    inline void runSplash(const SplashScreen::Brand &brandInfo, double uiScale, std::function<void()> done)
    {
        auto *s = new splashwindow::State;
        s->scale = uiScale > 0.0 ? uiScale : 1.0;
        s->done = std::move(done);
        s->startUs = g_get_monotonic_time();
        s->splash = std::make_shared<SplashScreen>(brandInfo);
        s->splash->width.set(SplashScreen::kWidth);
        s->splash->height.set(SplashScreen::kHeight);
        s->splash->begin(0.0);

        s->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);   // R-SPLASH-1: small, undecorated, centred
        gtk_window_set_decorated(GTK_WINDOW(s->window), FALSE);
        gtk_window_set_resizable(GTK_WINDOW(s->window), FALSE);
        gtk_window_set_position(GTK_WINDOW(s->window), GTK_WIN_POS_CENTER);
        gtk_window_set_type_hint(GTK_WINDOW(s->window), GDK_WINDOW_TYPE_HINT_SPLASHSCREEN);
        gtk_window_set_default_size(GTK_WINDOW(s->window), (int)std::ceil(SplashScreen::kWidth * s->scale),
                                    (int)std::ceil(SplashScreen::kHeight * s->scale));
        s->area = gtk_drawing_area_new();
        g_signal_connect(s->area, "draw", G_CALLBACK(splashwindow::onDraw), s);
        gtk_container_add(GTK_CONTAINER(s->window), s->area);
        gtk_widget_show_all(s->window);
        g_timeout_add(16, splashwindow::onTick, s);
    }
}
}
