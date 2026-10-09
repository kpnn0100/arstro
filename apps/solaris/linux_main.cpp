/*
 *  solaris — the GTK3 window (the host layer, docs/architecture.md).
 *
 *  The ONLY file with OS code in the GUI. It owns the window, the frame clock, the native file
 *  pickers, the codecs, the devices and the paths, and binds the app's two hooks to the service:
 *
 *      model     → SolarisService::model()
 *      dispatch  → SolarisService::dispatchText()   — the same text solaris-cc sends (R-G-4)
 *
 *  The service is pumped on every frame tick (the transport and the meters come back from the
 *  player through it); the window repaints only while the app says something is moving.
 */
#include "App.h"
#include "AudioFiles.h"
#include "EmbeddedFonts.h"
#include "Machine.h"
#include "SolarisService.h"
#ifdef SOLARIS_HAVE_PULSE
#include "AudioOutPulse.h"
#endif
#include "../../core/Artboard/src/adapter/native/CairoTarget.h"
#include <gtk/gtk.h>
#include <cmath>
#include <functional>
#include <memory>
#include <string>

using namespace arstro;
using arstro::solaris_ui::App;
using arstro::solaris_ui::AppHooks;

namespace
{
    constexpr int kW = 1440, kH = 900;

    solaris::SolarisService::Host makeServiceHost()
    {
        solaris::SolarisService::Host h;
        h.decodeAudio = solaris_host::decodeAudio;
        h.writeWav = solaris_host::writeWav;
        h.listDir = solaris_host::listDir;
        h.listDevices = solaris_host::listDevices;
#ifdef SOLARIS_HAVE_PULSE
        h.audioOut = [] { return std::unique_ptr<solaris::IAudioOut>(new solaris_host::AudioOutPulse()); };
#endif
        solaris_host::machinePaths(h.settingsPath, h.recentsPath); // the same files solaris-cc uses
        return h;
    }

    struct Host
    {
        solaris::SolarisService svc{makeServiceHost()};
        std::unique_ptr<App> app;
        artboard::CairoTarget target;
        GtkWidget *window = nullptr, *area = nullptr;
        gint64 startUs = 0;
    };

    double nowMs(const Host &a) { return a.startUs == 0 ? 0.0 : (g_get_monotonic_time() - a.startUs) / 1000.0; }

    std::string pick(Host *a, GtkFileChooserAction action, const char *title, const char *button, bool songs, const std::string &suggested)
    {
        GtkWidget *d = gtk_file_chooser_dialog_new(title, GTK_WINDOW(a->window), action, "_Cancel", GTK_RESPONSE_CANCEL, button,
                                                   GTK_RESPONSE_ACCEPT, nullptr);
        if (songs)
        {
            GtkFileFilter *f = gtk_file_filter_new();
            gtk_file_filter_set_name(f, "Solaris songs (.slp)");
            gtk_file_filter_add_pattern(f, "*.slp");
            gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(d), f);
        }
        if (action == GTK_FILE_CHOOSER_ACTION_SAVE)
        {
            gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(d), TRUE);
            gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d), suggested.c_str());
        }
        std::string out;
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
            if (char *p = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d))) { out = p; g_free(p); }
        gtk_widget_destroy(d);
        gtk_widget_queue_draw(a->area);
        return out;
    }

    gboolean onDraw(GtkWidget *, cairo_t *cr, gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        a->target.setContext(cr);
        a->app->render(a->target, nowMs(*a));
        return FALSE;
    }

    gboolean onTick(GtkWidget *, GdkFrameClock *, gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        a->svc.pump();
        if (a->app->needsRedraw(nowMs(*a))) gtk_widget_queue_draw(a->area);
        return G_SOURCE_CONTINUE;
    }

    gboolean onButton(GtkWidget *w, GdkEventButton *e, gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        if (e->type == GDK_2BUTTON_PRESS || e->type == GDK_3BUTTON_PRESS) return TRUE; // the app counts its own clicks
        gtk_widget_grab_focus(w);
        a->app->pointer(e->type == GDK_BUTTON_PRESS ? 0 : 2, e->x, e->y, e->button == 3 ? 2 : 0, nowMs(*a), (e->state & GDK_MOD1_MASK) != 0,
                        (e->state & GDK_SHIFT_MASK) != 0, (e->state & GDK_CONTROL_MASK) != 0);
        gtk_widget_queue_draw(a->area);
        return TRUE;
    }

    gboolean onMotion(GtkWidget *, GdkEventMotion *e, gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        a->app->pointer(1, e->x, e->y, 0, nowMs(*a), (e->state & GDK_MOD1_MASK) != 0, (e->state & GDK_SHIFT_MASK) != 0,
                        (e->state & GDK_CONTROL_MASK) != 0);
        gtk_widget_queue_draw(a->area);
        return TRUE;
    }

    gboolean onScroll(GtkWidget *, GdkEventScroll *e, gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        double dy = 0.0;
        if (e->direction == GDK_SCROLL_UP) dy = 1.0;
        else if (e->direction == GDK_SCROLL_DOWN) dy = -1.0;
        else if (e->direction == GDK_SCROLL_SMOOTH) dy = -e->delta_y;
        a->app->wheel(e->x, e->y, dy, (e->state & GDK_CONTROL_MASK) != 0);
        gtk_widget_queue_draw(a->area);
        return TRUE;
    }

    void onSizeAllocate(GtkWidget *, GtkAllocation *alloc, gpointer user) { static_cast<Host *>(user)->app->setSize(alloc->width, alloc->height); }

    gboolean onKey(GtkWidget *, GdkEventKey *e, gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        artboard::KeyEvent ke;
        ke.type = artboard::KeyEvent::Type::Down;
        ke.shift = (e->state & GDK_SHIFT_MASK) != 0;
        ke.ctrl = (e->state & GDK_CONTROL_MASK) != 0;
        ke.alt = (e->state & GDK_MOD1_MASK) != 0;
        switch (e->keyval)
        {
        case GDK_KEY_BackSpace: ke.keyCode = 8; break;
        case GDK_KEY_Return: case GDK_KEY_KP_Enter: ke.keyCode = 13; break;
        case GDK_KEY_Escape: ke.keyCode = 27; break;
        case GDK_KEY_space: ke.keyCode = 32; break;
        case GDK_KEY_Left: ke.keyCode = 37; break;
        case GDK_KEY_Up: ke.keyCode = 38; break;
        case GDK_KEY_Right: ke.keyCode = 39; break;
        case GDK_KEY_Down: ke.keyCode = 40; break;
        case GDK_KEY_Delete: ke.keyCode = 46; break;
        case GDK_KEY_comma: ke.keyCode = ','; break;
        default:
        {
            const gunichar u = gdk_keyval_to_unicode(gdk_keyval_to_upper(e->keyval));
            if (u >= '0' && u <= 'Z') ke.keyCode = (int)u; // the app's shortcuts use upper-case codes
        }
        }
        const bool handled = ke.keyCode && a->app->key(ke);
        gtk_widget_queue_draw(a->area);
        return handled ? TRUE : FALSE;
    }
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);
    arstro::cosmo_v2::registerEmbeddedFonts();
    gboolean animations = TRUE;
    g_object_get(gtk_settings_get_default(), "gtk-enable-animations", &animations, nullptr);
    artboard::setReducedMotion(!animations); // the OS's "reduce motion" reaches every primitive (design rule §2.6)

    auto host = std::make_unique<Host>();
    Host *a = host.get();
    AppHooks hooks;
    hooks.model = [a]() -> const solaris::AppModel & { return a->svc.model(); };
    hooks.dispatch = [a](const std::string &line, std::string &err) { return a->svc.dispatchText(line, err); };
    a->app = std::make_unique<App>(hooks, (double)kW, (double)kH);
    a->app->onPickSongToOpen = [a] {
        const std::string p = pick(a, GTK_FILE_CHOOSER_ACTION_OPEN, "Open song", "_Open", true, std::string());
        if (!p.empty()) a->app->openSongPicked(p);
    };
    a->app->onPickSongToCreate = [a] {
        const std::string p = pick(a, GTK_FILE_CHOOSER_ACTION_SAVE, "New song", "_Create", true, "Untitled.slp");
        if (!p.empty()) a->app->newSongPicked(p);
    };
    a->app->onPickSave = [a](const std::string &title, const std::string &suggested, std::function<void(const std::string &)> done) {
        const bool song = suggested.size() > 4 && suggested.compare(suggested.size() - 4, 4, ".slp") == 0;
        const std::string p = pick(a, GTK_FILE_CHOOSER_ACTION_SAVE, title.c_str(), "_Save", song, suggested);
        if (!p.empty()) done(p);
    };
    a->app->setOsReducedMotion(!animations); // the OS's "reduce motion" (design rule §2.6); the app's own setting ORs in
    a->app->onPickFolder = [a](std::function<void(const std::string &)> done) {
        const std::string p = pick(a, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "Add a sample folder", "_Add", false, std::string());
        if (!p.empty()) done(p);
    };

    a->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(a->window), "Solaris");
    gtk_window_set_default_size(GTK_WINDOW(a->window), kW, kH);
    g_signal_connect(a->window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    a->area = gtk_drawing_area_new();
    gtk_widget_set_size_request(a->area, (int)App::minWidth(), (int)App::minHeight());
    gtk_widget_set_can_focus(a->area, TRUE);
    gtk_widget_add_events(a->area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_KEY_PRESS_MASK |
                                       GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
    g_signal_connect(a->area, "draw", G_CALLBACK(onDraw), a);
    g_signal_connect(a->area, "button-press-event", G_CALLBACK(onButton), a);
    g_signal_connect(a->area, "button-release-event", G_CALLBACK(onButton), a);
    g_signal_connect(a->area, "motion-notify-event", G_CALLBACK(onMotion), a);
    g_signal_connect(a->area, "scroll-event", G_CALLBACK(onScroll), a);
    g_signal_connect(a->area, "size-allocate", G_CALLBACK(onSizeAllocate), a);
    g_signal_connect(a->area, "key-press-event", G_CALLBACK(onKey), a);
    gtk_container_add(GTK_CONTAINER(a->window), a->area);

    if (argc > 1)
    {
        std::string err;
        if (!a->svc.dispatchText("project open \"" + std::string(argv[1]) + "\"", err)) g_printerr("solaris: %s\n", err.c_str());
    }
    a->startUs = g_get_monotonic_time();
    gtk_widget_add_tick_callback(a->area, onTick, a, nullptr);
    gtk_widget_show_all(a->window);
    gtk_widget_grab_focus(a->area);
    gtk_main();
    return 0;
}
