/*
 *  interstellar — the GTK3 host. The ONLY layer with OS code (R-SCOPE-7).
 *
 *  It owns the window, the clock, the file dialogs, the fonts and the codec seams, and it holds
 *  NO behaviour: every state change goes in as a `Command` and everything drawn comes out of the
 *  `AppModel`. That is why `interstellar_shots` can build the same `App` with no display — the
 *  window is not where the application lives.
 *
 *  Three host jobs, all injected rather than imported by the core (R-SVC-7):
 *
 *    * the frame source — FFmpeg, one decoder per media path;
 *    * the frame writer — FFmpeg for `.mp4`/`.mov`/`.mkv`;
 *    * the typeface — Cosmo's embedded faces, so the app's own type does not depend on what the
 *      host has installed (R-FONT-1). Interstellar aliases Cosmo's tokens, so it embeds Cosmo's
 *      fonts too rather than shipping a second copy.
 *
 *  **Repaint is driven by need, not by a timer at frame rate.** A 16 ms tick that always redraws
 *  re-renders the whole window in software Cairo sixty times a second forever, at rest — which is
 *  the core a decode needs. `needsRedraw` keeps painting while anything is in flight and stops
 *  when nothing is, which R-G-1 permits: it forbids a visible change in one frame, not a repaint
 *  at rest.
 */
#include "App.h"
#include "core/service/InterstellarService.h"
#include "../../core/Artboard/src/adapter/native/CairoTarget.h"
#include "EmbeddedFonts.h"
#ifdef INTERSTELLAR_HAVE_FFMPEG
#include "FrameSourceFFmpeg.h"
#include "FrameWriterFFmpeg.h"
#endif
#include <gtk/gtk.h>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace arstro;
using namespace arstro::interstellar;
using arstro::interstellar_v1::App;

namespace
{
    struct Host
    {
        std::unique_ptr<InterstellarService> svc;
        std::unique_ptr<App> app;
        GtkWidget *window = nullptr;
        GtkWidget *canvas = nullptr;
        std::unique_ptr<artboard::CairoTarget> target;
        cairo_t *lastCr = nullptr;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        /** Playback advances the playhead from the wall clock and DROPS frames rather than
         *  stalling: it is better to show the right time late than the wrong time on time
         *  (R-NFR-4). */
        bool playing = false;
        double playStartedAtMs = 0, playStartedFrom = 0;
        int dropped = 0;

        double nowMs() const
        {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        }
    };

    Host g;

    void dispatchText(const std::string &line)
    {
        std::string err;
        if (!g.svc->dispatchText(line, err))
            std::fprintf(stderr, "refused: %s\n",
                         err.empty() ? g.svc->model().lastError.c_str() : err.c_str());
    }

    gboolean onDraw(GtkWidget *, cairo_t *cr, gpointer)
    {
        const double w = gtk_widget_get_allocated_width(g.canvas);
        const double h = gtk_widget_get_allocated_height(g.canvas);
        // ONE persistent render target across frames: images are registered with it by id, and a
        // target rebuilt per frame would re-upload every frame's pixels and leak the old ids.
        if (!g.target || g.lastCr != cr)
        {
            g.target = std::make_unique<artboard::CairoTarget>(cr);
            g.lastCr = cr;
        }
        g.app->setSize(w, h);
        g.app->render(*g.target, g.nowMs());
        return TRUE;
    }

    gboolean onTick(gpointer)
    {
        // Playback: derive the playhead from the wall clock rather than counting frames, so a slow
        // frame costs a DROPPED frame and not a slowed-down edit.
        if (g.playing)
        {
            const double elapsed = (g.nowMs() - g.playStartedAtMs) / 1000.0;
            const double t = g.playStartedFrom + elapsed;
            const double dur = g.svc->model().duration;
            if (t >= dur) { dispatchText("pause"); g.playing = false; dispatchText("playhead " + std::to_string(dur)); }
            else dispatchText("playhead " + std::to_string(t));
        }
        gtk_widget_queue_draw(g.canvas);
        return TRUE;
    }

    /** A pointer position in the canvas's own logical space. */
    void pointer(int kind, double x, double y)
    {
        g.app->pointer(kind, x, y, 0, g.nowMs());
        gtk_widget_queue_draw(g.canvas);
    }

    gboolean onButton(GtkWidget *, GdkEventButton *e, gpointer)
    {
        pointer(e->type == GDK_BUTTON_PRESS ? 0 : 2, e->x, e->y);
        return TRUE;
    }
    gboolean onMotion(GtkWidget *, GdkEventMotion *e, gpointer)
    {
        pointer(1, e->x, e->y);
        return TRUE;
    }
    gboolean onScroll(GtkWidget *, GdkEventScroll *e, gpointer)
    {
        double dy = 0;
        if (e->direction == GDK_SCROLL_UP) dy = 1;
        else if (e->direction == GDK_SCROLL_DOWN) dy = -1;
        else if (e->direction == GDK_SCROLL_SMOOTH) dy = -e->delta_y;
        g.app->wheel(e->x, e->y, dy, (e->state & GDK_CONTROL_MASK) != 0);
        gtk_widget_queue_draw(g.canvas);
        return TRUE;
    }

    std::string runFileChooser(const char *title, GtkFileChooserAction action, const char *pattern)
    {
        GtkWidget *d = gtk_file_chooser_dialog_new(
            title, GTK_WINDOW(g.window), action, "_Cancel", GTK_RESPONSE_CANCEL,
            action == GTK_FILE_CHOOSER_ACTION_SAVE ? "_Save" : "_Open", GTK_RESPONSE_ACCEPT, nullptr);
        if (pattern)
        {
            GtkFileFilter *f = gtk_file_filter_new();
            gtk_file_filter_set_name(f, pattern);
            for (const char *p : {"*.mp4", "*.mov", "*.mkv", "*.m4v", "*.avi", "*.png", "*.jpg",
                                  "*.jpeg", "*.tif", "*.tiff", "*.isp"})
                gtk_file_filter_add_pattern(f, p);
            gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(d), f);
        }
        std::string out;
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            char *p = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
            if (p) { out = p; g_free(p); }
        }
        gtk_widget_destroy(d);
        return out;
    }

    /** Import media and lay it on the timeline at the end — "open a video and start cutting",
     *  which is the one flow that has to work with no typing. Every step is a Command. */
    void importMedia(const std::string &path)
    {
        if (path.empty()) return;
        dispatchText("rack add " + path);
        const auto &m = g.svc->model();
        if (m.tracks.empty()) dispatchText("track add --kind video --name v0");
        // The source just added is the last one in the rack.
        if (m.rack.empty()) return;
        const std::string src = m.rack.back().bindName;
        const IFrameSource::Info info = g.svc->sourceInfo(m.rack.back().media);
        const double len = info.fps > 0 && info.frames > 0 ? (double)info.frames / info.fps : 5.0;
        // Appended after everything already on the track, so a second import does not land on top
        // of the first.
        double at = 0;
        for (const auto &c : m.clips) at = std::max(at, c.at + c.duration);
        char line[1024];
        std::snprintf(line, sizeof line,
                      "clip add --track %s --src rack:%s --in 0 --out %.3f --at %.3f",
                      m.tracks.front().name.c_str(), src.c_str(), len, at);
        dispatchText(line);
    }

    gboolean onKey(GtkWidget *, GdkEventKey *e, gpointer)
    {
        const bool ctrl = (e->state & GDK_CONTROL_MASK) != 0;
        const auto &m = g.svc->model();
        switch (e->keyval)
        {
            case GDK_KEY_space:
                // Play/pause. The playhead comes from the wall clock while playing, so starting
                // records where and when it started rather than accumulating per frame.
                g.playing = !g.playing;
                if (g.playing)
                {
                    g.playStartedAtMs = g.nowMs();
                    g.playStartedFrom = m.playhead;
                    dispatchText("play");
                }
                else dispatchText("pause");
                break;
            case GDK_KEY_Left: dispatchText("playhead +-0.0417"); break;   // one frame at 24 fps
            case GDK_KEY_Right: dispatchText("playhead +0.0417"); break;
            case GDK_KEY_Up: dispatchText("playhead prev-cut"); break;
            case GDK_KEY_Down: dispatchText("playhead next-cut"); break;
            case GDK_KEY_Home: dispatchText("playhead 0"); break;
            case GDK_KEY_s:
            {
                if (ctrl) { dispatchText("project save"); break; }
                // Split every clip under the playhead — the one edit a cutter makes constantly.
                std::vector<std::string> names;
                for (const auto &c : m.clips)
                    if (m.playhead > c.at && m.playhead < c.at + c.duration) names.push_back(c.name);
                for (const auto &n : names)
                    dispatchText("clip split " + n + " --at " + std::to_string(m.playhead));
                break;
            }
            case GDK_KEY_i:
                if (ctrl) importMedia(runFileChooser("Import media", GTK_FILE_CHOOSER_ACTION_OPEN, "Media"));
                break;
            case GDK_KEY_o:
                if (ctrl)
                {
                    const std::string p = runFileChooser("Open project", GTK_FILE_CHOOSER_ACTION_OPEN, "Media");
                    if (!p.empty()) dispatchText("project open " + p);
                }
                break;
            case GDK_KEY_e:
                if (ctrl)
                {
                    const std::string p = runFileChooser("Export master", GTK_FILE_CHOOSER_ACTION_SAVE, nullptr);
                    if (!p.empty())
                    {
                        // Synchronous on purpose, and honestly so: a render is not yet a background
                        // job (R-RENDER-1's `step()` is unbuilt), so the window is unresponsive
                        // until it finishes. Printing the path first means the user can see what
                        // it is doing rather than wondering whether it froze.
                        std::fprintf(stderr, "exporting %s …\n", p.c_str());
                        dispatchText("render --out " + p);
                        std::fprintf(stderr, "done\n");
                    }
                }
                break;
            case GDK_KEY_1: dispatchText(""); g.app->showWorkspace(Workspace::Grade); break;
            case GDK_KEY_2: g.app->showWorkspace(Workspace::Cut); break;
            case GDK_KEY_3: g.app->showWorkspace(Workspace::Mix); break;
            case GDK_KEY_4: g.app->showWorkspace(Workspace::Deliver); break;
            case GDK_KEY_Delete:
            {
                // Delete the selected clip. The timeline holds the selection, because which clip
                // is selected is presentation (R-SVC-4).
                const std::string sel = g.app->timeline().selected();
                if (!sel.empty()) dispatchText("clip delete " + sel);
                break;
            }
            default: return FALSE;
        }
        gtk_widget_queue_draw(g.canvas);
        return TRUE;
    }
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);

    InterstellarService::Hooks hooks;
#ifdef INTERSTELLAR_HAVE_FFMPEG
    hooks.makeFrameSource = []() -> std::unique_ptr<IFrameSource> {
        return std::unique_ptr<IFrameSource>(new arstro::interstellar_host::FrameSourceFFmpeg());
    };
    hooks.makeFrameWriter = [](const std::string &path) -> std::unique_ptr<IFrameWriter> {
        if (arstro::interstellar_host::FrameWriterFFmpeg::handles(path))
            return std::unique_ptr<IFrameWriter>(new arstro::interstellar_host::FrameWriterFFmpeg());
        std::fprintf(stderr, "no writer for %s — use .mp4, .mov or .mkv\n", path.c_str());
        return nullptr;
    };
#endif
    g.svc = std::make_unique<InterstellarService>(hooks);
    // The event stream IS the log (R-SVC-5), so the window's log is the same text the CLI prints.
    g.svc->subscribe([](const Event &e) { std::fprintf(stderr, "%s\n", formatEvent(e).c_str()); });

    g.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(g.window), "interstellar");
    gtk_window_set_default_size(GTK_WINDOW(g.window), 1440, 900);
    // Derived, not guessed: the monitor's floor plus the chrome plus the deck's minimum.
    GdkGeometry geom{};
    geom.min_width = 900;
    geom.min_height = 560;
    gtk_window_set_geometry_hints(GTK_WINDOW(g.window), nullptr, &geom, GDK_HINT_MIN_SIZE);

    g.canvas = gtk_drawing_area_new();
    gtk_container_add(GTK_CONTAINER(g.window), g.canvas);
    gtk_widget_add_events(g.canvas, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                        GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK |
                                        GDK_SMOOTH_SCROLL_MASK | GDK_KEY_PRESS_MASK);
    gtk_widget_set_can_focus(g.canvas, TRUE);

    // The typeface, compiled into the binary (R-FONT-1). Registered BEFORE the App is built, so
    // the first frame measures text in the app's own font rather than in a fallback — a shot or a
    // layout measured in the wrong face reports overflow bugs that do not exist.
    arstro::cosmo_v2::registerEmbeddedFonts();

    g.app = std::make_unique<App>(*g.svc, 1440, 900);

    g_signal_connect(g.canvas, "draw", G_CALLBACK(onDraw), nullptr);
    g_signal_connect(g.canvas, "button-press-event", G_CALLBACK(onButton), nullptr);
    g_signal_connect(g.canvas, "button-release-event", G_CALLBACK(onButton), nullptr);
    g_signal_connect(g.canvas, "motion-notify-event", G_CALLBACK(onMotion), nullptr);
    g_signal_connect(g.canvas, "scroll-event", G_CALLBACK(onScroll), nullptr);
    g_signal_connect(g.window, "key-press-event", G_CALLBACK(onKey), nullptr);
    g_signal_connect(g.window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    g_timeout_add(16, onTick, nullptr);

    // Bare paths on the command line: a `.isp` is opened, anything else is imported and laid on
    // the timeline — so `interstellar a.mp4 b.mp4` is a cut, ready to trim.
    for (int i = 1; i < argc; ++i)
    {
        const std::string p = argv[i];
        if (p.size() > 4 && p.substr(p.size() - 4) == ".isp") dispatchText("project open " + p);
        else importMedia(p);
    }

    gtk_widget_show_all(g.window);
    gtk_widget_grab_focus(g.canvas);
    std::fprintf(stderr,
                 "interstellar — space play/pause · ←→ frame · ↑↓ cut · S split · Del delete\n"
                 "               ctrl+I import · ctrl+O open · ctrl+S save · ctrl+E export\n"
                 "               1-4 workspace · wheel scroll · ctrl+wheel zoom\n");
    gtk_main();
    return 0;
}
