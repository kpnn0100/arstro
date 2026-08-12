/*
 *  Genesis — the Linux host.
 *
 *  A GTK3 window, a Cairo surface, and the Artboard native adapter. The host does exactly
 *  three things: feed raw pointer/key events into Artboard's platform-free input path, drive
 *  one frame per tick, and hand the app a real render target. Everything else — layout,
 *  motion, hit-testing — lives in the platform-free code above it, which is why the same app
 *  renders headlessly in the tests.
 */
#include "App.h"
#include "Theme.h"
#include "widgets/Modal.h"
#include "adapter/native/CairoTarget.h"
#include <artboard/artboard.h>
#include <fontconfig/fontconfig.h>
#include <gtk/gtk.h>
#include <chrono>
#include <string>
#include <unistd.h>
#include <vector>

namespace
{
    constexpr int kDefaultW = 1360;
    constexpr int kDefaultH = 860;

    struct Host
    {
        genesis::ui::App app;
        artboard::CairoTarget target;
        artboard::GestureRecognizer gestures;
        artboard::InputRouter router;
        GtkWidget *window = nullptr;
        GtkWidget *area = nullptr;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        double nowMs() const
        {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        }
    };

    std::string exeDir()
    {
        char buf[4096];
        const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof buf - 1);
        if (n <= 0) return ".";
        buf[n] = 0;
        std::string p(buf);
        const size_t slash = p.find_last_of('/');
        return slash == std::string::npos ? "." : p.substr(0, slash);
    }

    /** Register the vendored UI fonts so drawText's family names resolve (Artboard FR-22).
     *  Missing fonts are not fatal: the adapter falls back to a generic sans. */
    void registerFonts()
    {
        const std::string dir = exeDir() + "/assets/fonts";
        const char *files[] = {"DMSans-Regular.ttf",  "DMSans-Medium.ttf",  "DMSans-SemiBold.ttf",
                               "JetBrainsMono-Regular.ttf", "JetBrainsMono-Medium.ttf"};
        for (const char *f : files)
        {
            const std::string path = dir + "/" + f;
            if (::access(path.c_str(), R_OK) == 0)
                FcConfigAppFontAddFile(FcConfigGetCurrent(), (const FcChar8 *)path.c_str());
        }
    }

    artboard::PointerButton mapButton(guint b)
    {
        if (b == 3) return artboard::PointerButton::Right;
        if (b == 2) return artboard::PointerButton::Middle;
        return artboard::PointerButton::Left;
    }

    void feed(Host *h, artboard::RawPointer::Kind kind, double x, double y,
              artboard::PointerButton button, guint state)
    {
        artboard::RawPointer p;
        p.kind = kind;
        p.pos = {x, y};
        p.button = button;
        p.timeMs = h->nowMs();
        p.alt = (state & GDK_MOD1_MASK) != 0;
        p.shift = (state & GDK_SHIFT_MASK) != 0;
        p.ctrl = (state & GDK_CONTROL_MASK) != 0;
        h->gestures.feed(p);   // the sink routes each synthesized gesture
    }

    gboolean onDraw(GtkWidget *, cairo_t *cr, gpointer user)
    {
        auto *h = static_cast<Host *>(user);
        const double now = h->nowMs();
        h->gestures.advance(now);   // long-press timing lives in the recognizer, not here
        h->target.setContext(cr);
        h->app.advance(now);
        h->app.render(h->target);
        return FALSE;
    }

    gboolean onTick(gpointer user)
    {
        gtk_widget_queue_draw(static_cast<Host *>(user)->area);
        return G_SOURCE_CONTINUE;
    }

    gboolean onButton(GtkWidget *w, GdkEventButton *e, gpointer user)
    {
        auto *h = static_cast<Host *>(user);
        gtk_widget_grab_focus(w);
        feed(h, e->type == GDK_BUTTON_PRESS ? artboard::RawPointer::Kind::Down
                                            : artboard::RawPointer::Kind::Up,
             e->x, e->y, mapButton(e->button), e->state);
        return TRUE;
    }

    gboolean onMotion(GtkWidget *, GdkEventMotion *e, gpointer user)
    {
        feed(static_cast<Host *>(user), artboard::RawPointer::Kind::Move, e->x, e->y,
             artboard::PointerButton::Left, e->state);
        return TRUE;
    }

    void onSizeAllocate(GtkWidget *, GtkAllocation *alloc, gpointer user)
    {
        static_cast<Host *>(user)->app.setSize(alloc->width, alloc->height);
    }

    gboolean onKey(GtkWidget *, GdkEventKey *e, gpointer user)
    {
        auto *h = static_cast<Host *>(user);
        const bool ctrl = (e->state & GDK_CONTROL_MASK) != 0;

        // App shortcuts first, but only with a modifier, so typing into a focused
        // expression field is never stolen by a single-letter accelerator.
        if (ctrl)
        {
            switch (e->keyval)
            {
            case GDK_KEY_s: case GDK_KEY_S: h->app.saveDocument(); return TRUE;
            case GDK_KEY_o: case GDK_KEY_O: h->app.modal()->openBrowse(); return TRUE;
            case GDK_KEY_n: case GDK_KEY_N: h->app.modal()->openNew(); return TRUE;
            case GDK_KEY_e: case GDK_KEY_E: h->app.exportCode(); return TRUE;
            case GDK_KEY_r: case GDK_KEY_R: h->app.startVerify(); return TRUE;
            case GDK_KEY_z: case GDK_KEY_Z:
                if ((e->state & GDK_SHIFT_MASK) != 0) h->app.redo();
                else h->app.undo();
                return TRUE;
            case GDK_KEY_y: case GDK_KEY_Y: h->app.redo(); return TRUE;
            default: break;
            }
        }

        artboard::KeyEvent ke;
        ke.type = artboard::KeyEvent::Type::Down;
        switch (e->keyval)
        {
        case GDK_KEY_BackSpace: ke.keyCode = 8; break;
        case GDK_KEY_Delete: case GDK_KEY_KP_Delete: ke.keyCode = 46; break;
        case GDK_KEY_Left: ke.keyCode = 37; break;
        case GDK_KEY_Right: ke.keyCode = 39; break;
        case GDK_KEY_Home: ke.keyCode = 36; break;
        case GDK_KEY_End: ke.keyCode = 35; break;
        case GDK_KEY_Return: case GDK_KEY_KP_Enter: ke.keyCode = 13; break;
        case GDK_KEY_Escape: ke.keyCode = 27; break;
        case GDK_KEY_Tab: ke.keyCode = 9; break;
        default: ke.keyCode = 0; break;
        }
        if (ke.keyCode)
        {
            if (e->keyval == GDK_KEY_Escape && h->app.modal()->isOpen())
            {
                h->app.modal()->close();
                return TRUE;
            }
            h->app.dispatchKey(ke);
            return TRUE;
        }

        const guint32 unicode = gdk_keyval_to_unicode(e->keyval);
        if (unicode >= 0x20)
        {
            char utf8[8] = {0};
            const gint len = g_unichar_to_utf8((gunichar)unicode, utf8);
            utf8[len] = 0;
            artboard::KeyEvent text;
            text.type = artboard::KeyEvent::Type::Text;
            text.text = utf8;
            h->app.dispatchKey(text);
            return TRUE;
        }
        return FALSE;
    }
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);
    registerFonts();

    static Host host;
    // The recognizer turns raw pointer events into gestures; the router hit-tests the
    // Segment tree and delivers them. Both are platform-free — the host only supplies facts.
    host.router.add(&host.app);
    host.gestures.setSink([](const artboard::Gesture &g) { host.router.route(g); });
    host.app.setSize(kDefaultW, kDefaultH);
    for (int i = 1; i < argc; ++i)
        if (argv[i][0] != '-')
        {
            host.app.openDocument(argv[i]);
            break;
        }

    host.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(host.window), "Genesis by arstro");
    gtk_window_set_default_size(GTK_WINDOW(host.window), kDefaultW, kDefaultH);

    host.area = gtk_drawing_area_new();
    gtk_widget_set_size_request(host.area, 720, 520);
    gtk_widget_set_can_focus(host.area, TRUE);
    gtk_widget_add_events(host.area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                         GDK_POINTER_MOTION_MASK | GDK_KEY_PRESS_MASK);

    g_signal_connect(host.window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    g_signal_connect(host.area, "draw", G_CALLBACK(onDraw), &host);
    g_signal_connect(host.area, "button-press-event", G_CALLBACK(onButton), &host);
    g_signal_connect(host.area, "button-release-event", G_CALLBACK(onButton), &host);
    g_signal_connect(host.area, "motion-notify-event", G_CALLBACK(onMotion), &host);
    g_signal_connect(host.area, "key-press-event", G_CALLBACK(onKey), &host);
    g_signal_connect(host.area, "size-allocate", G_CALLBACK(onSizeAllocate), &host);

    gtk_container_add(GTK_CONTAINER(host.window), host.area);
    gtk_widget_show_all(host.window);
    gtk_widget_grab_focus(host.area);
    g_timeout_add(16, onTick, &host);
    gtk_main();
    return 0;
}
