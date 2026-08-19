/*
 *  Arstrobench by arstro — native host: a GTK3 drawing area -> Cairo, through
 *  Artboard's CairoTarget.
 *
 *  ONE host file for BOTH platforms. GTK3 + Cairo + Fontconfig are the same stack on
 *  Linux and on Windows under MSYS2/MinGW-w64, and nothing here is Linux-specific, so a
 *  second Windows host would be a copy that could only drift.
 *
 *  It registers cosmo's vendored DM Sans / JetBrains Mono as app-private fonts, so the
 *  shared theme's font families resolve to the same faces cosmo draws with (R-G-2).
 */
#include "BenchApp.h"
#include "../../core/Artboard/src/adapter/native/CairoTarget.h"
#include <fontconfig/fontconfig.h>
#include <gtk/gtk.h>
#include <string>

using arstro::arstrobench::BenchApp;

namespace
{
    struct Host
    {
        GtkWidget *window = nullptr;
        GtkWidget *area = nullptr;
        BenchApp app{BenchApp::kWidth, BenchApp::kHeight};
        artboard::CairoTarget target;
        gint64 startUs = 0;
    };

    double nowMs(const Host &h) { return h.startUs == 0 ? 0.0 : (g_get_monotonic_time() - h.startUs) / 1000.0; }
    int mapButton(guint b) { return b == 3 ? 2 : 0; }

    /** Register a vendored font file with THIS process's Fontconfig config only — no
     *  system install, exactly as cosmo does it. */
    void registerFont(const std::string &path)
    {
        if (!FcConfigAppFontAddFile(FcConfigGetCurrent(), (const FcChar8 *)path.c_str()))
            g_printerr("arstrobench: could not register font %s\n", path.c_str());
    }

    void registerBundledFonts()
    {
        // COSMO_ASSETS_DIR is baked in by CMakeLists.txt, so the fonts are found
        // regardless of the build directory or the working directory.
        const std::string dir = std::string(COSMO_ASSETS_DIR) + "/fonts";
        registerFont(dir + "/DMSans/DMSans-Regular.ttf");
        registerFont(dir + "/DMSans/DMSans-Medium.ttf");
        registerFont(dir + "/DMSans/DMSans-SemiBold.ttf");
        registerFont(dir + "/JetBrainsMono/JetBrainsMono-Regular.ttf");
        registerFont(dir + "/JetBrainsMono/JetBrainsMono-Medium.ttf");
    }

    gboolean onDraw(GtkWidget *, cairo_t *cr, gpointer user)
    {
        auto *h = static_cast<Host *>(user);
        h->target.setContext(cr);
        h->app.render(h->target, nowMs(*h));
        return FALSE;
    }
    gboolean onTick(gpointer user) { gtk_widget_queue_draw(static_cast<Host *>(user)->area); return G_SOURCE_CONTINUE; }
    gboolean onButton(GtkWidget *w, GdkEventButton *e, gpointer user)
    {
        auto *h = static_cast<Host *>(user);
        gtk_widget_grab_focus(w);
        h->app.pointer(e->type == GDK_BUTTON_PRESS ? 0 : 2, e->x, e->y, mapButton(e->button), nowMs(*h));
        return TRUE;
    }
    gboolean onMotion(GtkWidget *, GdkEventMotion *e, gpointer user)
    {
        auto *h = static_cast<Host *>(user);
        h->app.pointer(1, e->x, e->y, 0, nowMs(*h));
        return TRUE;
    }
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);
    registerBundledFonts();

    Host host;
    host.startUs = g_get_monotonic_time();

    const int w = (int)BenchApp::kWidth, h = (int)BenchApp::kHeight;
    host.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(host.window), "Arstrobench by arstro");
    gtk_window_set_default_size(GTK_WINDOW(host.window), w, h);
    // Fixed composition (R-UI-1): the layout is sized to its content, so resizing could
    // only clip it. Making it resizable requires giving every panel clip-and-scroll first.
    gtk_window_set_resizable(GTK_WINDOW(host.window), FALSE);

    host.area = gtk_drawing_area_new();
    gtk_widget_set_size_request(host.area, w, h);
    gtk_widget_set_can_focus(host.area, TRUE);
    gtk_widget_add_events(host.area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);

    g_signal_connect(host.window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    g_signal_connect(host.area, "draw", G_CALLBACK(onDraw), &host);
    g_signal_connect(host.area, "button-press-event", G_CALLBACK(onButton), &host);
    g_signal_connect(host.area, "button-release-event", G_CALLBACK(onButton), &host);
    g_signal_connect(host.area, "motion-notify-event", G_CALLBACK(onMotion), &host);

    gtk_container_add(GTK_CONTAINER(host.window), host.area);
    gtk_widget_show_all(host.window);
    gtk_widget_grab_focus(host.area);
    g_timeout_add(16, onTick, &host);  // ~60 fps: the UI keeps animating while a run works
    gtk_main();
    return 0;
}
