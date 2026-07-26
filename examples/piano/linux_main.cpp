/*
 *  Native Linux host for the Arstro Piano example.
 *
 *    rendering : GTK3 drawing area -> Cairo, via Artboard's CairoTarget adapter.
 *    audio     : a dedicated ALSA playback thread pulls PianoApp::renderAudio().
 *    input     : QWERTY row (a w s e d f t g y h u j k) -> one octave of notes;
 *                Z/X shift the octave; Space/Shift/Ctrl are the three pedals.
 *
 *  Same shape as examples/synth/linux_main.cpp — see that file's header comment
 *  for the general pattern this follows.
 */
#include "PianoApp.h"
#include "../../Artboard/src/adapter/native/CairoTarget.h"
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/XKBlib.h>
#include <alsa/asoundlib.h>
#include <atomic>
#include <cstdlib>
#include <thread>
#include <vector>
#include <map>

using arstro::examples::PianoApp;

namespace
{
    constexpr int kW = 800, kH = 340;
    constexpr unsigned kRate = 48000;
    // Latency, not throughput, is what makes a software piano feel wrong: a
    // pianist notices past ~15 ms between keypress and sound. This app used to ask
    // ALSA for a 50 ms buffer and write 512-frame periods, i.e. ~50-60 ms — the
    // single reason it felt laggy. Measured, the engine needs only ~19 % of the
    // real-time budget at 128 frames (0.51 ms of work per 2.67 ms block), so the
    // buffer can come down by 5x and still have 5x headroom against xruns.
    // Override at runtime with ARSTRO_PIANO_FRAMES / ARSTRO_PIANO_LATENCY_US if a
    // particular machine needs more slack.
    constexpr int kAudioFrames = 128;         // ~2.67 ms period
    constexpr unsigned kLatencyUs = 10000;    // ~10 ms total buffer

    struct App
    {
        GtkWidget *window = nullptr;
        GtkWidget *area = nullptr;
        PianoApp piano{(double)kW, (double)kH};
        artboard::CairoTarget target;
        gint64 startUs = 0;
        std::thread audioThread;
        std::atomic<bool> running{true};
        std::map<guint, int> held; // keyval -> repeat guard
    };

    // GTK keyval -> PianoApp key code: modifiers/space get their own codes
    // (16/17/32, matching common DOM keyCode values), letters -> ASCII.
    int keyCodeFor(guint keyval)
    {
        switch (keyval)
        {
        case GDK_KEY_Shift_L: case GDK_KEY_Shift_R: return 16;
        case GDK_KEY_Control_L: case GDK_KEY_Control_R: return 17;
        case GDK_KEY_space: return 32;
        default: break;
        }
        gunichar u = gdk_keyval_to_unicode(gdk_keyval_to_lower(keyval));
        return (u >= 'a' && u <= 'z') ? (int)u : 0;
    }

    double nowMs(const App &a)
    {
        return a.startUs == 0 ? 0.0 : (g_get_monotonic_time() - a.startUs) / 1000.0;
    }

    // Without this, X11 simulates a held key by sending rapid synthetic
    // release+press pairs (not one press followed by a real release) — the
    // `held` repeat-guard above then sees a spurious release mid-hold, calls
    // key(code,false) (note off / damper engages), and the very next synthetic
    // press re-triggers key(code,true) — so a genuinely held key chatters
    // on/off at the X11 repeat rate instead of sustaining. XKB's "detectable
    // autorepeat" mode makes X11 send ONE press for a hold and a real release
    // only when the key actually comes up, matching what `held` already
    // assumes. Must be set before the window is shown / events start flowing.
    void enableDetectableKeyAutoRepeat()
    {
        GdkDisplay *display = gdk_display_get_default();
        if (!display || !GDK_IS_X11_DISPLAY(display))
            return; // no-op under Wayland (native Wayland doesn't have this quirk)
        Display *xdisplay = GDK_DISPLAY_XDISPLAY(display);
        Bool supported = False;
        XkbSetDetectableAutoRepeat(xdisplay, True, &supported);
        if (!supported)
            g_warning("piano: XKB detectable autorepeat not supported by this X server — "
                      "held notes may chatter instead of sustaining");
    }

    void audioLoop(App *a)
    {
        snd_pcm_t *pcm = nullptr;
        if (snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0)
        {
            g_warning("piano: no ALSA device — running silent");
            return;
        }
        int frames = kAudioFrames;
        unsigned latencyUs = kLatencyUs;
        if (const char *e = std::getenv("ARSTRO_PIANO_FRAMES"))
        {
            const int v = std::atoi(e);
            if (v >= 16 && v <= 4096) frames = v;
        }
        if (const char *e = std::getenv("ARSTRO_PIANO_LATENCY_US"))
        {
            const long v = std::atol(e);
            if (v >= 1000 && v <= 200000) latencyUs = (unsigned)v;
        }
        if (snd_pcm_set_params(pcm, SND_PCM_FORMAT_FLOAT_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                               2, kRate, 1, latencyUs) < 0)
        {
            g_warning("piano: ALSA params failed — running silent");
            snd_pcm_close(pcm);
            return;
        }
        g_message("piano: audio %d frames/period, ~%.1f ms buffer", frames, latencyUs / 1000.0);
        std::vector<float> buf((size_t)frames * 2);
        while (a->running.load(std::memory_order_relaxed))
        {
            a->piano.renderAudio(buf.data(), frames);
            snd_pcm_sframes_t w = snd_pcm_writei(pcm, buf.data(), frames);
            if (w < 0)
                snd_pcm_recover(pcm, (int)w, 1); // recover from xrun/underrun
        }
        snd_pcm_drain(pcm);
        snd_pcm_close(pcm);
    }

    gboolean onDraw(GtkWidget *, cairo_t *cr, gpointer user)
    {
        auto *a = static_cast<App *>(user);
        a->target.setContext(cr);
        a->piano.render(a->target, nowMs(*a));
        return FALSE;
    }
    gboolean onTick(gpointer user)
    {
        gtk_widget_queue_draw(static_cast<App *>(user)->area);
        return G_SOURCE_CONTINUE;
    }
    gboolean onKeyPress(GtkWidget *, GdkEventKey *e, gpointer user)
    {
        auto *a = static_cast<App *>(user);
        if (a->held.count(e->keyval))
            return TRUE; // ignore auto-repeat
        int code = keyCodeFor(e->keyval);
        if (!code)
            return FALSE;
        a->held[e->keyval] = code;
        a->piano.key(code, true);
        return TRUE;
    }
    gboolean onKeyRelease(GtkWidget *, GdkEventKey *e, gpointer user)
    {
        auto *a = static_cast<App *>(user);
        auto it = a->held.find(e->keyval);
        if (it == a->held.end())
            return FALSE;
        a->piano.key(it->second, false);
        a->held.erase(it);
        return TRUE;
    }
    void onDestroy(GtkWidget *, gpointer user)
    {
        static_cast<App *>(user)->running.store(false);
        gtk_main_quit();
    }
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);
    enableDetectableKeyAutoRepeat();

    App app;
    app.piano.setSampleRate(kRate);
    app.startUs = g_get_monotonic_time();

    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "Arstro Piano");
    gtk_window_set_default_size(GTK_WINDOW(app.window), kW, kH);
    gtk_window_set_resizable(GTK_WINDOW(app.window), FALSE);

    app.area = gtk_drawing_area_new();
    gtk_widget_set_size_request(app.area, kW, kH);
    gtk_widget_set_can_focus(app.area, TRUE);
    gtk_widget_add_events(app.area, GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK);

    g_signal_connect(app.window, "destroy", G_CALLBACK(onDestroy), &app);
    g_signal_connect(app.area, "draw", G_CALLBACK(onDraw), &app);
    g_signal_connect(app.window, "key-press-event", G_CALLBACK(onKeyPress), &app);
    g_signal_connect(app.window, "key-release-event", G_CALLBACK(onKeyRelease), &app);

    gtk_container_add(GTK_CONTAINER(app.window), app.area);
    gtk_widget_show_all(app.window);
    gtk_widget_grab_focus(app.area);

    app.audioThread = std::thread(audioLoop, &app);
    g_timeout_add(16, onTick, &app); // ~60 fps redraw
    gtk_main();

    app.running.store(false);
    if (app.audioThread.joinable())
        app.audioThread.join();
    return 0;
}
