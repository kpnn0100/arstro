/*
 *  interstellar — the GTK3 window (the host layer, architecture.md §1).
 *
 *  The ONLY file with OS code in the GUI. It owns the window, the frame clock, the native file
 *  dialogs, the codecs and the paths, and it binds the app's three hooks to the service:
 *
 *      model        → InterstellarService::model()
 *      dispatch     → InterstellarService::dispatchText()   — the same text the CLI sends (R-G-4)
 *      renderFrame  → InterstellarService::renderFrame()
 *      thumbnail    → a host-side cache of small decoded stills (app/AppHooks.h's optional hook)
 *
 *  The service is pumped on every frame tick whether or not anything needs drawing (a rack decode,
 *  playback and a render job all advance in `pump`); the window repaints only while the app says
 *  something is moving (`needsRedraw`), so a window at rest costs nothing.
 */
#include "App.h"
#include "EmbeddedFonts.h"
#include "AudioSourceFFmpeg.h"
#ifdef INTERSTELLAR_HAVE_PULSE
#include "AudioOutPulse.h"
#endif
#include "FrameWriterFFmpeg.h"
#include "HostFrameSource.h"
#include "InterstellarService.h"
#include "PngWriter.h"
#include "Thumbnailer.h"
#include "VideoFrameDecoder.h"
#include "../../core/Artboard/src/adapter/native/CairoTarget.h"
#include "anim/Motion.h"
#include "core/AppSettings.h"
#include "core/ThreadBudget.h"
#include <gtk/gtk.h>
#include <malloc.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <memory>
#include <functional>
#include <string>
#include <vector>

using namespace arstro;
using namespace arstro::interstellar;
using arstro::interstellar_v1::App;
using arstro::interstellar_v1::AppHooks;

namespace
{
    constexpr int kW = 1440, kH = 900;

    std::string dataDir()
    {
        if (const char *x = std::getenv("XDG_DATA_HOME")) return std::string(x) + "/interstellar";
        if (const char *h = std::getenv("HOME")) return std::string(h) + "/.local/share/interstellar";
        return std::string();
    }
    std::string configDir()
    {
        if (const char *x = std::getenv("XDG_CONFIG_HOME")) return std::string(x) + "/interstellar";
        if (const char *h = std::getenv("HOME")) return std::string(h) + "/.config/interstellar";
        return std::string();
    }
    std::string recentsPath()
    {
        if (const char *x = std::getenv("INTERSTELLAR_RECENTS")) return x;
        const std::string d = dataDir();
        return d.empty() ? d : d + "/recents";
    }

    InterstellarService::Host makeServiceHost()
    {
        InterstellarService::Host h;
        h.rackDecoder = [](std::shared_ptr<const FrameSelector> sel) {
            return std::unique_ptr<cosmo::IImageDecoder>(new interstellar_host::VideoFrameDecoder(std::move(sel)));
        };
        h.frameSource = [] { return std::unique_ptr<IFrameSource>(new interstellar_host::HostFrameSource()); };
        h.audioSource = [] { return std::unique_ptr<IAudioSource>(new interstellar_host::AudioSourceFFmpeg()); };
#ifdef INTERSTELLAR_HAVE_PULSE
        h.audioOut = [] { return std::unique_ptr<IAudioOut>(new interstellar_host::AudioOutPulse()); };   // R-AUD-6
#endif
        h.frameWriter = [] { return std::unique_ptr<IFrameWriter>(new interstellar_host::FrameWriterFFmpeg()); };
        h.writeImage = [](const std::string &p, const Raster &r, std::string &err) {
            return interstellar_host::writePng(p, r, err);
        };
        h.recentsPath = recentsPath();
        h.asyncPreview = true;   // the monitor renders on a worker; scrubbing never blocks the window (D-5)
        // Engine settings (CPU limit, threads, preview quality, GPU, screen scale) and the preset
        // library live where the desktop keeps an app's config and data.
        if (!configDir().empty()) h.settingsPath = configDir() + "/settings.txt";
        if (const char *x = std::getenv("INTERSTELLAR_PRESETS")) h.presetDir = x;
        else if (!dataDir().empty()) h.presetDir = dataDir() + "/presets";
        return h;
    }

    struct Host
    {
        // Declaration order is construction order: budget → service → view.
        cosmo::ThreadBudget budget{50};
        InterstellarService svc{budget, makeServiceHost()};
        interstellar_host::Thumbnailer thumbs;
        std::unique_ptr<App> app;
        artboard::CairoTarget target;
        GtkWidget *window = nullptr;
        GtkWidget *area = nullptr;
        gint64 startUs = 0;
        int minW = 0, minH = 0;   // the size request last given to the drawing area
    };

    double nowMs(const Host &a) { return a.startUs == 0 ? 0.0 : (g_get_monotonic_time() - a.startUs) / 1000.0; }
    int mapButton(guint b) { return b == 3 ? 2 : 0; }

    /** A rack source's media is stored relative to the project; the thumbnail hook gets it as the
     *  model spells it, so it is resolved here — paths are the host's business (R-SCOPE-3). */
    std::string resolveMedia(const Host &a, const std::string &p)
    {
        namespace fs = std::filesystem;
        if (p.empty() || fs::path(p).is_absolute()) return p;
        const std::string isp = a.svc.model().projectPath;
        return isp.empty() ? p : (fs::path(isp).parent_path() / p).lexically_normal().string();
    }

    void addFilter(GtkWidget *d, const char *name, std::initializer_list<const char *> patterns)
    {
        GtkFileFilter *f = gtk_file_filter_new();
        gtk_file_filter_set_name(f, name);
        for (const char *p : patterns) gtk_file_filter_add_pattern(f, p);
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(d), f);
    }

    void pickProjectToOpen(Host *a)
    {
        GtkWidget *d = gtk_file_chooser_dialog_new("Open project", GTK_WINDOW(a->window), GTK_FILE_CHOOSER_ACTION_OPEN,
                                                   "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, nullptr);
        addFilter(d, "Interstellar projects", {"*.isp"});
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
            if (path) { a->app->openProjectPicked(path); g_free(path); }
        }
        gtk_widget_destroy(d);
        gtk_widget_queue_draw(a->area);
    }

    void pickProjectToCreate(Host *a)
    {
        GtkWidget *d = gtk_file_chooser_dialog_new("New project", GTK_WINDOW(a->window), GTK_FILE_CHOOSER_ACTION_SAVE,
                                                   "_Cancel", GTK_RESPONSE_CANCEL, "_Create", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(d), TRUE);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d), "Untitled.isp");
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
            if (path)
            {
                std::string p = path;
                if (p.size() < 4 || p.compare(p.size() - 4, 4, ".isp") != 0) p += ".isp";
                a->app->newProjectPicked(p);
                g_free(path);
            }
        }
        gtk_widget_destroy(d);
        gtk_widget_queue_draw(a->area);
    }

    void pickSaveAs(Host *a)
    {
        GtkWidget *d = gtk_file_chooser_dialog_new("Save project as", GTK_WINDOW(a->window), GTK_FILE_CHOOSER_ACTION_SAVE,
                                                   "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(d), TRUE);
        const std::string cur = a->svc.model().projectPath;
        const auto slash = cur.find_last_of('/');
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d), cur.empty() ? "Untitled.isp" : cur.substr(slash + 1).c_str());
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
            if (path)
            {
                std::string p = path;
                if (p.size() < 4 || p.compare(p.size() - 4, 4, ".isp") != 0) p += ".isp";
                a->app->saveAsPicked(p);
                g_free(path);
            }
        }
        gtk_widget_destroy(d);
        gtk_widget_queue_draw(a->area);
    }

    void pickPresetToImport(Host *a)
    {
        GtkWidget *d = gtk_file_chooser_dialog_new("Import preset", GTK_WINDOW(a->window), GTK_FILE_CHOOSER_ACTION_OPEN,
                                                   "_Cancel", GTK_RESPONSE_CANCEL, "_Import", GTK_RESPONSE_ACCEPT, nullptr);
        addFilter(d, "Presets (.apf)", {"*.apf"});
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
            if (path) { a->app->presetImportPicked(path); g_free(path); }
        }
        gtk_widget_destroy(d);
        gtk_widget_queue_draw(a->area);
    }

    /** A file to read or to write, with the filter and the extension a save makes sure of. */
    void pickFile(Host *a, bool save, const char *title, const char *filterName, std::initializer_list<const char *> patterns,
                  const std::string &suggested, const std::string &ensureExt, const std::function<void(const std::string &)> &done)
    {
        GtkWidget *d = gtk_file_chooser_dialog_new(title, GTK_WINDOW(a->window), save ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN,
                                                   "_Cancel", GTK_RESPONSE_CANCEL, save ? "_Export" : "_Open", GTK_RESPONSE_ACCEPT, nullptr);
        addFilter(d, filterName, patterns);
        if (save)
        {
            gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(d), TRUE);
            gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d), suggested.c_str());
        }
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
            if (path)
            {
                std::string p = path;
                g_free(path);
                if (save && !ensureExt.empty() && std::filesystem::path(p).extension().empty()) p += ensureExt;
                gtk_widget_destroy(d);
                gtk_widget_queue_draw(a->area);
                done(p);
                return;
            }
        }
        gtk_widget_destroy(d);
        gtk_widget_queue_draw(a->area);
    }

    /** R-COLOR-5/6: a .cube to read, or one to write (the extension made sure). */
    void pickLut(Host *a, bool save, const std::string &suggested, const std::function<void(const std::string &)> &done)
    {
        GtkWidget *d = gtk_file_chooser_dialog_new(save ? "Export LUT" : "Choose a LUT", GTK_WINDOW(a->window),
                                                   save ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel",
                                                   GTK_RESPONSE_CANCEL, save ? "_Export" : "_Open", GTK_RESPONSE_ACCEPT, nullptr);
        addFilter(d, "LUTs (.cube)", {"*.cube", "*.CUBE"});
        if (save)
        {
            gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(d), TRUE);
            gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d), suggested.c_str());
        }
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
            if (path)
            {
                std::string p = path;
                g_free(path);
                if (save && (p.size() < 5 || (p.compare(p.size() - 5, 5, ".cube") != 0 && p.compare(p.size() - 5, 5, ".CUBE") != 0))) p += ".cube";
                gtk_widget_destroy(d);
                gtk_widget_queue_draw(a->area);
                done(p);
                return;
            }
        }
        gtk_widget_destroy(d);
        gtk_widget_queue_draw(a->area);
    }

    /** A save dialog for a .png; `answer` gets the path with the extension made sure. */
    void pickPngToSave(Host *a, const char *title, const char *button, const char *name,
                       void (App::*answer)(const std::string &))
    {
        GtkWidget *d = gtk_file_chooser_dialog_new(title, GTK_WINDOW(a->window), GTK_FILE_CHOOSER_ACTION_SAVE,
                                                   "_Cancel", GTK_RESPONSE_CANCEL, button, GTK_RESPONSE_ACCEPT, nullptr);
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(d), TRUE);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d), name);
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
            if (path)
            {
                std::string p = path;
                if (p.size() < 4 || p.compare(p.size() - 4, 4, ".png") != 0) p += ".png";
                ((*a->app).*answer)(p);
                g_free(path);
            }
        }
        gtk_widget_destroy(d);
        gtk_widget_queue_draw(a->area);
    }

    void pickStillToExport(Host *a) { pickPngToSave(a, "Export still", "_Export", "still.png", &App::stillExportPicked); }
    void pickFrameToSave(Host *a) { pickPngToSave(a, "Save frame", "_Save", "frame.png", &App::frameSavePicked); }

    /** Copy Frame (R-UI-11): what the monitor shows, at full resolution, onto the CLIPBOARD
     *  selection as an image — any image editor or chat pastes it. The clipboard keeps its own
     *  reference to the pixbuf, so the bytes outlive this call. */
    bool copyFrame(Host *a, const std::string &bind, std::string &err)
    {
        Raster r;
        if (!a->svc.captureFrame(bind, r) || r.empty())
        {
            err = bind.empty() ? "Nothing to copy: no frame under the playhead" : "Could not render " + bind + " to copy";
            return false;
        }
        GdkPixbuf *pix = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, r.width, r.height);
        if (!pix) { err = "Out of memory copying the frame"; return false; }
        const int stride = gdk_pixbuf_get_rowstride(pix);
        guchar *dst = gdk_pixbuf_get_pixels(pix);
        for (int y = 0; y < r.height; ++y)
            std::memcpy(dst + (size_t)y * stride, r.rgba.data() + (size_t)y * r.width * 4, (size_t)r.width * 4);
        gtk_clipboard_set_image(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), pix);
        g_object_unref(pix);
        return true;
    }

    void pickFootage(Host *a)
    {
        GtkWidget *d = gtk_file_chooser_dialog_new("Add footage", GTK_WINDOW(a->window), GTK_FILE_CHOOSER_ACTION_OPEN,
                                                   "_Cancel", GTK_RESPONSE_CANCEL, "_Add", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(d), TRUE);
        addFilter(d, "Video and stills", {"*.mp4", "*.mov", "*.mkv", "*.m4v", "*.mxf", "*.avi", "*.webm", "*.MP4",
                                          "*.MOV", "*.jpg", "*.jpeg", "*.png", "*.tif", "*.tiff", "*.JPG", "*.PNG"});
        addFilter(d, "All files", {"*"});
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            std::vector<std::string> paths;
            GSList *files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(d));
            for (GSList *f = files; f; f = f->next)
            {
                paths.emplace_back(static_cast<char *>(f->data));
                g_free(f->data);
            }
            g_slist_free(files);
            if (!paths.empty()) a->app->footagePicked(paths);
        }
        gtk_widget_destroy(d);
        gtk_widget_queue_draw(a->area);
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
        const double now = nowMs(*a);
        // The service never depends on the view wanting to draw: rack loads, playback and render
        // jobs advance here every tick (R-SVC-1).
        a->svc.pump(now);
        a->app->setHomeClock((long long)std::time(nullptr));
        // The window minimum follows the TARGET screen scale (cosmo R-SCALE): a scale the window
        // is too small for grows the window rather than cropping the shell.
        const int minW = (int)std::ceil(a->app->minPhysicalWidth()), minH = (int)std::ceil(a->app->minPhysicalHeight());
        if (minW != a->minW || minH != a->minH)
        {
            a->minW = minW;
            a->minH = minH;
            gtk_widget_set_size_request(a->area, minW, minH);
        }
        if (a->app->needsRedraw(now)) gtk_widget_queue_draw(a->area);
        return G_SOURCE_CONTINUE;
    }

    gboolean onButton(GtkWidget *w, GdkEventButton *e, gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        if (e->type == GDK_2BUTTON_PRESS || e->type == GDK_3BUTTON_PRESS) return TRUE;   // the app counts its own clicks
        gtk_widget_grab_focus(w);
        a->app->pointer(e->type == GDK_BUTTON_PRESS ? 0 : 2, e->x, e->y, mapButton(e->button), nowMs(*a),
                        (e->state & GDK_MOD1_MASK) != 0, (e->state & GDK_SHIFT_MASK) != 0,
                        (e->state & GDK_CONTROL_MASK) != 0);
        gtk_widget_queue_draw(a->area);
        return TRUE;
    }

    gboolean onMotion(GtkWidget *, GdkEventMotion *e, gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        a->app->pointer(1, e->x, e->y, 0, nowMs(*a), (e->state & GDK_MOD1_MASK) != 0,
                        (e->state & GDK_SHIFT_MASK) != 0, (e->state & GDK_CONTROL_MASK) != 0);
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

    void onSizeAllocate(GtkWidget *, GtkAllocation *alloc, gpointer user)
    {
        static_cast<Host *>(user)->app->setSize(alloc->width, alloc->height);
    }

    /** R-EDT-2: a release, for the keys whose HOLD means something (K: J and L step a frame). */
    gboolean onKeyUp(GtkWidget *, GdkEventKey *e, gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        if (e->keyval != GDK_KEY_k && e->keyval != GDK_KEY_K) return FALSE;
        artboard::KeyEvent ke;
        ke.type = artboard::KeyEvent::Type::Up;
        ke.keyCode = 'K';
        a->app->key(ke);
        gtk_widget_queue_draw(a->area);
        return TRUE;
    }

    gboolean onKey(GtkWidget *, GdkEventKey *e, gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        artboard::KeyEvent ke;
        ke.shift = (e->state & GDK_SHIFT_MASK) != 0;
        ke.ctrl = (e->state & GDK_CONTROL_MASK) != 0;
        ke.alt = (e->state & GDK_MOD1_MASK) != 0;
        bool handled = false;
        // A printable character is offered as TEXT first (a focused name field), then as a key.
        if (!ke.ctrl && !ke.alt)
        {
            const gunichar u = gdk_keyval_to_unicode(e->keyval);
            if (u > 0x20 && u != 0x7f)
            {
                char buf[8] = {0};
                const int n = g_unichar_to_utf8(u, buf);
                artboard::KeyEvent te = ke;
                te.type = artboard::KeyEvent::Type::Text;
                te.text = std::string(buf, (size_t)n);
                handled = a->app->key(te);
            }
        }
        if (!handled)
        {
            int code = 0;
            switch (e->keyval)
            {
                case GDK_KEY_BackSpace: code = 8; break;
                case GDK_KEY_Return: case GDK_KEY_KP_Enter: code = 13; break;
                case GDK_KEY_Escape: code = 27; break;
                case GDK_KEY_space: code = 32; break;
                case GDK_KEY_Left: code = 37; break;
                case GDK_KEY_Up: code = 38; break;
                case GDK_KEY_Right: code = 39; break;
                case GDK_KEY_Down: code = 40; break;
                case GDK_KEY_Delete: code = 46; break;
                case GDK_KEY_comma: code = 188; break;    // R-EDT-1: Insert (the OEM code; 44 is not a key here)
                case GDK_KEY_period: code = 190; break;   // Overwrite (46 is Delete)
                default:
                {
                    const gunichar u = gdk_keyval_to_unicode(gdk_keyval_to_upper(e->keyval));
                    if (u >= '0' && u <= 'Z') code = (int)u;   // the app's shortcuts use upper-case codes
                }
            }
            if (code)
            {
                ke.type = artboard::KeyEvent::Type::Down;
                ke.keyCode = code;
                handled = a->app->key(ke);
            }
        }
        gtk_widget_queue_draw(a->area);
        return handled ? TRUE : FALSE;
    }
}

int main(int argc, char **argv)
{
    // A 1080p grade allocates ~33 MB float buffers per frame; with glibc's defaults each is
    // returned to the OS and faulted back in on the next frame (measured: 34 ms → 23.4 ms per
    // full-res grade with the thresholds pinned — render/NOTES.md).
    mallopt(M_MMAP_THRESHOLD, 512 * 1024 * 1024);
    mallopt(M_TRIM_THRESHOLD, 1024 * 1024 * 1024);

    gtk_init(&argc, &argv);
    // The typeface is in the binary, never resolved from the machine (arstro.design.rule §2.3).
    arstro::cosmo_v2::registerEmbeddedFonts();
    // Reduced motion: the OS's animation setting, read once, honoured by every tween (§2.6).
    gboolean animations = TRUE;
    g_object_get(gtk_settings_get_default(), "gtk-enable-animations", &animations, nullptr);
    artboard::setReducedMotion(!animations);

    auto host = std::make_unique<Host>();
    Host *a = host.get();
    AppHooks hooks;
    hooks.model = [a]() -> const AppModel & { return a->svc.model(); };
    hooks.dispatch = [a](const std::string &line, std::string &err) { return a->svc.dispatchText(line, err); };
    hooks.renderFrame = [a](double t, int edge, Raster &out) { return a->svc.renderFrame(t, edge, out); };
    hooks.thumbnail = [a](const std::string &media, double t, int edge, Raster &out) {
        return a->thumbs.get(resolveMedia(*a, media), t, edge, out);
    };
    // Stills decode on the Thumbnailer's worker; the app re-asks when this moves (D-5, D-6).
    hooks.thumbnailEpoch = [a]() { return a->thumbs.epoch(); };
    // Grade's monitor and the ref-frame slider's preview: one source, graded (R-UI-3, R-RACK-3).
    hooks.audioPeaks = [a](const std::string &media, std::vector<float> &peaks, double &per) { return a->svc.audioPeaks(media, peaks, per); };
    hooks.renderSource = [a](const std::string &bind, double t, int edge, Raster &out) {
        return a->svc.renderSourceFrame(bind, t, edge, out);
    };
    hooks.copyFrame = [a](const std::string &bind, std::string &err) { return copyFrame(a, bind, err); };
    a->app = std::make_unique<App>(hooks, (double)kW, (double)kH);
    a->app->onPickProjectToOpen = [a] { pickProjectToOpen(a); };
    a->app->onPickProjectToCreate = [a] { pickProjectToCreate(a); };
    a->app->onPickFootage = [a] { pickFootage(a); };
    a->app->onPickSaveAs = [a] { pickSaveAs(a); };
    a->app->onPickPresetToImport = [a] { pickPresetToImport(a); };
    a->app->onPickLutToOpen = [a](std::function<void(const std::string &)> done) { pickLut(a, false, std::string(), done); };
    a->app->onPickLutToSave = [a](const std::string &name, std::function<void(const std::string &)> done) { pickLut(a, true, name, done); };
    a->app->onPickTimelineToImport = [a](std::function<void(const std::string &)> done) {
        pickFile(a, false, "Import timeline", "Timelines (EDL, FCPXML, OTIO)", {"*.edl", "*.EDL", "*.fcpxml", "*.xml", "*.otio"}, std::string(), std::string(), done);
    };
    a->app->onPickTimelineToExport = [a](const std::string &name, std::function<void(const std::string &)> done) {
        pickFile(a, true, "Export timeline", "Timelines (EDL, FCPXML, OTIO)", {"*.edl", "*.fcpxml", "*.otio"}, name, ".fcpxml", done);
    };
    a->app->onPickStillToExport = [a] { pickStillToExport(a); };
    a->app->onPickFrameToSave = [a] { pickFrameToSave(a); };
    // The largest screen scale this display can give a window for (cosmo R-SCALE-3): larger ones
    // are drawn disabled in the settings dialog.
    {
        GdkRectangle wa{0, 0, 0, 0};
        if (GdkMonitor *mon = gdk_display_get_primary_monitor(gdk_display_get_default())) gdk_monitor_get_workarea(mon, &wa);
        int maxScale = 100;
        for (int sc : arstro::cosmo::AppSettings::uiScales())
            if (wa.width <= 0 || (App::minWidth() * sc / 100.0 <= wa.width && App::minHeight() * sc / 100.0 <= wa.height)) maxScale = sc;
        a->app->setMaxUiScale(maxScale);
    }

    a->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(a->window), "Interstellar");
    gtk_window_set_default_size(GTK_WINDOW(a->window), kW, kH);
    g_signal_connect(a->window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);

    a->area = gtk_drawing_area_new();
    a->minW = (int)std::ceil(a->app->minPhysicalWidth());
    a->minH = (int)std::ceil(a->app->minPhysicalHeight());
    gtk_widget_set_size_request(a->area, a->minW, a->minH);
    gtk_widget_set_can_focus(a->area, TRUE);
    gtk_widget_add_events(a->area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK |
                                       GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
    g_signal_connect(a->area, "draw", G_CALLBACK(onDraw), a);
    g_signal_connect(a->area, "button-press-event", G_CALLBACK(onButton), a);
    g_signal_connect(a->area, "button-release-event", G_CALLBACK(onButton), a);
    g_signal_connect(a->area, "motion-notify-event", G_CALLBACK(onMotion), a);
    g_signal_connect(a->area, "scroll-event", G_CALLBACK(onScroll), a);
    g_signal_connect(a->area, "size-allocate", G_CALLBACK(onSizeAllocate), a);
    g_signal_connect(a->area, "key-press-event", G_CALLBACK(onKey), a);
    g_signal_connect(a->area, "key-release-event", G_CALLBACK(onKeyUp), a);
    gtk_container_add(GTK_CONTAINER(a->window), a->area);

    // A project path on the command line opens it directly — the same command the Home card sends.
    if (argc > 1)
    {
        std::string err;
        if (!a->svc.dispatchText("project open \"" + std::string(argv[1]) + "\"", err))
            g_printerr("interstellar: %s\n", err.c_str());
    }

    a->startUs = g_get_monotonic_time();
    gtk_widget_add_tick_callback(a->area, onTick, a, nullptr);
    gtk_widget_show_all(a->window);
    gtk_widget_grab_focus(a->area);
    gtk_main();
    return 0;
}
