/*
 *  Native Linux host for cosmo_v2 by arstro: GTK3 drawing area -> Cairo
 *  (Artboard's CairoTarget). Mirrors cosmo/linux_main.cpp's GTK glue; the only
 *  addition is registering the typeface at startup, which is now a handoff of
 *  bytes that are already IN this binary (EmbeddedFonts, R-FONT-1) rather than a
 *  Fontconfig call on a path baked in at build time — so the drawText font-family
 *  parameter (Artboard FR-22) resolves to the app's own faces on any machine.
 */
#include "App.h"
#include "EmbeddedFonts.h"
#include "touch/PhoneApp.h"
#include "TouchViewport.h"
#include "ExportWriter.h"
#include "Log.h"
#include "ControlChannel.h"
#include "UiDump.h"
#include "widgets/WidgetLog.h"
#include "OmpPin.h"
#include "core/service/AppModelCodec.h"
#include "core/service/CosmoService.h"
#include "core/service/Event.h"
#include "core/ThreadBudget.h"
#include "widgets/SplashScreen.h"
#include "PinnedDecoder.h"
#include "PixelBudget.h"
#include "../../core/Artboard/src/adapter/native/CairoTarget.h"
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
using arstro::cosmo_v2::PinnedDecoder;   // R-CPU-2c / D-41: the host has ONE decoder, and it pins its thread
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
        // S4c: declaration order IS construction order, and it matters — the budget is the
        // service's, and the service is App's. The application is built bottom-up and the view
        // last, which is the shape R-SVC-1 always described and only now compiles.
        arstro::cosmo::ThreadBudget budget;
        arstro::cosmo::CosmoService svc{budget};
        App app{svc, (double)kW, (double)kH};
        artboard::CairoTarget target;
        PinnedDecoder decoder;
        gint64 startUs = 0;
        // R-SVC-8: only present when --control was given; a closed channel is inert, so
        // nothing below has to check whether the app is being driven from outside.
        arstro::cosmo_v2::ControlChannel control;
        bool loadCoverSent = false;   // transition state, not load state (see above)
        bool loadRevealed = false;
        std::map<std::string, DecodedImage> thumbs;  // decoded cover thumbnails, keyed by image path
        std::unique_ptr<ExportJob> exportJob;        // active batch export (nullptr when idle, R-EXPORT-6)
        // The in-force preferences, mirrored here because the load runs in the host.
        arstro::cosmo::AppSettings settings;

        // ── R-TOUCH-6: the same binary can draw the TOUCH shell instead of this one ──
        // Both shells bind to the SAME service above, which is the whole reason this can be a
        // live switch rather than a restart: the project, the selection, the parameters and the
        // undo history are the service's, so nothing is reloaded and nothing is lost. The phone
        // shell is built on first use and then kept, so switching back and forth is free.
        std::unique_ptr<arstro::cosmo_touch::PhoneApp> phone;
        bool touchMode = false;                  // which shell input goes to
        artboard::Property touchFade{0.0};       // 0 = desktop, 1 = touch; between = cross-fade

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
    void registerBundledFonts()
    {
        // R-FONT-1: the faces are IN this binary and go straight to the render adapter. What
        // this replaced — Fontconfig plus a path baked in at build time — made the app's own
        // type depend on a directory next to the source tree and on how the host resolves a
        // family name, and neither is allowed to differ between machines.
        arstro::cosmo_v2::registerEmbeddedFonts();
    }

    void openImageFile(Host *a, const std::string &path)
    {
        DecodedImage img = a->decoder.decodeFile(path);
        if (img.ok())
            a->app.openImage(img.rgba.data(), img.width, img.height, baseName(path), path);
        else
            g_printerr("cosmo_v2: could not decode %s%s\n", path.c_str(),
                       (PinnedDecoder::isRawExtension(path) && !PinnedDecoder::rawSupported())
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

    void openPath(Host *a, const std::string &path);       // fwd
    void startProjectLoad(Host *a, const std::string &path);  // fwd: defined with the load adapter
    void openPaths(Host *a, const std::vector<std::string> &paths)
    {
        for (const std::string &p : paths)
        {
            if (!PinnedDecoder::isRawExtension(p))
            {
                bool rawDup = false;
                for (const std::string &q : paths)
                    if (&q != &p && PinnedDecoder::isRawExtension(q) && stemLower(q) == stemLower(p))
                    { rawDup = true; break; }
                if (rawDup) { g_print("cosmo_v2: skipping %s (RAW with same name preferred)\n", p.c_str()); continue; }
            }
            openPath(a, p);
        }
    }

    void openPath(Host *a, const std::string &path)
    {
        // D-6: the PRIMARY document format used to be the one thing that could not be opened
        // from a shell — `.cmp` fell through to openImageFile() and failed to decode, so a
        // project was reachable only by clicking a recent card. Six other defect entries cited
        // that as their reason for being unverifiable. It is one branch now that the load is a
        // command (R-SVC-2), which is a fair summary of what the service bought.
        if (endsWith(path, ".cmp") || endsWith(path, ".cosmoproj"))
        {
            startProjectLoad(a, path);
            return;
        }
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
    void applyWindowMinimum(Host *a);   // defined below; the scale bridge needs it (R-SCALE-3)

    void onServiceEvent(Host *a, const arstro::cosmo::Event &e)
    {
        using K = arstro::cosmo::Event::Kind;
        // Every event IS a log line (R-SVC-5), so the load's progress, its failures and the
        // measured thread peak all reach cosmo_v2.log without one dedicated LOGI per fact.
        LOGI("%s", arstro::cosmo::formatEvent(e).c_str());

        switch (e.kind)
        {
            // R-SCALE-2: the scale is the one setting the service carries without acting on
            // it, so SOMEBODY has to hand it to the view — and it is the host, because the
            // host is what owns a view at all. Without this bridge `settings set uiScale=125`
            // over the control socket would change the stored value, print it in a dump, and
            // leave the window it is supposedly scaling untouched — a command that lies to a
            // script is worse than a command that does not exist (R-SVC-8).
            case K::SettingsChanged:
                if (a->svc.model().settings.uiScale != a->app.uiScale())
                {
                    a->settings.uiScale = a->svc.model().settings.uiScale;
                    a->app.setUiScale(a->settings.uiScale);
                    applyWindowMinimum(a);
                    if (a->area) gtk_widget_queue_draw(a->area);
                }
                break;

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
                    else if (const auto *t = a->svc.session().thumbForSlot(slot))
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

            case K::EntryStarted:
                // R-LOADUX-4 / D-22: name it when a worker PICKS IT UP, not when it lands. On
                // the reported project this is the difference between the status line changing
                // in milliseconds and changing after 9.1 seconds — the whole of the complaint.
                if (!e.text.empty()) a->app.setLoadStatus("Loading  " + e.text);
                a->app.setLoadInFlight(e.b, a->svc.model().load.stage);
                gtk_widget_queue_draw(a->area);
                break;

            case K::LoadStage:
                // Named phases, so the seconds before any decode finishes still say something
                // true: reading the project, decoding, writing it out for an import.
                a->app.setLoadInFlight(a->svc.model().load.started, e.text);
                if (e.text == "reading") a->app.setLoadStatus("Reading project\xE2\x80\xA6");
                else if (e.text == "saving") a->app.setLoadStatus("Saving project\xE2\x80\xA6");
                gtk_widget_queue_draw(a->area);
                break;

            case K::EntryProgress:
            {
                // D-24: the bar moves inside a single image now — LibRaw reports its own
                // demosaic iterations, which is the 90% of a RAF decode that used to be
                // invisible. The status line gains the sub-stage, so nine seconds of
                // "demosaicing" reads as work rather than as a hang.
                const auto &lm = a->svc.model().load;
                a->app.setLoadFraction(lm.fraction());
                if (!lm.entryStage.empty() && !lm.status.empty())
                    a->app.setLoadStatus(lm.status + "  \xE2\x80\x94  " + lm.entryStage);
                gtk_widget_queue_draw(a->area);
                break;
            }

            case K::LoadProgress:
                if (!e.text.empty()) a->app.setLoadStatus("Loading  " + e.text);
                a->app.setLoadProgress(e.a, e.b);
                a->app.setLoadInFlight(a->svc.model().load.started, a->svc.model().load.stage);
                a->app.setStreamProgress(e.a, e.b);   // R-LOADUX-3, in the editor
                gtk_widget_queue_draw(a->area);
                break;

            case K::LoadFinished:
                // R-CPU-4 as amended: the budget's own honesty clause, as a number rather than
                // a claim. `peakDecode` says how wide the pool actually got; `ompPinnedThreads`
                // says how many decoding threads the nested-team pin actually bound. Before
                // D-41 the second one covered the pool and nothing else, and nothing anywhere
                // would have said so.
                LOGI("cpu: load finished — peak decode %d of budget %d; %d thread(s) pinned (%s)",
                     a->budget.peakDecode(), a->budget.total(),
                     arstro::cosmo_v2::ompPinnedThreadCount(), arstro::cosmo_v2::ompPinStatus());
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
        a->svc.dispatch(c);
    }

    // Import Catalog: the same animated load, except the .cmp does not exist yet, so the
    // service writes it once the last image has landed.
    void startImportLoad(Host *a, std::vector<std::string> images, const std::string &cmpPath)
    {
        arstro::cosmo::Command c;
        c.kind = arstro::cosmo::Command::Kind::Import;
        c.paths = std::move(images);
        c.path = cmpPath;
        a->svc.dispatch(c);
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
        // R-SCALE-2: the splash is chrome like the rest of the shell, so it is drawn at the
        // same scale. Its Segment keeps its design size (420x260 logical) and the window is
        // sized to match in showSplash — the alternative, laying the splash out at a scaled
        // size, would mean a second set of metrics for one window.
        const double s = a->settings.uiScale / 100.0;
        const artboard::Transform root = artboard::Transform::scaling(s, s);
        a->splash->render(a->splashTarget, root);
        a->splash->renderOverlay(a->splashTarget, root);
        return FALSE;
    }

    void showMainWindow(Host *a);
    /** R-SCALE-3: the window may not be made smaller than the shell can be laid out in, AT
     *  THE SCALE IN FORCE. This is the whole enforcement mechanism — the layout code itself
     *  does not have to cope with an impossible box, because GTK will not hand it one.
     *
     *  The minimum is ASKED FOR, not guessed, and it is the app's, not the launcher's. The
     *  home screen sums its own anchored blocks (its action buttons pin to the top and the
     *  Settings / What's New / Help links to the bottom, so at 400 px they overlapped); the
     *  EDITOR's minimum is larger and had never been computed at all, which is how the photo
     *  canvas could be dragged down to 64 px with the rail still open. App::minLogical* takes
     *  the larger of the two, and multiplying by the scale is what makes a bigger scale
     *  require a bigger window instead of quietly breaking the layout.
     *
     *  Called again on every scale change: a size request is not a one-time hint. */
    void applyWindowMinimum(Host *a)
    {
        if (!a->area) return;
        const int minW = (int)std::ceil(a->app.minPhysicalWidth());
        const int minH = (int)std::ceil(a->app.minPhysicalHeight());
        gtk_widget_set_size_request(a->area, minW, minH);
        LOGI("window: minimum %dx%d physical = %.0fx%.0f logical at %d%% scale (R-SCALE-3)",
             minW, minH, App::minLogicalWidth(), App::minLogicalHeight(), a->app.uiScale());
    }

    gboolean onTick(gpointer user);
    void pollControl(Host *a);   // defined below; the splash tick needs it too (R-SVC-8)

    /** Runs once per frame while the splash is up: play the intro, then (and only then)
     *  do the startup work, feeding real progress into the bar; finally fade out, drop
     *  the borderless window and bring the real one up (R-SPLASH-3). */
    gboolean onSplashTick(gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        if (!a->splash) return G_SOURCE_REMOVE;
        a->splash->advance(nowMs(*a));

        // The service and the socket are serviced during the splash too, not just once the
        // main window exists. A client that attaches immediately after launch — which is what
        // a script does, because it has no way to know the intro is still playing — would
        // otherwise have its commands sit unread in the kernel buffer for the length of the
        // animation, and `wait` on the far end would look like a hang (R-SVC-8).
        a->svc.pump(nowMs(*a));
        pollControl(a);

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
                // R-SPLASH-5: the EMBEDDED preview, not a full decode. A cover is drawn at
                // 480 px, and the full path cost 8072 ms against 6.6 ms for the preview the
                // camera already wrote into the file — 1200x. That decode ran inline in this
                // GTK tick, so the splash froze mid-animation for its whole duration and the
                // status text set two lines above was never painted before the freeze began.
                DecodedImage img = a->decoder.decodeThumb(job.second, 480);
                if (img.ok())
                {
                    DecodedImage thumb = downscaleCover(img, 480);
                    a->app.setHomeThumbnail(job.first, thumb.rgba.data(), thumb.width, thumb.height);
                    a->thumbs[job.second] = std::move(thumb);
                }
                ++a->thumbDone;
            }
            const size_t total = a->thumbQueue.size();
            // R-SPLASH-5: the fraction AND the count — "how far" and "how much" are different
            // questions and a bar answers only the first.
            a->splash->setProgress(total == 0 ? 1.0 : (double)a->thumbDone / (double)total,
                                   (int)a->thumbDone, (int)total);
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
        {
            const double s = a->settings.uiScale / 100.0;
            gtk_window_set_default_size(GTK_WINDOW(a->splashWindow),
                                        (int)std::ceil(arstro::cosmo_v2::SplashScreen::kWidth * s),
                                        (int)std::ceil(arstro::cosmo_v2::SplashScreen::kHeight * s));
        }
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

    /** Where the touch shell is drawn inside this window (R-TOUCH-6). The rule itself lives in
     *  TouchViewport.h so the shot renderer can show the same thing; this only supplies the
     *  window's size. */
    artboard::Rect touchViewport(const Host &a)
    {
        int w = kW, h = kH;
        if (a.area)
        {
            const int aw = gtk_widget_get_allocated_width(a.area);
            const int ah = gtk_widget_get_allocated_height(a.area);
            if (aw > 0 && ah > 0) { w = aw; h = ah; }
        }
        return arstro::cosmo_v2::touchViewport((double)w, (double)h);
    }

    /** Turn touch mode on or off. Eased, because it is a visible change (R-G-1). */
    void setTouchMode(Host *a, bool on)
    {
        if (a->touchMode == on && (a->phone || !on)) return;
        a->touchMode = on;
        if (on && !a->phone)
        {
            const artboard::Rect v = touchViewport(*a);
            a->phone.reset(new arstro::cosmo_touch::PhoneApp(a->svc, v.w, v.h));
            // The touch shell asks the host to show/hide the soft keyboard; a desktop has a
            // real one, so there is nothing to do and saying so beats leaving it unset.
            a->phone->onKeyboard = [](bool) {};
            // The SAME dialogs the desktop shell uses (R-TOUCH-1: one project-opening story,
            // whichever shell is drawing). Without these the touch shell would fall back to its
            // own built-in browser, which on a desktop is the wrong file picker.
            a->phone->onNewProjectRequested = [a] { newProjectDialog(a); };
            a->phone->onOpenRequested = [a] { openProjectDialog(a); };
            a->phone->onImportRequested = [a] { importCatalogDialog(a); };
        }
        if (a->phone)
        {
            const artboard::Rect v = touchViewport(*a);
            a->phone->setSize(v.w, v.h);
        }
        a->touchFade.animateTo(on ? 1.0 : 0.0, 260.0, artboard::Easing::EaseOutCubic, nowMs(*a));
        if (a->area) gtk_widget_queue_draw(a->area);
    }

    gboolean onDraw(GtkWidget *, cairo_t *cr, gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        a->target.setContext(cr);
        const double now = nowMs(*a);
        const double f = a->touchFade.update(now);

        // Neither shell is redrawn at partial alpha unless the switch is actually in flight, so
        // the common case costs exactly what it did before: one shell, no layer.
        if (f <= 0.001) { a->app.render(a->target, now); return FALSE; }

        if (f < 0.999)
        {
            a->target.pushLayer(1.0 - f);
            a->app.render(a->target, now);
            a->target.popLayer();
        }
        if (a->phone)
        {
            const artboard::Rect v = touchViewport(*a);
            if (f < 0.999) a->target.pushLayer(f);
            a->target.save();
            // The letterbox is painted, not left transparent: an unpainted gutter would show
            // whatever the desktop shell drew there a frame ago.
            a->target.setTransform(artboard::Transform::identity());
            drawRoundedRect(a->target, artboard::Rect{0, 0, (double)gtk_widget_get_allocated_width(a->area),
                                                      (double)gtk_widget_get_allocated_height(a->area)},
                            0.0, artboard::Paint::filled(arstro::cosmo_v2::palette::background()));
            a->target.restore();
            a->phone->setOrigin(v.x, v.y);   // the shell offsets ITSELF (see PhoneApp::render)
            a->phone->render(a->target, now);
            if (f < 0.999) a->target.popLayer();
        }
        return FALSE;
    }
    // R-SVC-8: a line off the socket lands in the SAME dispatch a click produces, on the UI
    // thread, between frames — so there is no locking to get wrong, and "an agent did it" is
    // indistinguishable from "the user did it". Three commands are answered here rather than
    // in the service because only the caller knows where to print, who owns the loop, and
    // what the view looks like (CosmoService.cpp documents all three as front-end concerns).
    void pollControl(Host *a)
    {
        if (!a->control.isOpen()) return;
        a->control.poll([a](const std::string &line) {
            std::string err;
            const arstro::cosmo::Command c = arstro::cosmo::parseCommand(line, err);
            if (!c.valid())
            {
                // A blank or #-commented line parses to None with no error: a no-op, so a
                // client can pipe a commented script straight in.
                if (!err.empty())
                    a->control.broadcast("[evt] command.rejected line=" + line + " why=" + err);
                return;
            }
            if (c.kind == arstro::cosmo::Command::Kind::StatePrint)
            {
                // formatModel is multi-line, so it is framed: a client reading line-by-line
                // needs to know where a dump starts and stops.
                arstro::cosmo::ModelDumpOptions o;
                o.json = c.flag;
                o.stable = c.field("stable") == "1";    // D-14
                o.params = c.field("params") == "1";
                a->control.broadcast("[evt] state.begin");
                a->control.broadcast(arstro::cosmo::formatModel(a->svc.model(), o));
                a->control.broadcast("[evt] state.end");
                return;
            }
            if (c.kind == arstro::cosmo::Command::Kind::UiDump)
            {
                // P0.6 — "what is on screen" answered without a screen. Host-side, like the
                // state dump above and for the same reason: the Segment tree is presentation,
                // and CosmoService is not allowed to know it exists (R-SVC-3). The splash is
                // dumped from HERE rather than from App because the host owns it -- it lives
                // in its own borderless window and is not part of either App root.
                arstro::cosmo_v2::UiDumpOptions o;
                o.json = c.flag;
                o.visibleOnly = c.field("visible") == "1";
                if (!c.field("depth").empty()) o.maxDepth = std::atoi(c.field("depth").c_str());
                const std::string want = c.name.empty() ? "all" : c.name;
                a->control.broadcast("[evt] ui.begin");
                bool any = false;
                for (const auto &n : App::uiRootNames())
                {
                    if (want != "all" && want != n) continue;
                    if (const artboard::Segment *r = a->app.uiRoot(n))
                    {
                        a->control.broadcast(arstro::cosmo_v2::dumpSegmentTree(*r, n, o));
                        any = true;
                    }
                }
                if ((want == "all" || want == "splash") && a->splash)
                {
                    a->control.broadcast(arstro::cosmo_v2::dumpSegmentTree(*a->splash, "splash", o));
                    any = true;
                }
                if (!any)
                    a->control.broadcast("ui-root " + want + " (absent)");
                a->control.broadcast("[evt] ui.end");
                return;
            }
            a->svc.dispatch(c);   // a rejection already reaches the client via the event sink
            if (a->svc.quitRequested()) gtk_main_quit();
        });
    }

    gboolean onTick(gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        // One pump, every frame, unconditionally (R-SVC-6). The old code added and removed a
        // dedicated 15 ms GTK source per load; a service that is always pumped cannot forget
        // to be, and pump() is a cheap early-out when nothing is in flight.
        const double now = nowMs(*a);
        a->svc.pump(now);
        pollControl(a);
        // T3.1: repaint only when something on screen is actually moving. This line used to
        // be an unconditional queue_draw, so the whole window was re-rendered in software
        // Cairo sixty times a second forever — at rest, with nothing changing. A frame is
        // cheap (1.50 ms for the scaled photo, 0.07 ms for a full-window fill), so the cost
        // was never per-frame: it was that it never stopped, and on a small board that is the
        // core the render engine needs. R-G-1 forbids a change in one frame; it does not
        // require a repaint at rest.
        //
        // The pump still runs every tick unconditionally (R-SVC-6) — the service must never
        // depend on the view wanting to draw.
        if (a->touchMode || a->touchFade.isAnimating() || a->app.needsRedraw(now))
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
        if (a->touchMode && a->phone)
        {
            const artboard::Rect v = touchViewport(*a);
            a->phone->pointer(e->type == GDK_BUTTON_PRESS ? 0 : 2, e->x - v.x, e->y - v.y,
                              mapButton(e->button), nowMs(*a), alt, shift, ctrl);
            return TRUE;
        }
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
        if (a->touchMode && a->phone)
        {
            const artboard::Rect v = touchViewport(*a);
            a->phone->wheel(e->x - v.x, e->y - v.y, dy, ctrl);
        }
        else
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
        if (a->touchMode && a->phone)
        {
            const artboard::Rect v = touchViewport(*a);
            a->phone->pointer(1, e->x - v.x, e->y - v.y, 0, nowMs(*a), alt, shift, ctrl);
            return TRUE;
        }
        a->app.pointer(1, e->x, e->y, 0, nowMs(*a), alt, shift, ctrl);
        return TRUE;
    }
    void onSizeAllocate(GtkWidget *, GtkAllocation *alloc, gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        a->app.setSize(alloc->width, alloc->height);
        // The phone shell is laid out in its viewport, not in the window (R-TOUCH-6): in a wide
        // window that is a centred portrait column, and it has to be re-measured here or the
        // touch UI would keep the size it was built at.
        if (a->phone)
        {
            const artboard::Rect v = touchViewport(*a);
            a->phone->setSize(v.w, v.h);
        }
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

        // R-HOME-1c: a modal owns the keyboard. Routed BEFORE the host's own accelerators, or
        // Ctrl+S behind an unsaved-changes prompt would start a second save while the first
        // question is still on screen.
        if (auto dlg = a->app.confirmDialog())
            if (dlg->isOpen())
            {
                ::artboard::KeyEvent ke;
                ke.type = ::artboard::KeyEvent::Type::Down;
                ke.keyCode = (e->keyval == GDK_KEY_Escape) ? 0x1B
                             : (e->keyval == GDK_KEY_Return || e->keyval == GDK_KEY_KP_Enter) ? 0x0D
                                                                                             : 0;
                dlg->handleKey(ke);
                gtk_widget_queue_draw(a->area);
                return TRUE;
            }

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
    // P0.4: flags and env before init(), so even the session header states the level and
    // categories in force — a run that filtered everything away must still explain why.
    arstro::cosmo_v2::log::configureFromArgs(argc, argv);
    arstro::cosmo_v2::log::init();
    arstro::cosmo_v2::log::installCrashHandler();
    // D-3: g_log's DEFAULT handler, not merely g_print/g_printerr — GTK, Cairo and
    // GdkPixbuf all diagnose through g_warning/g_critical, which the old pair never saw,
    // so exactly the messages worth having were the ones missing from the file.
    arstro::cosmo_v2::log::installGlibHandler();
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
        DecodedImage img = host.decoder.decodeThumb(imgPath, 480);   // R-SPLASH-5
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
    host.app.applySettings(host.settings);

    // ── The service (R-SVC-1) ──────────────────────────────────────────────────────
    // It borrows App's session rather than owning it: App has owned `mSession` since long
    // before any of this, and moving that ownership in the same step as introducing the
    // service would mean rewriting both at once with nothing working in between. S4 moves it
    // and App becomes a view holding a CosmoService&.
    host.svc.setDecoderFactory(
        [] { return arstro::cosmo_v2::makePinnedDecoder(); });
    // Per-thread, on the thread: the OpenMP count is a per-thread ICV, which is why the old
    // env-var pin in main() bound nothing (D-12).
    host.svc.setWorkerInit([] { arstro::cosmo_v2::pinNestedOpenMPForThisThread(); });
    host.svc.setImageWriter([](const std::string &out, const std::string &src, const uint8_t *rgba,
                               int w, int h, std::string &err) {
        // The encoder stays in the host — a codec inside cosmo_core would break the Android
        // and WASM builds (R-SVC-7). ExportWriter already owns the metadata rules.
        App::ExportRequest req;   // defaults: JPEG q92, EXIF kept, profile embedded
        return arstro::cosmo_v2::exporter::write(req, rgba, w, h, out, src, err);
    });
    host.svc.subscribe([&host](const arstro::cosmo::Event &e) { onServiceEvent(&host, e); });
    // One call, so the budget, the session and the model can never disagree about what is in
    // force (D-15). App::applySettings above still seeds the widgets, which is a view concern.
    host.svc.applySettings(host.settings);
    // R-SVC-2: the view's outbound channel. A menu item, a shortcut and a line on the control
    // socket now travel the same path, so they cannot behave differently.
    host.app.onCommand = [&host](arstro::cosmo::Command c) { host.svc.dispatch(c); };
    host.settings.cpuPercent = host.budget.percent();
    // R-CPU-2c / D-41: Host::decoder is the LONE decoder — opening one photo, a .cosmo, the
    // synchronous workspace load — all on the UI thread with no pool around them. Attaching
    // the budget is what puts those decodes inside it: they may use the whole share, and no
    // more. The pool's own decoders come from makePinnedDecoder() with no budget, so each of
    // them opens a team of one and `decodeWorkers() x 1` is again exactly the budget.
    host.decoder.setBudget(&host.budget);
    // R-MEM-1/5: the engine's pixel caches are a share of THIS machine, not two literals. A
    // 26 MB proxy against the old 1 GB default held 37 photos, so browsing a 120-photo project
    // re-decoded on most hops (D-44) while 27 GB of RAM sat unused.
    {
        const auto caps = arstro::cosmo_v2::pixelCapsForThisMachine();
        host.svc.session().renderService().setMemoryCaps(caps.sourceBytes, caps.proxyBytes);
        LOGI("mem: pixel caches %zu MB sources + %zu MB proxies of %zu MB physical (R-MEM-1)",
             caps.sourceBytes / (1024 * 1024), caps.proxyBytes / (1024 * 1024),
             arstro::cosmo_v2::physicalMemoryBytes() / (1024 * 1024));
    }
    LOGI("cpu: budget %d%% = %d of %d cores; %d schedulable after the UI's %d (R-CPU-2d); "
         "engine %d threads, decode pool would be %d",
         host.budget.percent(), host.budget.total(), host.budget.cores(),
         host.budget.schedulable(), arstro::cosmo::ThreadBudget::kUiReserve,
         host.budget.engineThreads(), host.budget.decodeWorkers());
    host.app.onSettingsChanged = [&host](arstro::cosmo::AppSettings s) {
        host.settings = s;
        // R-SCALE-3: re-ask for the minimum at the new scale, and let GTK grow the window if
        // the current size no longer satisfies it. Without this, choosing 125% in a window
        // that only just met the 100% minimum would leave the shell laid out in a box smaller
        // than its own floor, and setSize's clamp would crop it rather than reflow it.
        applyWindowMinimum(&host);
        host.budget.setPercent(s.cpuPercent);              // R-CPU-3: next load, next render
        host.budget.setExplicitEngineThreads(s.threads);   // R-CPU-2b: an explicit count still wins
        if (!s.save()) g_printerr("cosmo_v2: could not save settings to %s\n",
                                  arstro::cosmo::AppSettings::path().c_str());
        setTouchMode(&host, s.touchUi);   // R-TOUCH-6: the host owns which shell exists
    };

    // §7: route the widget layer's trace into the log's `ui` category. The widget layer cannot
    // include Log.h — that is a host facility, and cosmo_widget_tests links neither GTK nor
    // cosmo_core — so it declares a sink and the host fills it in. Costs one null check per call
    // site when nothing is listening, which is every run without --debug.
    arstro::cosmo_v2::setWidgetLogSink([](const char *line) { CLOGD(Ui, "%s", line); });

    host.app.setPresetDir(exeDir() + "/presets");

    // R-SPLASH-3: the launcher's recents + their cover thumbnails are NOT built here —
    // showHome() runs from the splash tick once the intro has played, so nothing heavy
    // happens before the first thing on screen is the animation.
    bool straightToEditor = false;
    std::string controlPath;   // R-SVC-8
    {
        // Every other argv entry is still a file to open, so --control has to be pulled out
        // here or the socket path would be handed to openPaths() as though it were a photo.
        std::vector<std::string> paths;
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            // Before anything else, or --log-level=debug is collected as a photo to open.
            if (int n = arstro::cosmo_v2::log::consumeArgs(argc, argv, i)) { i += n - 1; continue; }
            if (arg == "--control" && i + 1 < argc) { controlPath = argv[++i]; continue; }
            if (arg.rfind("--control=", 0) == 0) { controlPath = arg.substr(10); continue; }
            // --project is the same thing as a bare .cmp argument; it exists because a flag is
            // unambiguous in a generated script, where a bare path could be an image.
            if (arg == "--project" && i + 1 < argc) { paths.emplace_back(argv[++i]); continue; }
            if (arg.rfind("--project=", 0) == 0) { paths.emplace_back(arg.substr(10)); continue; }
            paths.emplace_back(arg);
        }
        if (!paths.empty())
        {
            bool anyProject = false;
            for (const std::string &p : paths)
                if (endsWith(p, ".cmp") || endsWith(p, ".cosmoproj")) anyProject = true;
            openPaths(&host, paths);
            // A project opened from argv runs its normal animated load (R-LOADING), so the
            // screen belongs to the transition; only a bare image list lands straight in the
            // editor. Either way the splash is skipped — the user named something to open.
            if (!anyProject) host.app.showEditor();
            straightToEditor = true;
        }
    }

    // Opened after the service is fully configured, so no event a startup command emits can
    // be missed by a client that attaches immediately.
    if (!controlPath.empty())
    {
        std::string err;
        if (!host.control.open(controlPath, err))
            LOGE("control: %s", err.c_str());   // non-fatal: the window still runs unattended
        else
        {
            LOGI("control: driving cosmo from %s — same commands a click produces (R-SVC-8)",
                 host.control.path().c_str());
            // R-SVC-3/5: one event stream, one format. The socket, the log and a --watch
            // client all see the identical line.
            host.svc.subscribe([&host](const arstro::cosmo::Event &e) {
                host.control.broadcast(arstro::cosmo::formatEvent(e));
            });
        }
    }

    host.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(host.window), "cosmo. by arstro");
    gtk_window_set_default_size(GTK_WINDOW(host.window), kW, kH);

    host.area = gtk_drawing_area_new();
    // R-SCALE-3: tell App how big the screen actually is, so the settings row can disable the
    // scales this display cannot give a window for instead of offering a trap. The WORK AREA,
    // not the raw monitor size — a panel dock or a title bar is height the window will never
    // get, and a scale whose minimum only fits behind the taskbar does not fit.
    {
        GdkDisplay *dpy = gdk_display_get_default();
        GdkMonitor *mon = dpy ? gdk_display_get_primary_monitor(dpy) : nullptr;
        if (!mon && dpy && gdk_display_get_n_monitors(dpy) > 0) mon = gdk_display_get_monitor(dpy, 0);
        GdkRectangle area{};
        if (mon) gdk_monitor_get_workarea(mon, &area);
        if (area.width > 0 && area.height > 0)
        {
            host.app.setDisplaySize((double)area.width, (double)area.height);
            int mx = arstro::cosmo::AppSettings::uiScales().front();
            for (int s : arstro::cosmo::AppSettings::uiScales())
                if (host.app.scaleFitsDisplay(s)) mx = s;
            LOGI("display: work area %dx%d — screen scale offered up to %d%% here (R-SCALE-3)",
                 area.width, area.height, mx);
        }
        else
        {
            // Said out loud rather than left as a silently full list: "every scale is offered"
            // and "we could not find out" look identical in the dialog, and only one of them
            // means a 200%% chip is safe to click.
            LOGI("display: size unknown (no monitor work area) — every screen scale offered");
        }
    }

    applyWindowMinimum(&host);
    gtk_widget_set_can_focus(host.area, TRUE);
    gtk_widget_add_events(host.area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                        GDK_POINTER_MOTION_MASK | GDK_KEY_PRESS_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);

    // R-HOME-1c: REFUSE the close (return TRUE) and ask inside the window instead. `destroy`
    // alone was the whole exit path, so clicking the X discarded an entire editing session with
    // no warning — the unsaved-changes prompt existed but guarded only the route back to the
    // launcher (D-53). `App::requestQuit` calls back here once it is safe to go.
    host.app.onQuitApproved = [] { gtk_main_quit(); };
    g_signal_connect(host.window, "delete-event",
                     G_CALLBACK(+[](GtkWidget *, GdkEvent *, gpointer user) -> gboolean {
                         auto *a = static_cast<Host *>(user);
                         a->app.requestQuit();
                         if (a->area) gtk_widget_queue_draw(a->area);
                         return TRUE;   // never let GTK close it for us
                     }),
                     &host);
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
