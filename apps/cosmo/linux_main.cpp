/*
 *  Native Linux host for cosmo_v2 by arstro: GTK3 drawing area -> Cairo
 *  (Artboard's CairoTarget). Mirrors cosmo/linux_main.cpp's GTK glue; the only
 *  addition is registering the vendored DM Sans / JetBrains Mono files with
 *  Fontconfig at startup (App-private, no system install) so the drawText
 *  font-family parameter (Artboard FR-22) resolves to them.
 */
#include "App.h"
#include "ExportWriter.h"
#include "Log.h"
#include "OmpPin.h"
#include "core/service/CosmoService.h"
#include "core/service/Event.h"
#include "core/ThreadBudget.h"
#include "widgets/SplashScreen.h"
#include "core/decode/NativeImageDecoder.h"
#include "../../core/Artboard/src/adapter/native/CairoTarget.h"
#include <fontconfig/fontconfig.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using arstro::cosmo_v2::App;
using arstro::cosmo::NativeImageDecoder;
using arstro::cosmo::DecodedImage;

namespace
{
    constexpr int kW = 1440, kH = 900;  // Figma base artboard size

    // ── Project loading (R-LOADING + R-LOADPERF) ──────────────────────────────
    //
    // The pool, the decode, the strictly-in-order hand-off, the back-pressure and the
    // worker count now live in cosmo_core::ProjectLoader (R-SVC-1) — including the
    // constants that used to sit here, which are ProjectLoader::kDecodeWindow /
    // kMaxInFlightBytes and ThreadBudget::kMaxDecodeWorkers. What is left in the host is
    // the GTK main thread's consumer: apply each finished result to the session in order
    // and drive the animated loading screen (R-LOADING).

    // The load has no host-side object any more (R-SVC-1): CosmoService owns the pool, the
    // decode, the tree and the state, and the host owns only the ANIMATION. What used to be
    // LoadJob's flags are the two below on Host, because they are facts about the transition
    // rather than about the load — "has the cover been chosen" and "has the editor been
    // revealed" mean nothing to a CLI running the same project.

    // R-EXPORT-6 / R2: a batch export runs on its OWN thread, exactly like the
    // project loader above. It has to: EditSession::exportFullResSlot() renders at full
    // resolution through RenderService::renderFull(), which BLOCKS its caller until the
    // engine's worker finishes — doing that on the UI thread freezes every frame for the
    // length of each image, which is what made the collapse animation stutter. Here the
    // worker blocks instead, and the UI thread only drains finished results, so the
    // dialog animates at full framerate from the first frame to the last.
    //
    // Safe because the dialog is modal for the batch's duration: nothing can submit an
    // edit, and App::render() skips its own full-render path while
    // App::exportInProgress() (RenderService's full-render channel holds one request at
    // a time). The worker never touches GTK — it only renders, encodes and writes, and
    // hands plain results back for the UI thread to report.
    struct ExportJob
    {
        struct Result
        {
            std::string name;      // display name of the image just written
            std::string path;      // where it landed
            bool ok = false;
            std::string error;     // why not, when !ok
        };

        App::ExportRequest req;
        std::mutex mu;                         // guards `ready`
        std::vector<Result> ready;             // produced results, in order
        std::atomic<bool> producedAll{false};
        std::atomic<bool> stop{false};
        size_t consumed = 0;                   // main-thread reported count
        int failures = 0;
        std::thread worker;
        guint pollId = 0;

        ~ExportJob()
        {
            stop.store(true);
            if (worker.joinable()) worker.join();
            if (pollId) g_source_remove(pollId);
        }
    };

    struct Host
    {
        GtkWidget *window = nullptr;
        GtkWidget *area = nullptr;
        App app{(double)kW, (double)kH};
        artboard::CairoTarget target;
        NativeImageDecoder decoder;
        gint64 startUs = 0;
        // R-SVC-1: the application lives here, not in App and not in this file. Constructed
        // once main() has an App to borrow the session from; every project open, edit,
        // selection and export goes through dispatch().
        std::unique_ptr<arstro::cosmo::CosmoService> svc;
        bool loadCoverSent = false;   // transition state, not load state (see above)
        bool loadRevealed = false;
        std::map<std::string, DecodedImage> thumbs;  // decoded cover thumbnails, keyed by image path
        std::unique_ptr<ExportJob> exportJob;        // active batch export (nullptr when idle, R-EXPORT-6)
        // The in-force preferences, mirrored here because the load runs in the host.
        arstro::cosmo::AppSettings settings;
        // R-SVC-10: the ONE owner of the CPU budget. The decode pool and the engine's
        // thread count are both slices of `budget.total()`, so they can no longer each
        // take the whole percentage and add up to twice it (D-11).
        arstro::cosmo::ThreadBudget budget;

        // ── R-SPLASH: the application-open animation, in its OWN borderless window ──
        GtkWidget *splashWindow = nullptr;
        GtkWidget *splashArea = nullptr;
        std::shared_ptr<arstro::cosmo_v2::SplashScreen> splash;
        artboard::CairoTarget splashTarget;
        // While true, cover-thumbnail requests are QUEUED instead of decoded, so the
        // intro plays against an idle main loop and the work happens after it
        // (R-SPLASH-3) -- the same "animation first" split the project open uses.
        bool startupPhase = false;
        std::vector<std::pair<int, std::string>> thumbQueue;
        size_t thumbDone = 0;
        bool startupWorkBegun = false;
    };

    double nowMs(const Host &a) { return a.startUs == 0 ? 0.0 : (g_get_monotonic_time() - a.startUs) / 1000.0; }
    int mapButton(guint b) { return b == 3 ? 2 : 0; }

    std::string baseName(const std::string &p)
    {
        auto s = p.find_last_of("/\\");
        return s == std::string::npos ? p : p.substr(s + 1);
    }
    bool endsWith(const std::string &s, const std::string &suf)
    {
        return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
    }
    // Case-insensitive suffix check, for sniffing a user-typed export extension
    // (".PNG"/".png"/".Jpg" should all be recognized the same way).
    bool endsWithCI(const std::string &s, const std::string &suf)
    {
        if (s.size() < suf.size()) return false;
        return std::equal(suf.rbegin(), suf.rend(), s.rbegin(), [](char a, char b) {
            return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
        });
    }

    // Box-average downscale of a decoded cover to a small thumbnail (long edge <=
    // maxEdge). Used for both the home cards and the loading-screen centre image, so
    // covers cost little memory and the loading thumbnail is available with no I/O.
    DecodedImage downscaleCover(const DecodedImage &src, int maxEdge)
    {
        if (!src.ok() || (src.width <= maxEdge && src.height <= maxEdge)) return src;
        const double sc = (double)maxEdge / std::max(src.width, src.height);
        const int nw = std::max(1, (int)(src.width * sc)), nh = std::max(1, (int)(src.height * sc));
        DecodedImage out; out.width = nw; out.height = nh; out.rgba.resize((size_t)nw * nh * 4);
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x)
            {
                const int sx0 = (int)(x / sc), sx1 = std::min(src.width, (int)((x + 1) / sc) + 1);
                const int sy0 = (int)(y / sc), sy1 = std::min(src.height, (int)((y + 1) / sc) + 1);
                unsigned long r = 0, g = 0, b = 0, a = 0, n = 0;
                for (int sy = sy0; sy < sy1; ++sy)
                    for (int sx = sx0; sx < sx1; ++sx)
                    {
                        const uint8_t *p = &src.rgba[((size_t)sy * src.width + sx) * 4];
                        r += p[0]; g += p[1]; b += p[2]; a += p[3]; ++n;
                    }
                uint8_t *o = &out.rgba[((size_t)y * nw + x) * 4];
                if (n) { o[0] = (uint8_t)(r / n); o[1] = (uint8_t)(g / n); o[2] = (uint8_t)(b / n); o[3] = (uint8_t)(a / n); }
                else { o[0] = o[1] = o[2] = 0; o[3] = 255; }
            }
        return out;
    }

    std::string exeDir()
    {
        char buf[4096];
#ifdef _WIN32
        DWORD n = GetModuleFileNameA(nullptr, buf, sizeof(buf) - 1);
        if (n == 0 || n >= sizeof(buf) - 1) return ".";
        buf[n] = '\0';
#else
        ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n <= 0) return ".";
        buf[n] = '\0';
#endif
        std::string exe(buf);
        auto s = exe.find_last_of("/\\");  // Windows paths use '\\'
        return s == std::string::npos ? std::string(".") : exe.substr(0, s);
    }

    // Registers a vendored font file as an app-private font (no system install,
    // scoped to this process's Fontconfig config) so cosmo_v2's Theme can select
    // it by family name via the drawText fontFamily parameter.
    void registerFont(const std::string &path)
    {
        if (!FcConfigAppFontAddFile(FcConfigGetCurrent(), (const FcChar8 *)path.c_str()))
            g_printerr("cosmo_v2: could not register font %s\n", path.c_str());
    }

    // Route every g_print/g_printerr through the file logger (chomping the
    // trailing newline the format strings add), so all of the app's console
    // diagnostics land in the log file too — no per-call-site changes needed.
    std::string chomp(const char *s)
    {
        std::string t = s ? s : "";
        while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) t.pop_back();
        return t;
    }
    void logPrintHandler(const gchar *s) { arstro::cosmo_v2::log::write(arstro::cosmo_v2::log::Level::Info, chomp(s)); }
    void logPrinterrHandler(const gchar *s) { arstro::cosmo_v2::log::write(arstro::cosmo_v2::log::Level::Error, chomp(s)); }

    void registerBundledFonts()
    {
        // COSMO_SOURCE_DIR (baked in by CMakeLists.txt) locates assets next to
        // the source tree, independent of the build directory or CWD.
        const std::string dir = std::string(COSMO_SOURCE_DIR) + "/assets/fonts";
        registerFont(dir + "/DMSans/DMSans-Regular.ttf");
        registerFont(dir + "/DMSans/DMSans-Medium.ttf");
        registerFont(dir + "/DMSans/DMSans-SemiBold.ttf");
        registerFont(dir + "/JetBrainsMono/JetBrainsMono-Regular.ttf");
        registerFont(dir + "/JetBrainsMono/JetBrainsMono-Medium.ttf");
    }

    void openImageFile(Host *a, const std::string &path)
    {
        DecodedImage img = a->decoder.decodeFile(path);
        if (img.ok())
            a->app.openImage(img.rgba.data(), img.width, img.height, baseName(path), path);
        else
            g_printerr("cosmo_v2: could not decode %s%s\n", path.c_str(),
                       (NativeImageDecoder::isRawExtension(path) && !NativeImageDecoder::rawSupported())
                           ? " (RAW needs a LibRaw build)" : "");
    }

    std::string stemLower(const std::string &p)
    {
        std::string b = baseName(p);
        auto dot = b.find_last_of('.');
        if (dot != std::string::npos) b = b.substr(0, dot);
        for (char &c : b) c = (char)std::tolower((unsigned char)c);
        return b;
    }

    void openPath(Host *a, const std::string &path);  // fwd
    void openPaths(Host *a, const std::vector<std::string> &paths)
    {
        for (const std::string &p : paths)
        {
            if (!NativeImageDecoder::isRawExtension(p))
            {
                bool rawDup = false;
                for (const std::string &q : paths)
                    if (&q != &p && NativeImageDecoder::isRawExtension(q) && stemLower(q) == stemLower(p))
                    { rawDup = true; break; }
                if (rawDup) { g_print("cosmo_v2: skipping %s (RAW with same name preferred)\n", p.c_str()); continue; }
            }
            openPath(a, p);
        }
    }

    void openPath(Host *a, const std::string &path)
    {
        if (endsWith(path, ".cosmo"))
        {
            std::string imgPath;
            arstro::EditParams params;
            if (App::readSessionFile(path, imgPath, params) && !imgPath.empty())
            {
                DecodedImage img = a->decoder.decodeFile(imgPath);
                if (img.ok())
                {
                    a->app.openImage(img.rgba.data(), img.width, img.height, baseName(imgPath), imgPath);
                    a->app.applyParams(params);
                }
                else
                    g_printerr("cosmo_v2: session image not found: %s\n", imgPath.c_str());
            }
            return;
        }
        openImageFile(a, path);
    }

    void saveSessionDialog(Host *a)
    {
        if (a->app.imageCount() == 0) return;
        GtkWidget *d = gtk_file_chooser_dialog_new(
            "Save session", GTK_WINDOW(a->window), GTK_FILE_CHOOSER_ACTION_SAVE,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(d), TRUE);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d), "edit.cosmo");
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
            if (a->app.saveSessionAs(path)) g_print("cosmo_v2: saved session %s\n", path);
            g_free(path);
        }
        gtk_widget_destroy(d);
        gtk_widget_queue_draw(a->area);
    }

    void saveWorkspaceDialog(Host *a)
    {
        if (a->app.imageCount() == 0) return;
        GtkWidget *d = gtk_file_chooser_dialog_new(
            "Save workspace", GTK_WINDOW(a->window), GTK_FILE_CHOOSER_ACTION_SAVE,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(d), TRUE);
        const std::string cur = a->app.currentWorkspacePath();
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d), cur.empty() ? "workspace.cosmoproj" : baseName(cur).c_str());
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
            std::string p = path ? path : "";
            if (!p.empty() && !endsWith(p, ".cosmoproj")) p += ".cosmoproj";
            if (a->app.saveWorkspaceAs(p)) g_print("cosmo_v2: saved workspace %s\n", p.c_str());
            g_free(path);
        }
        gtk_widget_destroy(d);
        gtk_widget_queue_draw(a->area);
    }

    void loadWorkspaceFile(Host *a, const std::string &path)
    {
        std::vector<App::WorkspaceEntry> entries;
        if (!App::readWorkspaceFile(path, entries))
        {
            g_printerr("cosmo_v2: could not read workspace %s\n", path.c_str());
            return;
        }
        a->app.resetWorkspace();
        for (const auto &e : entries)
        {
            const int parentNode = e.parent < 0 ? 0 : e.parent + 1;
            if (e.group)
            {
                a->app.addWorkspaceGroup(parentNode, e.name, e.params, e.history, e.bypass);
                continue;
            }
            DecodedImage img = a->decoder.decodeFile(e.imagePath);
            if (img.ok())
            {
                const int slot = a->app.openImageInto(parentNode, img.rgba.data(), img.width, img.height,
                                                       baseName(e.imagePath), e.imagePath);
                a->app.applyParamsToSlot(slot, e.params, e.history);
                a->app.setSlotBypass(slot, e.bypass);   // R-BYPASS-6
            }
            else
            {
                g_printerr("cosmo_v2: workspace image missing: %s\n", e.imagePath.c_str());
                a->app.addWorkspaceMissingImage(parentNode, baseName(e.imagePath));
            }
        }
        a->app.finishWorkspaceLoad(path);
    }

    void loadWorkspaceDialog(Host *a)
    {
        if (a->app.imageCount() > 0)
        {
            GtkWidget *confirm = gtk_message_dialog_new(
                GTK_WINDOW(a->window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK_CANCEL,
                "Loading a workspace replaces every open image and its edits. Continue?");
            const int resp = gtk_dialog_run(GTK_DIALOG(confirm));
            gtk_widget_destroy(confirm);
            if (resp != GTK_RESPONSE_OK) return;
        }
        GtkWidget *d = gtk_file_chooser_dialog_new(
            "Load workspace", GTK_WINDOW(a->window), GTK_FILE_CHOOSER_ACTION_OPEN,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, nullptr);
        GtkFileFilter *filt = gtk_file_filter_new();
        gtk_file_filter_set_name(filt, "Cosmo workspace (*.cosmoproj)");
        gtk_file_filter_add_pattern(filt, "*.cosmoproj");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(d), filt);
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
            if (path) loadWorkspaceFile(a, path);
            g_free(path);
        }
        gtk_widget_destroy(d);
        gtk_widget_queue_draw(a->area);
    }

    void renameGroupDialog(Host *a)
    {
        GtkWidget *d = gtk_dialog_new_with_buttons(
            "Rename group", GTK_WINDOW(a->window), GTK_DIALOG_MODAL,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Rename", GTK_RESPONSE_ACCEPT, nullptr);
        GtkWidget *entry = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "group name");
        gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
        gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_ACCEPT);
        gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(d))), entry);
        gtk_widget_show_all(d);
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            const char *name = gtk_entry_get_text(GTK_ENTRY(entry));
            if (name && *name) a->app.renameGroup(name);
        }
        gtk_widget_destroy(d);
        gtk_widget_queue_draw(a->area);
    }

    void savePresetDialog(Host *a)
    {
        if (a->app.imageCount() == 0) return;
        GtkWidget *d = gtk_dialog_new_with_buttons(
            "Save preset", GTK_WINDOW(a->window), GTK_DIALOG_MODAL,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
        GtkWidget *entry = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "preset name");
        gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
        gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_ACCEPT);
        gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(d))), entry);
        gtk_widget_show_all(d);
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            const char *name = gtk_entry_get_text(GTK_ENTRY(entry));
            if (name && *name && a->app.savePreset(name)) g_print("cosmo_v2: saved preset '%s'\n", name);
        }
        gtk_widget_destroy(d);
        gtk_widget_queue_draw(a->area);
    }

    void exportPresetDialog(Host *a)
    {
        if (a->app.imageCount() == 0) return;
        GtkWidget *d = gtk_file_chooser_dialog_new(
            "Export preset", GTK_WINDOW(a->window), GTK_FILE_CHOOSER_ACTION_SAVE,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Export", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(d), TRUE);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d), "preset.apf");
        GtkFileFilter *filt = gtk_file_filter_new();
        gtk_file_filter_set_name(filt, "Arstro preset (*.apf)");
        gtk_file_filter_add_pattern(filt, "*.apf");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(d), filt);
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
            std::string p = path ? path : "";
            if (!p.empty() && !endsWith(p, ".apf")) p += ".apf";
            if (a->app.exportPresetTo(p)) g_print("cosmo_v2: exported preset %s\n", p.c_str());
            g_free(path);
        }
        gtk_widget_destroy(d);
        gtk_widget_queue_draw(a->area);
    }

    void importPresetDialog(Host *a)
    {
        GtkWidget *d = gtk_file_chooser_dialog_new(
            "Import preset", GTK_WINDOW(a->window), GTK_FILE_CHOOSER_ACTION_OPEN,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, nullptr);
        GtkFileFilter *filt = gtk_file_filter_new();
        gtk_file_filter_set_name(filt, "Arstro preset (*.apf)");
        gtk_file_filter_add_pattern(filt, "*.apf");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(d), filt);
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
            if (path && !a->app.importPresetFrom(path))
                g_printerr("cosmo_v2: could not import preset %s (bad file or different engine)\n", path);
            g_free(path);
        }
        gtk_widget_destroy(d);
        gtk_widget_queue_draw(a->area);
    }

    void openDialog(Host *a)
    {
        GtkWidget *d = gtk_file_chooser_dialog_new(
            "Open image", GTK_WINDOW(a->window), GTK_FILE_CHOOSER_ACTION_OPEN,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(d), TRUE);
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            GSList *files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(d));
            std::vector<std::string> paths;
            for (GSList *it = files; it; it = it->next) { paths.emplace_back((const char *)it->data); g_free(it->data); }
            g_slist_free(files);
            openPaths(a, paths);
        }
        gtk_widget_destroy(d);
        gtk_widget_queue_draw(a->area);
    }

    // ── Projects (.cmp) — a .cmp IS a workspace catalog referencing images on disk
    //    (R-HOME-2); New/Open/Import reuse the workspace machinery above. ──
    std::string ensureCmp(const std::string &p) { return endsWith(p, ".cmp") ? p : p + ".cmp"; }

    // Read a just-created/opened .cmp back and record it in the recent index.
    void rememberProject(const std::string &cmpPath)
    {
        std::vector<App::WorkspaceEntry> entries;
        App::readWorkspaceFile(cmpPath, entries);
        arstro::cosmo::RecentEntry e;
        e.name = std::filesystem::path(cmpPath).stem().string();
        e.path = cmpPath;
        long long size = 0; int count = 0;
        for (const auto &en : entries)
            if (!en.group && !en.imagePath.empty())
            {
                if (e.firstImagePath.empty()) e.firstImagePath = en.imagePath;
                ++count;
                std::error_code ec;
                const auto s = std::filesystem::file_size(en.imagePath, ec);
                if (!ec) size += (long long)s;
            }
        e.photoCount = count;
        e.sizeBytes = size;
        e.lastOpened = (long long)std::time(nullptr);
        arstro::cosmo::ProjectStore::remember(std::move(e));
    }

    // The decode producer used to live here. It is now ProjectLoader::produce in
    // cosmo_core (R-SVC-1), which is what makes a load runnable — and measurable —
    // without a window; what remains below is the host's half, applying results to App.

    // Main-thread consumer (GTK timeout): apply any decoded results in order, feed
    // the loading cover + progress, and on completion finish + reveal. Runs while
    // the worker decodes, so the loading screen animates at full frame rate.
    // ── The service's events, translated into animation (R-SVC-4) ────────────────────
    //
    // This is the entire host side of a project load now. The service decodes, attaches and
    // owns the state (R-SVC-1); everything below turns "what changed" into "what the window
    // does about it". No decisions are taken here — the only judgement left is WHICH pixels
    // make the loading-screen cover, and even that is a lookup.
    //
    // This adapter is precisely why App keeps its animation while its logic leaves: R-G-1
    // lives in the view, and the service never learns what an eased property is. It says a
    // load is 6 of 18; the bar decides how to get there.
    void onServiceEvent(Host *a, const arstro::cosmo::Event &e)
    {
        using K = arstro::cosmo::Event::Kind;
        // Every event IS a log line (R-SVC-5), so the load's progress, its failures and the
        // measured thread peak all reach cosmo_v2.log without one dedicated LOGI per fact.
        LOGI("%s", arstro::cosmo::formatEvent(e).c_str());

        switch (e.kind)
        {
            case K::ProjectOpening:
                // Emitted BEFORE the session is reset, and that ordering is load-bearing:
                // App::resetWorkspace clears the visible state (thumb pool, last frame), so
                // beginning the transition afterwards would fade in from nothing instead of
                // from the outgoing editor.
                a->app.beginOpenTransition(e.text);
                a->app.resetWorkspace();
                a->app.setLoadProgress(0, e.b);
                a->app.setStreamProgress(0, e.b);
                a->loadCoverSent = false;
                a->loadRevealed = false;
                break;

            case K::EntryDecoded:
            {
                const int slot = e.b;
                // The filmstrip's thumb pool is indexed BY SLOT, so it must be fed in
                // lockstep with attachment or a cell draws the previous project's image.
                a->app.registerThumb(slot);
                if (!a->loadCoverSent && !a->app.hasLoadingCover())
                {
                    // Preferred cover is the project card's cached 480px thumbnail (#3):
                    // already decoded, no I/O. The fallback is the 110px filmstrip thumb —
                    // small, but this path only happens for a project whose card was never
                    // drawn, and a soft cover beats an empty one.
                    auto it = a->thumbs.find(a->app.sourcePathForSlot(slot));
                    if (it != a->thumbs.end() && it->second.ok())
                        a->app.setLoadingCover(it->second.rgba.data(), it->second.width, it->second.height);
                    else if (const auto *t = a->svc->session().thumbForSlot(slot))
                        a->app.setLoadingCover(t->rgba.data(), t->w, t->h);
                    a->loadCoverSent = true;
                }
                if (!a->loadRevealed)
                {
                    // R-LOADPERF-3: the first photo makes the project showable, but it does
                    // not reveal on its own — the loading screen stays so its bar means
                    // something, and App reveals on completion (or on its own cap, for a
                    // catalog too big to wait for).
                    a->loadRevealed = true;
                    a->app.selectImage(slot);
                    a->app.setLoadUsable();
                }
                else
                    a->app.refreshLibrary();   // the arrived photo replaces its spinner
                break;
            }

            case K::EntryFailed:
                a->app.refreshLibrary();       // the service already stopped the spinner
                break;

            case K::LoadProgress:
                if (!e.text.empty()) a->app.setLoadStatus("Loading  " + e.text);
                a->app.setLoadProgress(e.a, e.b);
                a->app.setStreamProgress(e.a, e.b);   // R-LOADUX-3, in the editor
                gtk_widget_queue_draw(a->area);
                break;

            case K::LoadFinished:
                a->app.setStreamProgress(e.b, e.b);   // fades out
                a->app.refreshLibrary();
                a->app.finishOpenTransition();        // the bar has filled; reveal
                gtk_widget_queue_draw(a->area);
                break;

            case K::SelectionChanged:
            case K::ParamsChanged:
            case K::HistoryChanged:
                // A command changed the edit state from outside the widgets — a script, or an
                // agent on the control socket (R-SVC-8). Re-push the panels so the window
                // agrees with the model; a click through the widgets has already done this
                // for itself, and doing it twice is idempotent.
                a->app.syncFromSession();
                gtk_widget_queue_draw(a->area);
                break;

            case K::Error:
            case K::CommandRejected:
                g_printerr("cosmo_v2: %s\n", e.text.c_str());
                break;

            // Screen ownership stays with App in S2 — it drives the transition phases, which
            // are presentation. S4 makes the model's `screen` authoritative and this becomes
            // a real case rather than a deliberate no-op.
            default: break;
        }
    }

    // Open an existing .cmp: name the path, let the events animate it (R-SVC-2).
    void startProjectLoad(Host *a, const std::string &path)
    {
        arstro::cosmo::Command c;
        c.kind = arstro::cosmo::Command::Kind::ProjectOpen;
        c.path = path;
        a->svc->dispatch(c);
    }

    // Import Catalog: the same animated load, except the .cmp does not exist yet, so the
    // service writes it once the last image has landed.
    void startImportLoad(Host *a, std::vector<std::string> images, const std::string &cmpPath)
    {
        arstro::cosmo::Command c;
        c.kind = arstro::cosmo::Command::Kind::Import;
        c.paths = std::move(images);
        c.path = cmpPath;
        a->svc->dispatch(c);
    }

    void addCmpFilter(GtkWidget *d)
    {
        GtkFileFilter *filt = gtk_file_filter_new();
        gtk_file_filter_set_name(filt, "Cosmo project (*.cmp)");
        gtk_file_filter_add_pattern(filt, "*.cmp");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(d), filt);
    }

    void newProjectDialog(Host *a)
    {
        GtkWidget *d = gtk_file_chooser_dialog_new(
            "New Project", GTK_WINDOW(a->window), GTK_FILE_CHOOSER_ACTION_SAVE,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Create", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(d), TRUE);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d), "Untitled.cmp");
        addCmpFilter(d);
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
            if (path)
            {
                const std::string cmp = ensureCmp(path);
                a->app.resetWorkspace();
                a->app.saveWorkspaceAs(cmp);   // empty project; user adds photos in the editor
                rememberProject(cmp);
                a->app.showEditor();
            }
            g_free(path);
        }
        gtk_widget_destroy(d);
        gtk_widget_queue_draw(a->area);
    }

    void openProjectDialog(Host *a)
    {
        GtkWidget *d = gtk_file_chooser_dialog_new(
            "Open Project", GTK_WINDOW(a->window), GTK_FILE_CHOOSER_ACTION_OPEN,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, nullptr);
        addCmpFilter(d);
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
            if (path) startProjectLoad(a, path);  // animated loading transition (R-LOADING)
            g_free(path);
        }
        gtk_widget_destroy(d);
        gtk_widget_queue_draw(a->area);
    }

    void importCatalogDialog(Host *a)
    {
        GtkWidget *d = gtk_file_chooser_dialog_new(
            "Import Catalog", GTK_WINDOW(a->window), GTK_FILE_CHOOSER_ACTION_OPEN,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Import", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(d), TRUE);
        std::vector<std::string> imgs;
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            GSList *files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(d));
            for (GSList *it = files; it; it = it->next) { imgs.emplace_back((const char *)it->data); g_free(it->data); }
            g_slist_free(files);
        }
        gtk_widget_destroy(d);
        if (imgs.empty()) return;

        GtkWidget *sd = gtk_file_chooser_dialog_new(
            "Save New Project", GTK_WINDOW(a->window), GTK_FILE_CHOOSER_ACTION_SAVE,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Create", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(sd), TRUE);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(sd), "Imported.cmp");
        addCmpFilter(sd);
        std::string cmp;
        if (gtk_dialog_run(GTK_DIALOG(sd)) == GTK_RESPONSE_ACCEPT)
        {
            char *p = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(sd));
            if (p) cmp = ensureCmp(p);
            g_free(p);
        }
        gtk_widget_destroy(sd);
        if (cmp.empty()) return;

        // Import runs the SAME animated open transition as a recent project (R-HOME-5):
        // the picked images become root-level workspace entries and go through the
        // shared background loader, so a big catalog streams in behind the loading
        // screen instead of freezing the UI for the length of every decode. The .cmp
        // does not exist yet, so it is written once the last image has landed.
        // Building the entry list is the service's job now (R-SVC-2): the host names the
        // images and the target .cmp, and nothing else.
        startImportLoad(a, std::move(imgs), cmp);
        gtk_widget_queue_draw(a->area);
    }

    // File -> Export... (and the bare 's' shortcut): renders the CURRENT
    // selection at full resolution with its edits baked in (App::exportFullRes)
    // and writes it out as PNG or JPEG, chosen by the extension of the typed/
    // picked filename (defaulting to PNG when neither is recognized). JPEG has
    // no alpha channel, so that path packs a tight RGB copy for GdkPixbuf's saver
    // rather than handing it the RGBA buffer PNG uses directly.
    void saveDialog(Host *a)
    {
        if (a->app.imageCount() == 0)
            return;
        GtkWidget *d = gtk_file_chooser_dialog_new(
            "Export image", GTK_WINDOW(a->window), GTK_FILE_CHOOSER_ACTION_SAVE,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Export", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(d), TRUE);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d), "cosmo_export.png");

        GtkFileFilter *pngFilter = gtk_file_filter_new();
        gtk_file_filter_set_name(pngFilter, "PNG image (*.png)");
        gtk_file_filter_add_pattern(pngFilter, "*.png");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(d), pngFilter);

        GtkFileFilter *jpegFilter = gtk_file_filter_new();
        gtk_file_filter_set_name(jpegFilter, "JPEG image (*.jpg, *.jpeg)");
        gtk_file_filter_add_pattern(jpegFilter, "*.jpg");
        gtk_file_filter_add_pattern(jpegFilter, "*.jpeg");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(d), jpegFilter);

        gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(d), pngFilter);

        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            char *rawPath = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
            std::string path = rawPath;
            g_free(rawPath);

            const bool jpeg = endsWithCI(path, ".jpg") || endsWithCI(path, ".jpeg");
            if (!jpeg && !endsWithCI(path, ".png"))
                path += ".png";  // no recognized extension typed -> default to PNG

            int w = 0, h = 0;
            const uint8_t *px = a->app.exportFullRes(w, h);
            if (px && w > 0 && h > 0)
            {
                GError *err = nullptr;
                if (jpeg)
                {
                    std::vector<uint8_t> rgb((size_t)w * h * 3);
                    for (size_t i = 0, n = (size_t)w * h; i < n; ++i)
                    {
                        rgb[i * 3 + 0] = px[i * 4 + 0];
                        rgb[i * 3 + 1] = px[i * 4 + 1];
                        rgb[i * 3 + 2] = px[i * 4 + 2];
                    }
                    GdkPixbuf *pb = gdk_pixbuf_new_from_data(rgb.data(), GDK_COLORSPACE_RGB, FALSE, 8,
                                                             w, h, w * 3, nullptr, nullptr);
                    gchar *optKeys[] = {const_cast<gchar *>("quality"), nullptr};
                    gchar *optVals[] = {const_cast<gchar *>("92"), nullptr};
                    gdk_pixbuf_savev(pb, path.c_str(), "jpeg", optKeys, optVals, &err);
                    g_object_unref(pb);
                }
                else
                {
                    GdkPixbuf *pb = gdk_pixbuf_new_from_data(px, GDK_COLORSPACE_RGB, TRUE, 8, w, h,
                                                             w * 4, nullptr, nullptr);
                    gdk_pixbuf_savev(pb, path.c_str(), "png", nullptr, nullptr, &err);
                    g_object_unref(pb);
                }
                if (err) { g_printerr("cosmo_v2: export failed: %s\n", err->message); g_error_free(err); }
                else g_print("cosmo_v2: exported %s (%dx%d)\n", path.c_str(), w, h);
            }
        }
        gtk_widget_destroy(d);
    }

    // ── Export modal host seams (R-EXPORT) ────────────────────────────────────

    // "Change…": pick the one output folder used when "Same as source" is unticked.
    void chooseExportFolderDialog(Host *a)
    {
        GtkWidget *d = gtk_file_chooser_dialog_new(
            "Export to folder", GTK_WINDOW(a->window), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Select", GTK_RESPONSE_ACCEPT, nullptr);
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            gchar *dir = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
            if (dir) { a->app.setExportDestination(dir); g_free(dir); }
        }
        gtk_widget_destroy(d);
        gtk_widget_queue_draw(a->area);
    }

    // Main thread: drain whatever the export worker has finished and report it to the
    // modal, one poll per frame. This is the ONLY place export results touch the UI.
    gboolean pollExport(gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        if (!a->exportJob) return G_SOURCE_REMOVE;
        ExportJob &job = *a->exportJob;
        const size_t total = job.req.slots.size();

        for (;;)
        {
            ExportJob::Result r;
            {
                std::lock_guard<std::mutex> lk(job.mu);
                if (job.consumed >= job.ready.size()) break;
                r = std::move(job.ready[job.consumed]);
                ++job.consumed;
            }
            if (r.ok) g_print("cosmo_v2: exported %s\n", r.path.c_str());
            else
            {
                ++job.failures;
                g_printerr("cosmo_v2: export failed for %s: %s\n", r.path.c_str(), r.error.c_str());
            }
            // Report the count DONE plus the name of the one now in flight, so the
            // status line names what is being written rather than what just finished.
            const std::string next = job.consumed < total
                                         ? a->app.nameForSlot(job.req.slots[job.consumed])
                                         : std::string();
            a->app.setExportProgress((int)job.consumed, (int)total, next);
            gtk_widget_queue_draw(a->area);
        }

        if (job.producedAll.load() && job.consumed >= total)
        {
            LOGI("cosmo_v2: export finished (%zu written, %d failed)", total - job.failures, job.failures);
            job.pollId = 0;                 // we are returning REMOVE; don't double-remove
            a->exportJob.reset();           // joins the worker
            gtk_widget_queue_draw(a->area);
            return G_SOURCE_REMOVE;
        }
        return G_SOURCE_CONTINUE;
    }

    // Fired by the modal only AFTER its collapse animation has finished (R-EXPORT-6
    // beat 1 is pure animation, no I/O — the same split R-LOADING-0/1 uses), so the
    // first full-res render can never stall the tween.
    void startExportBatch(Host *a, App::ExportRequest req)
    {
        if (req.slots.empty()) return;
        if (a->exportJob) return;   // one batch at a time
        LOGI("cosmo_v2: exporting %zu image(s) as %s", req.slots.size(), req.format.c_str());
        a->exportJob = std::make_unique<ExportJob>();
        ExportJob *job = a->exportJob.get();
        job->req = std::move(req);

        job->worker = std::thread([a, job] {
            for (size_t i = 0; i < job->req.slots.size(); ++i)
            {
                if (job->stop.load()) break;
                const int slot = job->req.slots[i];
                const std::string src = a->app.sourcePathForSlot(slot);
                const std::string name = a->app.nameForSlot(slot);

                ExportJob::Result r;
                r.name = name;
                r.path = arstro::cosmo_v2::exporter::resolvePath(job->req, src, name);

                int w = 0, h = 0;
                const uint8_t *px = a->app.exportFullResSlot(slot, w, h);   // blocks HERE, off the UI thread
                if (!px) { r.ok = false; r.error = "no rendered frame"; }
                else       r.ok = arstro::cosmo_v2::exporter::write(job->req, px, w, h, r.path, src, r.error);

                std::lock_guard<std::mutex> lk(job->mu);
                job->ready.push_back(std::move(r));
            }
            job->producedAll.store(true);
        });

        job->pollId = g_timeout_add(15, pollExport, a);   // ~1 poll per frame
    }

    // ── R-SPLASH: the application-open animation ──────────────────────────────

    gboolean onSplashDraw(GtkWidget *, cairo_t *cr, gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        if (!a->splash) return FALSE;
        a->splashTarget.setContext(cr);
        a->splash->render(a->splashTarget);
        a->splash->renderOverlay(a->splashTarget);
        return FALSE;
    }

    void showMainWindow(Host *a);
    gboolean onTick(gpointer user);

    /** Runs once per frame while the splash is up: play the intro, then (and only then)
     *  do the startup work, feeding real progress into the bar; finally fade out, drop
     *  the borderless window and bring the real one up (R-SPLASH-3). */
    gboolean onSplashTick(gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        if (!a->splash) return G_SOURCE_REMOVE;
        a->splash->advance(nowMs(*a));

        if (a->splash->introDone() && !a->startupWorkBegun)
        {
            // The intro has played; NOW do the work it was covering. showHome() fills
            // thumbQueue rather than decoding inline, because startupPhase is set.
            a->startupWorkBegun = true;
            a->splash->setStatus("Scanning projects…");     // R-SPLASH-2a: name the work
            if (a->app.onHomeScreen()) a->app.showHome();   // rebuild recents -> queue thumbs
        }
        if (a->startupWorkBegun)
        {
            // One cover thumbnail per frame: each is a full-resolution decode, so doing
            // them in a loop here would stall the very animation this exists to protect.
            if (a->thumbDone < a->thumbQueue.size())
            {
                const auto &job = a->thumbQueue[a->thumbDone];
                // Name the project whose cover is being read, not the raw file path —
                // the launcher's unit is a project (R-SPLASH-2a).
                const std::string who = a->app.recentName(job.first);
                a->splash->setStatus(who.empty() ? std::string("Loading covers…") : "Loading  " + who);
                DecodedImage img = a->decoder.decodeFile(job.second);
                if (img.ok())
                {
                    DecodedImage thumb = downscaleCover(img, 480);
                    a->app.setHomeThumbnail(job.first, thumb.rgba.data(), thumb.width, thumb.height);
                    a->thumbs[job.second] = std::move(thumb);
                }
                ++a->thumbDone;
            }
            const size_t total = a->thumbQueue.size();
            a->splash->setProgress(total == 0 ? 1.0 : (double)a->thumbDone / (double)total);
            if (a->thumbDone >= total)
            {
                a->splash->setStatus("Ready");
                a->splash->beginExit();
            }
        }

        gtk_widget_queue_draw(a->splashArea);
        if (a->splash->isGone())
        {
            a->startupPhase = false;
            gtk_widget_destroy(a->splashWindow);
            a->splashWindow = nullptr; a->splashArea = nullptr;
            a->splash.reset();
            showMainWindow(a);
            return G_SOURCE_REMOVE;
        }
        return G_SOURCE_CONTINUE;
    }

    void startSplash(Host *a)
    {
        a->startupPhase = true;
        a->splash = std::make_shared<arstro::cosmo_v2::SplashScreen>();
        a->splash->width.set(arstro::cosmo_v2::SplashScreen::kWidth);
        a->splash->height.set(arstro::cosmo_v2::SplashScreen::kHeight);
        a->splash->begin(nowMs(*a));

        // R-SPLASH-1: small, undecorated, centred — a splash, not a screen.
        a->splashWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_decorated(GTK_WINDOW(a->splashWindow), FALSE);
        gtk_window_set_resizable(GTK_WINDOW(a->splashWindow), FALSE);
        gtk_window_set_position(GTK_WINDOW(a->splashWindow), GTK_WIN_POS_CENTER);
        gtk_window_set_type_hint(GTK_WINDOW(a->splashWindow), GDK_WINDOW_TYPE_HINT_SPLASHSCREEN);
        gtk_window_set_default_size(GTK_WINDOW(a->splashWindow),
                                    (int)arstro::cosmo_v2::SplashScreen::kWidth,
                                    (int)arstro::cosmo_v2::SplashScreen::kHeight);
        a->splashArea = gtk_drawing_area_new();
        g_signal_connect(a->splashArea, "draw", G_CALLBACK(onSplashDraw), a);
        gtk_container_add(GTK_CONTAINER(a->splashWindow), a->splashArea);
        gtk_widget_show_all(a->splashWindow);
        g_timeout_add(16, onSplashTick, a);
    }

    void showMainWindow(Host *a)
    {
        gtk_widget_show_all(a->window);
        gtk_widget_grab_focus(a->area);
        g_timeout_add(16, onTick, a);
    }

    gboolean onDraw(GtkWidget *, cairo_t *cr, gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        a->target.setContext(cr);
        a->app.render(a->target, nowMs(*a));
        return FALSE;
    }
    gboolean onTick(gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        // One pump, every frame, unconditionally (R-SVC-6). The old code added and removed a
        // dedicated 15 ms GTK source per load; a service that is always pumped cannot forget
        // to be, and pump() is a cheap early-out when nothing is in flight.
        if (a->svc) a->svc->pump(nowMs(*a));
        gtk_widget_queue_draw(a->area);
        return G_SOURCE_CONTINUE;
    }

    gboolean onButton(GtkWidget *w, GdkEventButton *e, gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        gtk_widget_grab_focus(w);
        const bool alt = (e->state & GDK_MOD1_MASK) != 0;
        const bool shift = (e->state & GDK_SHIFT_MASK) != 0;
        const bool ctrl = (e->state & GDK_CONTROL_MASK) != 0;
        a->app.pointer(e->type == GDK_BUTTON_PRESS ? 0 : 2, e->x, e->y, mapButton(e->button), nowMs(*a), alt, shift, ctrl);
        return TRUE;
    }
    gboolean onScroll(GtkWidget *, GdkEventScroll *e, gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        const bool ctrl = (e->state & GDK_CONTROL_MASK) != 0;
        double dy = 0.0;
        if (e->direction == GDK_SCROLL_UP) dy = 1.0;
        else if (e->direction == GDK_SCROLL_DOWN) dy = -1.0;
        else if (e->direction == GDK_SCROLL_SMOOTH) dy = -e->delta_y;
        a->app.wheel(e->x, e->y, dy, ctrl);
        gtk_widget_queue_draw(a->area);
        return TRUE;
    }
    gboolean onMotion(GtkWidget *, GdkEventMotion *e, gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        const bool alt = (e->state & GDK_MOD1_MASK) != 0;
        const bool shift = (e->state & GDK_SHIFT_MASK) != 0;
        const bool ctrl = (e->state & GDK_CONTROL_MASK) != 0;
        a->app.pointer(1, e->x, e->y, 0, nowMs(*a), alt, shift, ctrl);
        return TRUE;
    }
    void onSizeAllocate(GtkWidget *, GtkAllocation *alloc, gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        a->app.setSize(alloc->width, alloc->height);
    }
    gboolean onKey(GtkWidget *, GdkEventKey *e, gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        const bool ctrl = (e->state & GDK_CONTROL_MASK) != 0;
        const bool shift = (e->state & GDK_SHIFT_MASK) != 0;
        const bool alt = (e->state & GDK_MOD1_MASK) != 0;

        // Text input first: a focused field (the home-screen search box) gets typed
        // characters + backspace; if it consumes them, don't run editor shortcuts.
        if (e->keyval == GDK_KEY_BackSpace)
        {
            artboard::KeyEvent ke; ke.type = artboard::KeyEvent::Type::Down; ke.keyCode = 8;
            ke.shift = shift; ke.ctrl = ctrl; ke.alt = alt;
            if (a->app.key(ke)) { gtk_widget_queue_draw(a->area); return TRUE; }
        }
        else if (!ctrl && !alt)
        {
            const gunichar u = gdk_keyval_to_unicode(e->keyval);
            if (u >= 0x20 && u != 0x7f)
            {
                char buf[8] = {0};
                const int n = g_unichar_to_utf8(u, buf);
                artboard::KeyEvent ke; ke.type = artboard::KeyEvent::Type::Text; ke.text = std::string(buf, (size_t)n);
                if (a->app.key(ke)) { gtk_widget_queue_draw(a->area); return TRUE; }
            }
        }
        // Arrow keys walk the photo rack (R-BROWSE-2). Sent before the plain-key
        // shortcuts so the app can claim them; if it doesn't, GTK keeps its own default.
        if (e->keyval == GDK_KEY_Left || e->keyval == GDK_KEY_Right)
        {
            artboard::KeyEvent ke; ke.type = artboard::KeyEvent::Type::Down;
            ke.keyCode = (e->keyval == GDK_KEY_Left) ? 37 : 39;
            ke.shift = shift; ke.ctrl = ctrl; ke.alt = alt;
            if (a->app.key(ke)) { gtk_widget_queue_draw(a->area); return TRUE; }
        }
        // Enter / Escape reach a focused in-app field (the group-rename box, DR-TREE-5):
        // Enter commits, Escape cancels.
        if (e->keyval == GDK_KEY_Return || e->keyval == GDK_KEY_KP_Enter || e->keyval == GDK_KEY_Escape)
        {
            artboard::KeyEvent ke; ke.type = artboard::KeyEvent::Type::Down;
            ke.keyCode = (e->keyval == GDK_KEY_Escape) ? 27 : 13;
            ke.shift = shift; ke.ctrl = ctrl; ke.alt = alt;
            if (a->app.key(ke)) { gtk_widget_queue_draw(a->area); return TRUE; }
        }
        // While an in-app text field is focused, suppress the editor's single-key
        // shortcuts (Delete, o, s, ...) so those keys edit the text instead.
        if (a->app.isTextEditing()) { gtk_widget_queue_draw(a->area); return TRUE; }

        // On the home screen the launcher owns the keyboard, and during the open
        // transition input is swallowed — don't leak editor keys in either case.
        if (a->app.onHomeScreen() || a->app.inOpenTransition()) return TRUE;

        if (ctrl && (e->keyval == GDK_KEY_z || e->keyval == GDK_KEY_Z))
        {
            if (shift) a->app.redo(); else a->app.undo();
            gtk_widget_queue_draw(a->area);
            return TRUE;
        }
        if (ctrl && (e->keyval == GDK_KEY_y || e->keyval == GDK_KEY_Y))
        {
            a->app.redo();
            gtk_widget_queue_draw(a->area);
            return TRUE;
        }
        // Ctrl+S = save the project (to its current path, else prompt);
        // Ctrl+Shift+S = Save As (always prompt for a new path). Both persist the
        // full workspace: group tree, per-image params, and edit history.
        if (ctrl && (e->keyval == GDK_KEY_s || e->keyval == GDK_KEY_S))
        {
            if (shift) saveWorkspaceDialog(a);
            else       a->app.saveWorkspace();
            gtk_widget_queue_draw(a->area);
            return TRUE;
        }
        if (!ctrl && (e->keyval == GDK_KEY_o || e->keyval == GDK_KEY_O))
        {
            openDialog(a);
            return TRUE;
        }
        if (!ctrl && (e->keyval == GDK_KEY_s || e->keyval == GDK_KEY_S))
        {
            saveDialog(a);
            return TRUE;
        }
        if (e->keyval == GDK_KEY_Delete || e->keyval == GDK_KEY_KP_Delete)
        {
            a->app.deleteSelected();
            gtk_widget_queue_draw(a->area);
            return TRUE;
        }
        return FALSE;
    }
}

int main(int argc, char **argv)
{
    // Logging first: capture startup, every g_print/g_printerr, and any fatal
    // signal's backtrace to ~/.config/cosmo_v2/cosmo_v2.log.
    arstro::cosmo_v2::log::init();
    arstro::cosmo_v2::log::installCrashHandler();
    g_set_print_handler(logPrintHandler);
    g_set_printerr_handler(logPrinterrHandler);
    LOGI("cosmo_v2 starting (%d args, log at %s)", argc - 1,
         arstro::cosmo_v2::log::path().c_str());

    LOGI("cpu: %s", arstro::cosmo_v2::ompPinStatus());

    gtk_init(&argc, &argv);
    registerBundledFonts();

    Host host;
    host.startUs = g_get_monotonic_time();

    host.app.onOpenRequested = [&host] { openDialog(&host); };
    host.app.onSaveAsRequested = [&host] { saveSessionDialog(&host); };
    host.app.onExportRequested = [&host] { saveDialog(&host); };   // bare 's': quick single-image export
    host.app.onChooseExportFolderRequested = [&host] { chooseExportFolderDialog(&host); };
    host.app.onExportBatchRequested = [&host](App::ExportRequest r) { startExportBatch(&host, std::move(r)); };
    host.app.onSavePresetRequested = [&host] { savePresetDialog(&host); };
    host.app.onExportPresetRequested = [&host] { exportPresetDialog(&host); };
    host.app.onImportPresetRequested = [&host] { importPresetDialog(&host); };
    host.app.onRenameGroupRequested = [&host] { renameGroupDialog(&host); };
    host.app.onSaveWorkspaceRequested = [&host] { saveWorkspaceDialog(&host); };
    host.app.onLoadWorkspaceRequested = [&host] { loadWorkspaceDialog(&host); };

    // Home screen / projects (R-HOME).
    host.app.onNewProjectRequested   = [&host] { newProjectDialog(&host); };
    host.app.onOpenProjectRequested  = [&host] { openProjectDialog(&host); };
    host.app.onImportCatalogRequested = [&host] { importCatalogDialog(&host); };
    host.app.onOpenRecentRequested = [&host](const std::string &path) {
        startProjectLoad(&host, path);  // animated loading transition (R-LOADING)
    };
    host.app.onDecodeThumbnail = [&host](int idx, const std::string &imgPath) {
        auto it = host.thumbs.find(imgPath);
        if (it != host.thumbs.end() && it->second.ok())  // already decoded -> reuse (no re-decode on return)
        {
            host.app.setHomeThumbnail(idx, it->second.rgba.data(), it->second.width, it->second.height);
            return;
        }
        // R-SPLASH-3: during launch these are QUEUED and decoded one per frame by the
        // splash tick. Decoding here would be the very stall the splash exists to hide.
        if (host.startupPhase) { host.thumbQueue.emplace_back(idx, imgPath); return; }
        DecodedImage img = host.decoder.decodeFile(imgPath);
        if (!img.ok()) return;
        DecodedImage thumb = downscaleCover(img, 480);  // small: cheap to cache + reuse
        host.app.setHomeThumbnail(idx, thumb.rgba.data(), thumb.width, thumb.height);
        host.thumbs[imgPath] = std::move(thumb);        // reused as the loading-screen centre image (#3)
    };

    // R-SETTINGS-4: what the user last chose is in force before the first frame, and
    // every later change is written straight back.
    host.settings = arstro::cosmo::AppSettings::load();
    // R-SVC-10: the budget is told once, here, and it owns the engine's thread count from
    // then on. App no longer converts the percentage for itself — that second conversion
    // was half of D-11.
    host.budget.setPercent(host.settings.cpuPercent);
    host.budget.setExplicitEngineThreads(host.settings.threads);
    host.app.applySettings(host.settings);

    // ── The service (R-SVC-1) ──────────────────────────────────────────────────────
    // It borrows App's session rather than owning it: App has owned `mSession` since long
    // before any of this, and moving that ownership in the same step as introducing the
    // service would mean rewriting both at once with nothing working in between. S4 moves it
    // and App becomes a view holding a CosmoService&.
    host.svc = std::make_unique<arstro::cosmo::CosmoService>(host.app.session(), host.budget);
    host.svc->setDecoderFactory(
        [] { return std::unique_ptr<arstro::cosmo::IImageDecoder>(new NativeImageDecoder()); });
    // Per-thread, on the thread: the OpenMP count is a per-thread ICV, which is why the old
    // env-var pin in main() bound nothing (D-12).
    host.svc->setWorkerInit([] { arstro::cosmo_v2::pinNestedOpenMPForThisThread(); });
    host.svc->setImageWriter([](const std::string &out, const std::string &src, const uint8_t *rgba,
                               int w, int h, std::string &err) {
        // The encoder stays in the host — a codec inside cosmo_core would break the Android
        // and WASM builds (R-SVC-7). ExportWriter already owns the metadata rules.
        App::ExportRequest req;   // defaults: JPEG q92, EXIF kept, profile embedded
        return arstro::cosmo_v2::exporter::write(req, rgba, w, h, out, src, err);
    });
    host.svc->subscribe([&host](const arstro::cosmo::Event &e) { onServiceEvent(&host, e); });
    host.settings.cpuPercent = host.budget.percent();
    LOGI("cpu: budget %d%% = %d of %d cores; engine %d threads, decode pool would be %d",
         host.budget.percent(), host.budget.total(), host.budget.cores(),
         host.budget.engineThreads(), host.budget.decodeWorkers());
    host.app.onSettingsChanged = [&host](arstro::cosmo::AppSettings s) {
        host.settings = s;
        host.budget.setPercent(s.cpuPercent);              // R-CPU-3: next load, next render
        host.budget.setExplicitEngineThreads(s.threads);   // R-CPU-2b: an explicit count still wins
        if (!s.save()) g_printerr("cosmo_v2: could not save settings to %s\n",
                                  arstro::cosmo::AppSettings::path().c_str());
    };

    host.app.setPresetDir(exeDir() + "/presets");

    // R-SPLASH-3: the launcher's recents + their cover thumbnails are NOT built here —
    // showHome() runs from the splash tick once the intro has played, so nothing heavy
    // happens before the first thing on screen is the animation.
    bool straightToEditor = false;
    {
        std::vector<std::string> paths;
        for (int i = 1; i < argc; ++i) paths.emplace_back(argv[i]);
        if (!paths.empty()) { openPaths(&host, paths); host.app.showEditor(); straightToEditor = true; }
    }

    host.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(host.window), "cosmo. by arstro");
    gtk_window_set_default_size(GTK_WINDOW(host.window), kW, kH);

    host.area = gtk_drawing_area_new();
    gtk_widget_set_size_request(host.area, 640, 400);
    gtk_widget_set_can_focus(host.area, TRUE);
    gtk_widget_add_events(host.area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                        GDK_POINTER_MOTION_MASK | GDK_KEY_PRESS_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);

    g_signal_connect(host.window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    g_signal_connect(host.area, "draw", G_CALLBACK(onDraw), &host);
    g_signal_connect(host.area, "button-press-event", G_CALLBACK(onButton), &host);
    g_signal_connect(host.area, "button-release-event", G_CALLBACK(onButton), &host);
    g_signal_connect(host.area, "motion-notify-event", G_CALLBACK(onMotion), &host);
    g_signal_connect(host.area, "scroll-event", G_CALLBACK(onScroll), &host);
    g_signal_connect(host.area, "key-press-event", G_CALLBACK(onKey), &host);
    g_signal_connect(host.area, "size-allocate", G_CALLBACK(onSizeAllocate), &host);

    gtk_container_add(GTK_CONTAINER(host.window), host.area);
    // The main window is built but NOT shown: the splash owns the screen until its
    // animation has played and the startup work behind it is done (R-SPLASH-1/3).
    if (straightToEditor) showMainWindow(&host);
    else startSplash(&host);
    gtk_main();
    return 0;
}
