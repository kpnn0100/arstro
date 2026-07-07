/*
 *  Native Linux host for cosmo_v2 by arstro: GTK3 drawing area -> Cairo
 *  (Artboard's CairoTarget). Mirrors cosmo/linux_main.cpp's GTK glue; the only
 *  addition is registering the vendored DM Sans / JetBrains Mono files with
 *  Fontconfig at startup (App-private, no system install) so the drawText
 *  font-family parameter (Artboard FR-22) resolves to them.
 */
#include "App.h"
#include "Log.h"
#include "../cosmo/decode/NativeImageDecoder.h"
#include "../Artboard/src/adapter/native/CairoTarget.h"
#include <fontconfig/fontconfig.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <unistd.h>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using arstro::cosmo_v2::App;
using arstro::cosmo::NativeImageDecoder;
using arstro::cosmo::DecodedImage;

namespace
{
    constexpr int kW = 1440, kH = 900;  // Figma base artboard size

    // A project open in progress: the workspace entries decoded one-per-idle-tick
    // so the animated loading screen keeps rendering (R-LOADING) instead of the UI
    // freezing on a synchronous decode-all.
    struct LoadJob
    {
        std::vector<App::WorkspaceEntry> entries;
        size_t i = 0;
        std::string path;
        bool coverSent = false;
    };

    struct Host
    {
        GtkWidget *window = nullptr;
        GtkWidget *area = nullptr;
        App app{(double)kW, (double)kH};
        artboard::CairoTarget target;
        NativeImageDecoder decoder;
        gint64 startUs = 0;
        std::unique_ptr<LoadJob> load;  // active incremental project load (nullptr when idle)
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

    std::string exeDir()
    {
        char buf[4096];
        ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n <= 0) return ".";
        buf[n] = '\0';
        std::string exe(buf);
        auto s = exe.find_last_of('/');
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
        // COSMO_V2_SOURCE_DIR (baked in by CMakeLists.txt) locates assets next to
        // the source tree, independent of the build directory or CWD.
        const std::string dir = std::string(COSMO_V2_SOURCE_DIR) + "/assets/fonts";
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
                a->app.addWorkspaceGroup(parentNode, e.name, e.offset);
                continue;
            }
            DecodedImage img = a->decoder.decodeFile(e.imagePath);
            if (img.ok())
            {
                const int slot = a->app.openImageInto(parentNode, img.rgba.data(), img.width, img.height,
                                                       baseName(e.imagePath), e.imagePath);
                a->app.applyParamsToSlot(slot, e.params);
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

    // One step of an incremental project load (called from the GTK idle loop, so
    // draw frames interleave and the loading screen animates). Decodes one entry
    // per call, feeding the App's cover + progress; on completion reveals the editor.
    gboolean stepLoad(gpointer user)
    {
        auto *a = static_cast<Host *>(user);
        LoadJob *job = a->load.get();
        if (!job) return G_SOURCE_REMOVE;

        if (job->i >= job->entries.size())  // done: finish + reveal
        {
            a->app.finishWorkspaceLoad(job->path);
            rememberProject(job->path);
            a->app.finishOpenTransition();
            a->load.reset();
            gtk_widget_queue_draw(a->area);
            return G_SOURCE_REMOVE;
        }

        const auto &e = job->entries[job->i];
        const int parentNode = e.parent < 0 ? 0 : e.parent + 1;
        if (e.group)
        {
            a->app.addWorkspaceGroup(parentNode, e.name, e.offset);
        }
        else
        {
            DecodedImage img = a->decoder.decodeFile(e.imagePath);
            if (img.ok())
            {
                const int slot = a->app.openImageInto(parentNode, img.rgba.data(), img.width, img.height,
                                                       baseName(e.imagePath), e.imagePath);
                a->app.applyParamsToSlot(slot, e.params);
                if (!job->coverSent)  // first decoded image -> the loading-screen cover
                {
                    a->app.setLoadingCover(img.rgba.data(), img.width, img.height);
                    job->coverSent = true;
                }
            }
            else
            {
                g_printerr("cosmo_v2: workspace image missing: %s\n", e.imagePath.c_str());
                a->app.addWorkspaceMissingImage(parentNode, baseName(e.imagePath));
            }
        }
        job->i++;
        a->app.setLoadProgress((int)job->i, (int)job->entries.size());
        gtk_widget_queue_draw(a->area);
        return G_SOURCE_CONTINUE;
    }

    // Open a .cmp project with the animated loading transition (R-LOADING): start
    // the transition, then decode its images incrementally off the idle loop.
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
        a->load = std::make_unique<LoadJob>();
        a->load->entries = std::move(entries);
        a->load->path = path;
        a->app.setLoadProgress(0, (int)a->load->entries.size());
        g_idle_add(stepLoad, a);
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

    void saveDialog(Host *a)
    {
        if (a->app.imageCount() == 0)
            return;
        GtkWidget *d = gtk_file_chooser_dialog_new(
            "Export image", GTK_WINDOW(a->window), GTK_FILE_CHOOSER_ACTION_SAVE,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Export", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(d), TRUE);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d), "cosmo_export.png");
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
            char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
            int w = 0, h = 0;
            const uint8_t *px = a->app.exportFullRes(w, h);
            if (px && w > 0 && h > 0)
            {
                GdkPixbuf *pb = gdk_pixbuf_new_from_data(px, GDK_COLORSPACE_RGB, TRUE, 8, w, h,
                                                         w * 4, nullptr, nullptr);
                GError *err = nullptr;
                gdk_pixbuf_savev(pb, path, "png", nullptr, nullptr, &err);
                if (err) { g_printerr("cosmo_v2: export failed: %s\n", err->message); g_error_free(err); }
                else g_print("cosmo_v2: exported %s (%dx%d)\n", path, w, h);
                g_object_unref(pb);
            }
            g_free(path);
        }
        gtk_widget_destroy(d);
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
        DecodedImage img = host.decoder.decodeFile(imgPath);
        if (img.ok()) host.app.setHomeThumbnail(idx, img.rgba.data(), img.width, img.height);
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
