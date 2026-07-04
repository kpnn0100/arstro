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
#include <unistd.h>
#include <cctype>
#include <string>
#include <vector>

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

    std::string baseName(const std::string &p)
    {
        auto s = p.find_last_of("/\\");
        return s == std::string::npos ? p : p.substr(s + 1);
    }
    bool endsWith(const std::string &s, const std::string &suf)
    {
        return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
    }

    void openImageFile(App *a, const std::string &path)
    {
        DecodedImage img = a->decoder.decodeFile(path);
        if (img.ok())
            a->app.openImage(img.rgba.data(), img.width, img.height, baseName(path), path);
        else
            g_printerr("cosmo: could not decode %s%s\n", path.c_str(),
                       (NativeImageDecoder::isRawExtension(path) && !NativeImageDecoder::rawSupported())
                           ? " (RAW needs a LibRaw build)" : "");
    }

    // filename without directory or extension, lowercased (for same-name matching)
    std::string stemLower(const std::string &p)
    {
        std::string b = baseName(p);
        auto dot = b.find_last_of('.');
        if (dot != std::string::npos) b = b.substr(0, dot);
        for (char &c : b) c = (char)std::tolower((unsigned char)c);
        return b;
    }

    // Open a set of paths, but when the same base name exists as both RAW and a
    // rendered format (e.g. IMG_1.RW2 + IMG_1.JPG), keep only the RAW (#11).
    void openPath(App *a, const std::string &path);  // fwd
    void openPaths(App *a, const std::vector<std::string> &paths)
    {
        for (const std::string &p : paths)
        {
            if (!NativeImageDecoder::isRawExtension(p))
            {
                bool rawDup = false;
                for (const std::string &q : paths)
                    if (&q != &p && NativeImageDecoder::isRawExtension(q) && stemLower(q) == stemLower(p))
                    { rawDup = true; break; }
                if (rawDup) { g_print("cosmo: skipping %s (RAW with same name preferred)\n", p.c_str()); continue; }
            }
            openPath(a, p);
        }
    }

    void openPath(App *a, const std::string &path)
    {
        if (endsWith(path, ".cosmo"))  // a saved session: load the referenced image + its params
        {
            std::string imgPath;
            arstro::EditParams params;
            if (CosmoApp::readSessionFile(path, imgPath, params) && !imgPath.empty())
            {
                DecodedImage img = a->decoder.decodeFile(imgPath);
                if (img.ok())
                {
                    a->app.openImage(img.rgba.data(), img.width, img.height, baseName(imgPath), imgPath);
                    a->app.applyParams(params);
                }
                else
                    g_printerr("cosmo: session image not found: %s\n", imgPath.c_str());
            }
            return;
        }
        openImageFile(a, path);
    }

    void saveSessionDialog(App *a)
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
            if (a->app.saveSessionAs(path)) g_print("cosmo: saved session %s\n", path);
            g_free(path);
        }
        gtk_widget_destroy(d);
        gtk_widget_queue_draw(a->area);
    }

    void saveWorkspaceDialog(App *a)
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
            if (a->app.saveWorkspaceAs(p)) g_print("cosmo: saved workspace %s\n", p.c_str());
            g_free(path);
        }
        gtk_widget_destroy(d);
        gtk_widget_queue_draw(a->area);
    }

    // Recreate every image + group from a parsed workspace (decoding is host-side;
    // the app only knows the group tree + develop settings). A missing source file
    // becomes a placeholder leaf so later entries' `parent` indices stay aligned.
    void loadWorkspaceFile(App *a, const std::string &path)
    {
        std::vector<CosmoApp::WorkspaceEntry> entries;
        if (!CosmoApp::readWorkspaceFile(path, entries))
        {
            g_printerr("cosmo: could not read workspace %s\n", path.c_str());
            return;
        }
        a->app.resetWorkspace();
        for (const auto &e : entries)
        {
            const int parentNode = e.parent < 0 ? 0 : e.parent + 1;  // node 0 = workspace root
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
                g_printerr("cosmo: workspace image missing: %s\n", e.imagePath.c_str());
                a->app.addWorkspaceMissingImage(parentNode, baseName(e.imagePath));
            }
        }
        a->app.finishWorkspaceLoad(path);
    }

    void loadWorkspaceDialog(App *a)
    {
        if (a->app.imageCount() > 0)  // loading replaces the whole session -- confirm first
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

    void renameGroupDialog(App *a)
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

    void savePresetDialog(App *a)
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
            if (name && *name && a->app.savePreset(name)) g_print("cosmo: saved preset '%s'\n", name);
        }
        gtk_widget_destroy(d);
        gtk_widget_queue_draw(a->area);
    }

    // Export the current develop settings to a chosen .apf path (Export preset).
    void exportPresetDialog(App *a)
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
            if (!p.empty() && !endsWith(p, ".apf")) p += ".apf";  // enforce the extension
            if (a->app.exportPresetTo(p)) g_print("cosmo: exported preset %s\n", p.c_str());
            g_free(path);
        }
        gtk_widget_destroy(d);
        gtk_widget_queue_draw(a->area);
    }

    // Pick an .apf file to import (then the app raises the category picker).
    void importPresetDialog(App *a)
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
                g_printerr("cosmo: could not import preset %s (bad file or different engine)\n", path);
            g_free(path);
        }
        gtk_widget_destroy(d);
        gtk_widget_queue_draw(a->area);
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
            std::vector<std::string> paths;
            for (GSList *it = files; it; it = it->next) { paths.emplace_back((const char *)it->data); g_free(it->data); }
            g_slist_free(files);
            openPaths(a, paths);  // prefer RAW over a same-name rendered file
        }
        gtk_widget_destroy(d);
        gtk_widget_queue_draw(a->area);
    }

    void saveDialog(App *a)
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
            const uint8_t *px = a->app.exportFullRes(w, h);  // full-res RGBA8
            if (px && w > 0 && h > 0)
            {
                GdkPixbuf *pb = gdk_pixbuf_new_from_data(px, GDK_COLORSPACE_RGB, TRUE, 8, w, h,
                                                         w * 4, nullptr, nullptr);
                GError *err = nullptr;
                gdk_pixbuf_savev(pb, path, "png", nullptr, nullptr, &err);
                if (err) { g_printerr("cosmo: export failed: %s\n", err->message); g_error_free(err); }
                else g_print("cosmo: exported %s (%dx%d)\n", path, w, h);
                g_object_unref(pb);
            }
            g_free(path);
        }
        gtk_widget_destroy(d);
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
        const bool alt = (e->state & GDK_MOD1_MASK) != 0;  // Mod1 = Alt
        const bool shift = (e->state & GDK_SHIFT_MASK) != 0;
        const bool ctrl = (e->state & GDK_CONTROL_MASK) != 0;
        a->app.pointer(e->type == GDK_BUTTON_PRESS ? 0 : 2, e->x, e->y, mapButton(e->button), nowMs(*a), alt, shift, ctrl);
        return TRUE;
    }
    gboolean onScroll(GtkWidget *, GdkEventScroll *e, gpointer user)
    {
        auto *a = static_cast<App *>(user);
        const bool ctrl = (e->state & GDK_CONTROL_MASK) != 0;
        double dy = 0.0;
        if (e->direction == GDK_SCROLL_UP) dy = 1.0;
        else if (e->direction == GDK_SCROLL_DOWN) dy = -1.0;
        else if (e->direction == GDK_SCROLL_SMOOTH) dy = -e->delta_y;  // up = positive
        a->app.wheel(e->x, e->y, dy, ctrl);
        gtk_widget_queue_draw(a->area);
        return TRUE;
    }
    gboolean onMotion(GtkWidget *, GdkEventMotion *e, gpointer user)
    {
        auto *a = static_cast<App *>(user);
        const bool alt = (e->state & GDK_MOD1_MASK) != 0;
        const bool shift = (e->state & GDK_SHIFT_MASK) != 0;
        const bool ctrl = (e->state & GDK_CONTROL_MASK) != 0;
        a->app.pointer(1, e->x, e->y, 0, nowMs(*a), alt, shift, ctrl);
        return TRUE;
    }
    void onSizeAllocate(GtkWidget *, GtkAllocation *alloc, gpointer user)
    {
        auto *a = static_cast<App *>(user);
        a->app.setSize(alloc->width, alloc->height);  // reflow the UI to the window
    }
    gboolean onKey(GtkWidget *, GdkEventKey *e, gpointer user)
    {
        auto *a = static_cast<App *>(user);
        const bool ctrl = (e->state & GDK_CONTROL_MASK) != 0;
        const bool shift = (e->state & GDK_SHIFT_MASK) != 0;
        // Ctrl+Z = undo, Ctrl+Y (or Ctrl+Shift+Z) = redo
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
    gtk_init(&argc, &argv);
    App app;
    app.startUs = g_get_monotonic_time();

    // File menu actions -> native dialogs
    app.app.onOpenRequested = [&app] { openDialog(&app); };
    app.app.onSaveAsRequested = [&app] { saveSessionDialog(&app); };
    app.app.onSavePresetRequested = [&app] { savePresetDialog(&app); };
    app.app.onExportPresetRequested = [&app] { exportPresetDialog(&app); };
    app.app.onImportPresetRequested = [&app] { importPresetDialog(&app); };
    app.app.onRenameGroupRequested = [&app] { renameGroupDialog(&app); };
    app.app.onSaveWorkspaceRequested = [&app] { saveWorkspaceDialog(&app); };
    app.app.onLoadWorkspaceRequested = [&app] { loadWorkspaceDialog(&app); };

    // Presets live in a "presets" folder next to the cosmo executable, so a preset
    // library travels with the app. Fall back to the CWD if the exe path is unknown.
    {
        std::string exeDir = ".";
        char buf[4096];
        ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n > 0) { buf[n] = '\0'; std::string exe(buf); auto s = exe.find_last_of('/'); if (s != std::string::npos) exeDir = exe.substr(0, s); }
        app.app.setPresetDir(exeDir + "/presets");
    }

    // open any files passed on the command line (image or .cosmo session)
    {
        std::vector<std::string> paths;
        for (int i = 1; i < argc; ++i) paths.emplace_back(argv[i]);
        openPaths(&app, paths);  // prefer RAW over a same-name rendered file
    }

    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "Cosmo by arstro");
    gtk_window_set_default_size(GTK_WINDOW(app.window), kW, kH);

    app.area = gtk_drawing_area_new();
    gtk_widget_set_size_request(app.area, 640, 400);  // minimum; the area fills the window
    gtk_widget_set_can_focus(app.area, TRUE);
    gtk_widget_add_events(app.area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                        GDK_POINTER_MOTION_MASK | GDK_KEY_PRESS_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);

    g_signal_connect(app.window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    g_signal_connect(app.area, "draw", G_CALLBACK(onDraw), &app);
    g_signal_connect(app.area, "button-press-event", G_CALLBACK(onButton), &app);
    g_signal_connect(app.area, "button-release-event", G_CALLBACK(onButton), &app);
    g_signal_connect(app.area, "motion-notify-event", G_CALLBACK(onMotion), &app);
    g_signal_connect(app.area, "scroll-event", G_CALLBACK(onScroll), &app);
    g_signal_connect(app.area, "key-press-event", G_CALLBACK(onKey), &app);
    g_signal_connect(app.area, "size-allocate", G_CALLBACK(onSizeAllocate), &app);

    gtk_container_add(GTK_CONTAINER(app.window), app.area);
    gtk_widget_show_all(app.window);
    gtk_widget_grab_focus(app.area);
    g_timeout_add(16, onTick, &app);
    gtk_main();
    return 0;
}
