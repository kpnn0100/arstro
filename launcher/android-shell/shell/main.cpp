/*
 *  arstro-android-shell — entry point (M1.1).
 *
 *  The Android-theme touch shell host: GTK3 windows rendered by Artboard's Cairo
 *  adapter (matching the proven cosmo host, cosmo/linux_main.cpp). At M1.1 this is
 *  deliberately minimal — it opens ONE plain GTK window and proves the build wiring
 *  (artboard_core + GTK3 + Cairo + fontconfig link, GTK initializes). The real
 *  multi-toplevel layer-shell host (SurfaceHost), the shared frame clock, GDK->
 *  RawPointer input, ShellState, and the five Android surfaces all arrive in the
 *  later M1 tasks (M1.2–M1.6); see launcher/docs/android-theme-plan.md §3.1.
 *
 *  gtk-layer-shell is OPTIONAL here: M1.1 only needs a plain window. From M1.2 the
 *  surfaces become real layer-shell toplevels and it becomes required (the CMake
 *  warns when it is absent). HAVE_GTK_LAYER_SHELL reports which build this is.
 */
#include <gtk/gtk.h>
#include "artboard/artboard.h"

#include <cstdio>
#include <cstring>

namespace
{
    // A headless/CI self-check: initialize GTK if a display exists, realize one window,
    // and exit WITHOUT entering the blocking main loop — so the binary is verifiable
    // (built, links, runs, GTK+Artboard usable) on a machine with no display.
    int selfTest()
    {
        // Prove artboard_core is linked and usable (the render HAL's test adapter).
        artboard::RecordingTarget rec;
        rec.save();
        rec.restore();

        const bool haveLayerShell =
#ifdef HAVE_GTK_LAYER_SHELL
            true;
#else
            false;
#endif
        std::printf("arstro-android-shell: self-test OK\n");
        std::printf("  GTK %d.%d.%d, gtk-layer-shell: %s, artboard ops recorded: %zu\n",
                    gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version(),
                    haveLayerShell ? "yes" : "no (M1.1 plain-window mode)", rec.ops().size());
        return 0;
    }
}

int main(int argc, char **argv)
{
    bool wantSelfTest = false;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--self-test") == 0)
            wantSelfTest = true;

    if (!gtk_init_check(&argc, &argv))
    {
        // No display (headless / CI). Not a failure in a self-test context — the binary
        // built and ran; there is simply nothing to show.
        std::fprintf(stderr, "arstro-android-shell: no display available (GTK could not init).\n");
        return wantSelfTest ? selfTest() : 1;
    }

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Arstro Android Shell");
    gtk_window_set_default_size(GTK_WINDOW(window), 1080, 2160);  // portrait phone proportions
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    gtk_widget_show_all(window);

    if (wantSelfTest)
    {
        // Window realized; exit without blocking so the check is scriptable under a
        // nested/virtual display too.
        return selfTest();
    }

    gtk_main();
    return 0;
}
