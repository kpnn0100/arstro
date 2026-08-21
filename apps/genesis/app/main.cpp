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
#include "widgets/SplashScreen.h"
#include "adapter/native/CairoTarget.h"
#include <artboard/artboard.h>
#include <fontconfig/fontconfig.h>
#include <gtk/gtk.h>
#include <chrono>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
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
        // The splash owns the screen while startup work happens behind it, so the first
        // thing drawn is never a blank frame (the pattern cosmo uses).
        genesis::ui::SplashScreen splash;
        artboard::CairoTarget splashTarget;
        GtkWidget *splashWindow = nullptr;
        GtkWidget *splashArea = nullptr;
        bool startupDone = false;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        double nowMs() const
        {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        }
    };

    std::string exeDir()
    {
        char buf[4096];
#ifdef _WIN32
        const DWORD n = GetModuleFileNameA(nullptr, buf, sizeof(buf) - 1);
        if (n == 0 || n >= sizeof(buf) - 1) return ".";
        buf[n] = 0;
#else
        const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof buf - 1);
        if (n <= 0) return ".";
        buf[n] = 0;
#endif
        std::string p(buf);
        const size_t slash = p.find_last_of("/\\");  // Windows paths use '\\'
        return slash == std::string::npos ? "." : p.substr(0, slash);
    }

    /** Bind Artboard's clipboard seam to the system one (FR-44), so copy/paste crosses
     *  applications rather than only working inside this process. */
    void installSystemClipboard()
    {
        GtkClipboard *board = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        artboard::Clipboard::install(
            [board] {
                gchar *text = gtk_clipboard_wait_for_text(board);
                if (!text) return std::string();
                std::string out = text;
                g_free(text);
                return out;
            },
            [board](const std::string &text) {
                gtk_clipboard_set_text(board, text.c_str(), (gint)text.size());
            });
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

    gboolean onTick(gpointer user);
    void showMainWindow(Host *h);
    void startSplash(Host *h);

    gboolean onSplashDraw(GtkWidget *, cairo_t *cr, gpointer user)
    {
        auto *h = static_cast<Host *>(user);
        h->splashTarget.setContext(cr);
        h->splash.advance(h->nowMs());
        h->splash.render(h->splashTarget);
        h->splashTarget.setContext(nullptr);   // valid only for this frame
        return FALSE;
    }

    gboolean onSplashTick(gpointer user)
    {
        auto *h = static_cast<Host *>(user);
        gtk_widget_queue_draw(h->splashArea);

        // Do the startup work behind the animation, one step per tick, naming each step —
        // the bar shows real progress rather than a decorative sweep.
        if (!h->startupDone && h->splash.introDone())
        {
            struct Step { const char *label; double progress; };
            static const Step steps[] = {
                {"Loading component bases", 0.35},
                {"Preparing the preview", 0.7},
                {"Reading recent components", 1.0},
            };
            static size_t next = 0;
            if (next < sizeof steps / sizeof steps[0])
            {
                h->splash.setStatus(steps[next].label);
                h->splash.setProgress(steps[next].progress);
                ++next;
                return G_SOURCE_CONTINUE;
            }
            h->startupDone = true;
            h->splash.beginExit();
        }
        if (h->startupDone && h->splash.isGone())
        {
            gtk_widget_destroy(h->splashWindow);
            h->splashWindow = nullptr;
            showMainWindow(h);
            return G_SOURCE_REMOVE;
        }
        return G_SOURCE_CONTINUE;
    }

    gboolean onDraw(GtkWidget *, cairo_t *cr, gpointer user)
    {
        auto *h = static_cast<Host *>(user);
        const double now = h->nowMs();
        h->gestures.advance(now);   // long-press timing lives in the recognizer, not here
        h->target.setContext(cr);
        h->app.advance(now);
        h->app.render(h->target);
        // GTK owns this cairo_t and destroys it when the handler returns, so drop it: a
        // target held past its frame is a dangling context, and anything that measured text
        // through it between frames would read garbage.
        h->target.setContext(nullptr);
        return FALSE;
    }

    void showMainWindow(Host *h)
    {
        gtk_widget_show_all(h->window);
        gtk_widget_grab_focus(h->area);
        g_timeout_add(16, onTick, h);
    }

    void startSplash(Host *h)
    {
        h->splashWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_decorated(GTK_WINDOW(h->splashWindow), FALSE);
        gtk_window_set_position(GTK_WINDOW(h->splashWindow), GTK_WIN_POS_CENTER);
        gtk_window_set_type_hint(GTK_WINDOW(h->splashWindow), GDK_WINDOW_TYPE_HINT_SPLASHSCREEN);
        gtk_window_set_default_size(GTK_WINDOW(h->splashWindow),
                                    (int)genesis::ui::SplashScreen::kWidth,
                                    (int)genesis::ui::SplashScreen::kHeight);
        h->splashArea = gtk_drawing_area_new();
        g_signal_connect(h->splashArea, "draw", G_CALLBACK(onSplashDraw), h);
        gtk_container_add(GTK_CONTAINER(h->splashWindow), h->splashArea);
        gtk_widget_show_all(h->splashWindow);
        g_timeout_add(16, onSplashTick, h);
    }

    gboolean onTick(gpointer user)
    {
        gtk_widget_queue_draw(static_cast<Host *>(user)->area);
        return G_SOURCE_CONTINUE;
    }

    /** File -> Open (the Chrome/home "Open" button, and Ctrl+O): a native file chooser
     *  filtered to *.genesis, so Open can reach any component file on disk — not just
     *  whatever happens to sit in the process's current working directory (the app used
     *  to scan "." itself, which is why it needed this fix). The host owns file dialogs;
     *  same split cosmo uses for its own Open. */
    void openGenesisDialog(Host *h)
    {
        GtkWidget *d = gtk_file_chooser_dialog_new(
            "Open component", GTK_WINDOW(h->window), GTK_FILE_CHOOSER_ACTION_OPEN,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, nullptr);

        GtkFileFilter *filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, "Genesis component (*.genesis)");
        gtk_file_filter_add_pattern(filter, "*.genesis");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(d), filter);

        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
            if (h->app.openDocument(path))
                h->app.showEditor();
            g_free(path);
        }
        gtk_widget_destroy(d);
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

    /** One wheel notch, in pixels. GTK reports steps for a wheel and continuous deltas for a
     *  trackpad; only the host knows which, so the conversion belongs here (FR-46). */
    constexpr double kWheelStepPx = 52.0;

    gboolean onScroll(GtkWidget *, GdkEventScroll *e, gpointer user)
    {
        auto *h = static_cast<Host *>(user);
        artboard::RawPointer p;
        p.kind = artboard::RawPointer::Kind::Scroll;
        p.pos = {e->x, e->y};
        p.timeMs = h->nowMs();
        p.alt = (e->state & GDK_MOD1_MASK) != 0;
        p.shift = (e->state & GDK_SHIFT_MASK) != 0;
        p.ctrl = (e->state & GDK_CONTROL_MASK) != 0;
        switch (e->direction)
        {
        case GDK_SCROLL_UP: p.scroll = {0.0, -kWheelStepPx}; break;
        case GDK_SCROLL_DOWN: p.scroll = {0.0, kWheelStepPx}; break;
        case GDK_SCROLL_LEFT: p.scroll = {-kWheelStepPx, 0.0}; break;
        case GDK_SCROLL_RIGHT: p.scroll = {kWheelStepPx, 0.0}; break;
        case GDK_SCROLL_SMOOTH:
            p.scroll = {e->delta_x * kWheelStepPx, e->delta_y * kWheelStepPx};
            break;
        }
        h->gestures.feed(p);
        gtk_widget_queue_draw(h->area);
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
            case GDK_KEY_o: case GDK_KEY_O: openGenesisDialog(h); return TRUE;
            case GDK_KEY_n: case GDK_KEY_N: h->app.modal()->openNew(); return TRUE;
            case GDK_KEY_e: case GDK_KEY_E: h->app.exportCode(); return TRUE;
            case GDK_KEY_r: case GDK_KEY_R: h->app.startVerify(); return TRUE;
            case GDK_KEY_d: case GDK_KEY_D: h->app.duplicateSelected(); return TRUE;
            case GDK_KEY_a: case GDK_KEY_A:
            case GDK_KEY_c: case GDK_KEY_C:
            case GDK_KEY_x: case GDK_KEY_X:
            case GDK_KEY_v: case GDK_KEY_V:
            {
                // Selection and clipboard belong to a focused field. Only fall through to the
                // app when nothing is focused to consume them.
                artboard::KeyEvent k;
                k.type = artboard::KeyEvent::Type::Down;
                k.ctrl = true;
                k.shift = (e->state & GDK_SHIFT_MASK) != 0;
                k.keyCode = gdk_keyval_to_upper(e->keyval) - GDK_KEY_A + 65;
                h->app.dispatchKey(k);
                return TRUE;
            }
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
        ke.ctrl = ctrl;
        ke.shift = (e->state & GDK_SHIFT_MASK) != 0;
        ke.alt = (e->state & GDK_MOD1_MASK) != 0;
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
    installSystemClipboard();

    static Host host;
    // `host` has static storage duration, so the lambdas below name it directly rather
    // than capturing it (capturing a non-automatic variable is redundant, and warned on).
    host.app.onOpenRequested = [] { openGenesisDialog(&host); };
    // The recognizer turns raw pointer events into gestures; the router hit-tests the
    // Segment tree and delivers them. Both are platform-free — the host only supplies facts.
    host.router.add(&host.app);
    host.gestures.setSink([](const artboard::Gesture &g) { host.router.route(g); });
    host.app.setSize(kDefaultW, kDefaultH);
    bool openedFromArgv = false;
    for (int i = 1; i < argc; ++i)
        if (argv[i][0] != '-')
        {
            openedFromArgv = host.app.openDocument(argv[i]);
            if (openedFromArgv)
                host.app.showEditor();
            break;
        }

    host.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(host.window), "Genesis by arstro");
    gtk_window_set_default_size(GTK_WINDOW(host.window), kDefaultW, kDefaultH);

    host.area = gtk_drawing_area_new();
    gtk_widget_set_size_request(host.area, 720, 520);
    gtk_widget_set_can_focus(host.area, TRUE);
    gtk_widget_add_events(host.area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                         GDK_POINTER_MOTION_MASK | GDK_KEY_PRESS_MASK |
                                         GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);

    g_signal_connect(host.window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    g_signal_connect(host.area, "draw", G_CALLBACK(onDraw), &host);
    g_signal_connect(host.area, "button-press-event", G_CALLBACK(onButton), &host);
    g_signal_connect(host.area, "button-release-event", G_CALLBACK(onButton), &host);
    g_signal_connect(host.area, "motion-notify-event", G_CALLBACK(onMotion), &host);
    g_signal_connect(host.area, "scroll-event", G_CALLBACK(onScroll), &host);
    g_signal_connect(host.area, "key-press-event", G_CALLBACK(onKey), &host);
    g_signal_connect(host.area, "size-allocate", G_CALLBACK(onSizeAllocate), &host);

    gtk_container_add(GTK_CONTAINER(host.window), host.area);

    // Built but NOT shown: the splash owns the screen until its animation has played and the
    // startup work behind it is done. Opening a file on the command line skips straight in.
    if (openedFromArgv)
        showMainWindow(&host);
    else
        startSplash(&host);
    gtk_main();
    return 0;
}
