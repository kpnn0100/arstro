/*
 *  Native Linux host for Cosmo by arstro: GTK3 drawing area -> Cairo (Artboard's
 *  CairoTarget). Images are decoded by NativeImageDecoder (GdkPixbuf for JPEG/PNG,
 *  LibRaw for RAW when built with COSMO_HAVE_LIBRAW). Open files via argv or press
 *  'O' for a file chooser.
 */
#include "CosmoApp.h"
#include "decode/NativeImageDecoder.h"
#include "../Artboard/src/adapter/native/CairoTarget.h"
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <string>

using arstro::cosmo::CosmoApp;
using arstro::cosmo::NativeImageDecoder;
using arstro::cosmo::DecodedImage;

namespace
{
    constexpr int kW = 1280, kH = 860;

    struct App
    {
        GtkWidget *window = nullptr;
        GtkWidget *area = nullptr;
        CosmoApp app{(double)kW, (double)kH};
        artboard::CairoTarget target;
        NativeImageDecoder decoder;
        gint64 startUs = 0;
    };

    double nowMs(const App &a) { return a.startUs == 0 ? 0.0 : (g_get_monotonic_time() - a.startUs) / 1000.0; }
    int mapButton(guint b) { return b == 3 ? 2 : 0; }

    void openPath(App *a, const std::string &path)
    {
        DecodedImage img = a->decoder.decodeFile(path);
        if (img.ok())
            a->app.openImage(img.rgba.data(), img.width, img.height, img.name);
        else
            g_printerr("cosmo: could not decode %s%s\n", path.c_str(),
                       (NativeImageDecoder::isRawExtension(path) && !NativeImageDecoder::rawSupported())
                           ? " (RAW needs a LibRaw build)" : "");
    }

    void openDialog(App *a)
    {
        GtkWidget *d = gtk_file_chooser_dialog_new(
            "Open image", GTK_WINDOW(a->window), GTK_FILE_CHOOSER_ACTION_OPEN,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(d), TRUE);
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            GSList *files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(d));
            for (GSList *it = files; it; it = it->next)
            {
                openPath(a, (const char *)it->data);
                g_free(it->data);
            }
            g_slist_free(files);
        }
        gtk_widget_destroy(d);
        gtk_widget_queue_draw(a->area);
    }

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
    gboolean onKey(GtkWidget *, GdkEventKey *e, gpointer user)
    {
        if (e->keyval == GDK_KEY_o || e->keyval == GDK_KEY_O)
        {
            openDialog(static_cast<App *>(user));
            return TRUE;
        }
        return FALSE;
    }
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);
    App app;
    app.startUs = g_get_monotonic_time();

    // open any files passed on the command line
    for (int i = 1; i < argc; ++i)
        openPath(&app, argv[i]);

    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "Cosmo by arstro");
    gtk_window_set_default_size(GTK_WINDOW(app.window), kW, kH);

    app.area = gtk_drawing_area_new();
    gtk_widget_set_size_request(app.area, kW, kH);
    gtk_widget_set_can_focus(app.area, TRUE);
    gtk_widget_add_events(app.area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                        GDK_POINTER_MOTION_MASK | GDK_KEY_PRESS_MASK);

    g_signal_connect(app.window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    g_signal_connect(app.area, "draw", G_CALLBACK(onDraw), &app);
    g_signal_connect(app.area, "button-press-event", G_CALLBACK(onButton), &app);
    g_signal_connect(app.area, "button-release-event", G_CALLBACK(onButton), &app);
    g_signal_connect(app.area, "motion-notify-event", G_CALLBACK(onMotion), &app);
    g_signal_connect(app.area, "key-press-event", G_CALLBACK(onKey), &app);

    gtk_container_add(GTK_CONTAINER(app.window), app.area);
    gtk_widget_show_all(app.window);
    gtk_widget_grab_focus(app.area);
    g_timeout_add(16, onTick, &app);
    gtk_main();
    return 0;
}
