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
#include "core/decode/NativeImageDecoder.h"
#include "../Artboard/src/adapter/native/CairoTarget.h"
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

    // A project open in progress. The heavy image decode runs on a BACKGROUND
    // THREAD (producer) so it never stalls the UI; the GTK main thread (consumer)
    // polls finished results and applies them to the session + drives the animated
    // loading screen (R-LOADING). Results are consumed strictly in entry order so
    // the group-tree parent indices stay valid.
    struct LoadJob
    {
        struct Result
        {
            bool group = false;
            std::string name;                 // group name, or leaf display name
            std::string imagePath;            // source path (image leaves)
            int parent = -1;
            arstro::EditParams params;         // per-item edit params (image or group)
            arstro::cosmo::History history;    // per-item branching edit timeline
            std::vector<uint8_t> rgba;         // decoded pixels (image leaves)
            int w = 0, h = 0;
            bool decoded = false;              // false = missing/failed image
            bool bypass = false;               // R-BYPASS-6: node's filter disabled
        };

        std::vector<App::WorkspaceEntry> entries;
        std::string path;
        std::mutex mu;                         // guards `ready`
        std::vector<Result> ready;             // produced results, in order
        std::atomic<bool> producedAll{false};  // worker finished producing
        std::atomic<bool> stop{false};         // ask the worker to bail out
        size_t consumed = 0;                   // main-thread applied count
        bool coverSent = false;
        std::thread worker;
        guint pollId = 0;

        ~LoadJob()
        {
            stop.store(true);
            if (worker.joinable()) worker.join();
            if (pollId) g_source_remove(pollId);
        }
    };

    // R-EXPORT-6: a batch export writes ONE image per main-loop idle step rather
    // than looping inline, so the modal keeps painting its progress bar and the app
    // never appears frozen (R2). The job holds the request + the cursor into it.
    struct ExportJob
    {
        App::ExportRequest req;
        size_t index = 0;
        int failures = 0;
    };

    struct Host
    {
        GtkWidget *window = nullptr;
        GtkWidget *area = nullptr;
        App app{(double)kW, (double)kH};
        artboard::CairoTarget target;
        NativeImageDecoder decoder;
        gint64 startUs = 0;
        std::unique_ptr<LoadJob> load;              // active project load (nullptr when idle)
        std::map<std::string, DecodedImage> thumbs;  // decoded cover thumbnails, keyed by image path
        std::unique_ptr<ExportJob> exportJob;        // active batch export (nullptr when idle, R-EXPORT-6)
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

    // Background producer: decode every entry's image (the heavy work) off the UI
    // thread, pushing results in order. Touches only the job + its own decoder — no
    // App/GTK access — so there is no data race with the main thread.
    void decodeWorker(LoadJob *job)
    {
        NativeImageDecoder dec;  // worker-local; GdkPixbuf/LibRaw decode is thread-safe per instance
        for (const auto &e : job->entries)
        {
            if (job->stop.load()) return;
            LoadJob::Result r;
            r.group = e.group;
            r.parent = e.parent;
            r.params = e.params;
            r.history = e.history;
            r.bypass = e.bypass;               // R-BYPASS-6
            if (e.group)
            {
                r.name = e.name;
            }
            else
            {
                r.imagePath = e.imagePath;
                r.name = baseName(e.imagePath);
                DecodedImage img = dec.decodeFile(e.imagePath);
                if (img.ok()) { r.rgba = std::move(img.rgba); r.w = img.width; r.h = img.height; r.decoded = true; }
            }
            std::lock_guard<std::mutex> lk(job->mu);
            job->ready.push_back(std::move(r));
        }
        job->producedAll.store(true);
    }

    // Main-thread consumer (GTK timeout): apply any decoded results in order, feed
    // the loading cover + progress, and on completion finish + reveal. Runs while
    // the worker decodes, so the loading screen animates at full frame rate.
    gboolean pollLoad(gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        LoadJob *job = a->load.get();
        if (!job) return G_SOURCE_REMOVE;

        for (;;)  // drain everything ready this tick
        {
            LoadJob::Result r;
            {
                std::lock_guard<std::mutex> lk(job->mu);
                if (job->consumed >= job->ready.size()) break;
                r = std::move(job->ready[job->consumed]);
                ++job->consumed;
            }
            const int parentNode = r.parent < 0 ? 0 : r.parent + 1;
            if (r.group)
            {
                a->app.addWorkspaceGroup(parentNode, r.name, r.params, r.history, r.bypass);
            }
            else if (r.decoded)
            {
                const int slot = a->app.openImageInto(parentNode, r.rgba.data(), r.w, r.h, r.name, r.imagePath);
                a->app.applyParamsToSlot(slot, r.params, r.history);
                a->app.setSlotBypass(slot, r.bypass);   // R-BYPASS-6
                // Fallback cover only if the thumbnail wasn't already supplied (#3):
                // normally the loading-screen centre image is the cached thumbnail.
                if (!job->coverSent && !a->app.hasLoadingCover())
                {
                    a->app.setLoadingCover(r.rgba.data(), r.w, r.h);
                    job->coverSent = true;
                }
            }
            else
            {
                g_printerr("cosmo_v2: workspace image missing: %s\n", r.imagePath.c_str());
                a->app.addWorkspaceMissingImage(parentNode, r.name);
            }
            if (!r.name.empty())
                a->app.setLoadStatus("Loading  " + r.name);  // what's loading, above the bar
            a->app.setLoadProgress((int)job->consumed, (int)job->entries.size());
            gtk_widget_queue_draw(a->area);
        }

        if (job->producedAll.load() && job->consumed >= job->entries.size())
        {
            a->app.finishWorkspaceLoad(job->path);
            rememberProject(job->path);
            a->app.finishOpenTransition();  // signals load done; reveal waits for the intro
            job->pollId = 0;                // returning REMOVE drops this source; don't double-remove
            a->load.reset();                // joins the (already-finished) worker
            return G_SOURCE_REMOVE;
        }
        return G_SOURCE_CONTINUE;
    }

    // Open a .cmp project with the animated loading transition (R-LOADING): start the
    // transition immediately, decode its images on a background thread, and apply the
    // results on the UI thread so the heavy work never stalls the animation.
    void startProjectLoad(Host *a, const std::string &path)
    {
        std::vector<App::WorkspaceEntry> entries;
        if (!App::readWorkspaceFile(path, entries))
        {
            g_printerr("cosmo_v2: could not read project %s\n", path.c_str());
            return;
        }
        a->app.beginOpenTransition(std::filesystem::path(path).stem().string());
        a->app.resetWorkspace();

        // Loading-screen centre image = the project's already-decoded cover thumbnail
        // (#3), so it is present in part 1 with no I/O. Falls back to the first full
        // decode (pollLoad) if the thumbnail wasn't cached yet.
        for (const auto &e : entries)
            if (!e.group && !e.imagePath.empty())
            {
                auto it = a->thumbs.find(e.imagePath);
                if (it != a->thumbs.end() && it->second.ok())
                    a->app.setLoadingCover(it->second.rgba.data(), it->second.width, it->second.height);
                break;
            }

        a->load.reset();  // stop/join any prior load first
        a->load = std::make_unique<LoadJob>();
        LoadJob *job = a->load.get();
        job->entries = std::move(entries);
        job->path = path;
        a->app.setLoadProgress(0, (int)job->entries.size());

        // Part 1 is pure animation: start the background decode + poll ONLY when the
        // intro finishes (#4). App fires onLoadingReady at that point.
        a->app.onLoadingReady = [a]() {
            LoadJob *j = a->load.get();
            if (!j || j->worker.joinable()) return;  // guard against a double-fire
            j->worker = std::thread(decodeWorker, j);
            j->pollId = g_timeout_add(15, pollLoad, a);  // ~1 poll per frame
        };
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

        a->app.resetWorkspace();
        for (const auto &ip : imgs)
        {
            DecodedImage img = a->decoder.decodeFile(ip);
            if (img.ok()) a->app.openImage(img.rgba.data(), img.width, img.height, baseName(ip), ip);
        }
        a->app.saveWorkspaceAs(cmp);
        rememberProject(cmp);
        a->app.showEditor();
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

    // One image per idle step: render it full-res through the session (which composes
    // the same params the preview did, so bypass is honoured -- R-EXPORT-7), encode it
    // via ExportWriter, then report progress so the modal's bar advances (R-EXPORT-6).
    gboolean exportStep(gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        if (!a->exportJob) return G_SOURCE_REMOVE;
        ExportJob &job = *a->exportJob;
        const auto &slots = job.req.slots;

        if (job.index >= slots.size())
        {
            LOGI("cosmo_v2: export finished (%zu written, %d failed)", slots.size() - job.failures, job.failures);
            a->app.setExportProgress((int)slots.size(), (int)slots.size(), "");
            a->exportJob.reset();
            gtk_widget_queue_draw(a->area);
            return G_SOURCE_REMOVE;
        }

        const int slot = slots[job.index];
        const std::string src = a->app.sourcePathForSlot(slot);
        const std::string name = a->app.nameForSlot(slot);
        const std::string out = arstro::cosmo_v2::exporter::resolvePath(job.req, src, name);

        int w = 0, h = 0;
        const uint8_t *px = a->app.exportFullResSlot(slot, w, h);
        std::string err;
        if (!px || !arstro::cosmo_v2::exporter::write(job.req, px, w, h, out, src, err))
        {
            ++job.failures;
            g_printerr("cosmo_v2: export failed for %s: %s\n", out.c_str(),
                       err.empty() ? "no rendered frame" : err.c_str());
        }
        else
            g_print("cosmo_v2: exported %s (%dx%d)\n", out.c_str(), w, h);

        ++job.index;
        // Report the count DONE plus the name of the one now in flight, so the status
        // line names what is being written rather than what already finished.
        const std::string next = job.index < slots.size()
                                     ? a->app.nameForSlot(slots[job.index])
                                     : std::string();
        a->app.setExportProgress((int)job.index, (int)slots.size(), next);
        gtk_widget_queue_draw(a->area);
        return G_SOURCE_CONTINUE;
    }

    void startExportBatch(Host *a, App::ExportRequest req)
    {
        if (req.slots.empty()) return;
        if (a->exportJob) return;   // one batch at a time
        LOGI("cosmo_v2: exporting %zu image(s) as %s", req.slots.size(), req.format.c_str());
        a->exportJob = std::make_unique<ExportJob>();
        a->exportJob->req = std::move(req);
        g_idle_add(exportStep, a);
    }

    gboolean onDraw(GtkWidget *, cairo_t *cr, gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        a->target.setContext(cr);
        a->app.render(a->target, nowMs(*a));
        return FALSE;
    }
    gboolean onTick(gpointer user) { gtk_widget_queue_draw(static_cast<Host *>(user)->area); return G_SOURCE_CONTINUE; }
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
        DecodedImage img = host.decoder.decodeFile(imgPath);
        if (!img.ok()) return;
        DecodedImage thumb = downscaleCover(img, 480);  // small: cheap to cache + reuse
        host.app.setHomeThumbnail(idx, thumb.rgba.data(), thumb.width, thumb.height);
        host.thumbs[imgPath] = std::move(thumb);        // reused as the loading-screen centre image (#3)
    };

    host.app.setPresetDir(exeDir() + "/presets");

    {
        std::vector<std::string> paths;
        for (int i = 1; i < argc; ++i) paths.emplace_back(argv[i]);
        if (paths.empty()) host.app.showHome();  // start on the launcher (R-HOME-1)
        else { openPaths(&host, paths); host.app.showEditor(); }
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
    gtk_widget_show_all(host.window);
    gtk_widget_grab_focus(host.area);
    g_timeout_add(16, onTick, &host);
    gtk_main();
    return 0;
}
