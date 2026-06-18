/*
 *  Native Linux host for Pulsar by arstro: GTK3 drawing area -> Cairo, via
 *  Artboard's CairoTarget. UI-only (no audio yet).
 */
#include "PulsarApp.h"
#include "../Artboard/src/adapter/native/CairoTarget.h"
#include <gtk/gtk.h>

using arstro::pulsar::PulsarApp;

namespace
{
    constexpr int kW = 1340, kH = 680;

    struct App
    {
        GtkWidget *window = nullptr;
        GtkWidget *area = nullptr;
        PulsarApp app{(double)kW, (double)kH};
        artboard::CairoTarget target;
        gint64 startUs = 0;
    };

    double nowMs(const App &a) { return a.startUs == 0 ? 0.0 : (g_get_monotonic_time() - a.startUs) / 1000.0; }
    int mapButton(guint b) { return b == 3 ? 2 : 0; }

    gboolean onDraw(GtkWidget *, cairo_t *cr, gpointer user)
    {
        auto *a = static_cast<App *>(user);
        a->target.setContext(cr);
        a->app.render(a->target, nowMs(*a));
        return FALSE;
    }
    gboolean onTick(gpointer user) { gtk_widget_queue_draw(static_cast<App *>(user)->area); return G_SOURCE_CONTINUE; }
    gboolean onButton(GtkWidget *w, GdkEventButton *e, gpointer user)
    {
        auto *a = static_cast<App *>(user);
        gtk_widget_grab_focus(w);
        a->app.pointer(e->type == GDK_BUTTON_PRESS ? 0 : 2, e->x, e->y, mapButton(e->button), nowMs(*a));
        return TRUE;
    }
    gboolean onMotion(GtkWidget *, GdkEventMotion *e, gpointer user)
    {
        auto *a = static_cast<App *>(user);
        a->app.pointer(1, e->x, e->y, 0, nowMs(*a));
        return TRUE;
    }
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);
    App app;
    app.startUs = g_get_monotonic_time();

    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "Pulsar by arstro");
    gtk_window_set_default_size(GTK_WINDOW(app.window), kW, kH);
    gtk_window_set_resizable(GTK_WINDOW(app.window), FALSE);

    app.area = gtk_drawing_area_new();
    gtk_widget_set_size_request(app.area, kW, kH);
    gtk_widget_set_can_focus(app.area, TRUE);
    gtk_widget_add_events(app.area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);

    g_signal_connect(app.window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    g_signal_connect(app.area, "draw", G_CALLBACK(onDraw), &app);
    g_signal_connect(app.area, "button-press-event", G_CALLBACK(onButton), &app);
    g_signal_connect(app.area, "button-release-event", G_CALLBACK(onButton), &app);
    g_signal_connect(app.area, "motion-notify-event", G_CALLBACK(onMotion), &app);

    gtk_container_add(GTK_CONTAINER(app.window), app.area);
    gtk_widget_show_all(app.window);
    gtk_widget_grab_focus(app.area);
    g_timeout_add(16, onTick, &app);
    gtk_main();
    return 0;
}
