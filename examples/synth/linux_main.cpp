/*
 *  Native Linux host for the Basic Synth example.
 *
 *    rendering : GTK3 drawing area -> Cairo, via Artboard's CairoTarget adapter.
 *    audio     : a dedicated ALSA playback thread pulls SynthApp::renderAudio().
 *    input     : mouse -> raw pointer events; QWERTY row (a w s e d f t g y h u j k)
 *                -> one octave of notes.
 *
 *  SynthApp is platform-free: note/param input is queued lock-free into the DSP
 *  engine and the scope ring is mutex-guarded, so the GTK (UI) thread and the ALSA
 *  (audio) thread share it safely. If no audio device opens, the UI still runs.
 */
#include "SynthApp.h"
#include "../../Artboard/src/adapter/native/CairoTarget.h"
#include <gtk/gtk.h>
#include <alsa/asoundlib.h>
#include <atomic>
#include <thread>
#include <vector>
#include <map>

using arstro::examples::SynthApp;

namespace
{
    constexpr int kW = 900, kH = 480;
    constexpr unsigned kRate = 48000;
    constexpr int kAudioFrames = 512;

    struct App
    {
        GtkWidget *window = nullptr;
        GtkWidget *area = nullptr;
        SynthApp synth{(double)kW, (double)kH};
        artboard::CairoTarget target;
        gint64 startUs = 0;
        std::thread audioThread;
        std::atomic<bool> running{true};
        std::map<guint, int> held; // keyval -> midi note
    };

    double nowMs(const App &a)
    {
        return a.startUs == 0 ? 0.0 : (g_get_monotonic_time() - a.startUs) / 1000.0;
    }

    void audioLoop(App *a)
    {
        snd_pcm_t *pcm = nullptr;
        if (snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0)
        {
            g_warning("synth: no ALSA device — running silent");
            return;
        }
        if (snd_pcm_set_params(pcm, SND_PCM_FORMAT_FLOAT_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                               2, kRate, 1, 50000) < 0)
        {
            g_warning("synth: ALSA params failed — running silent");
            snd_pcm_close(pcm);
            return;
        }
        std::vector<float> buf((size_t)kAudioFrames * 2);
        while (a->running.load(std::memory_order_relaxed))
        {
            a->synth.renderAudio(buf.data(), kAudioFrames);
            snd_pcm_sframes_t w = snd_pcm_writei(pcm, buf.data(), kAudioFrames);
            if (w < 0)
                snd_pcm_recover(pcm, (int)w, 1); // recover from xrun/underrun
        }
        snd_pcm_drain(pcm);
        snd_pcm_close(pcm);
    }

    int mapButton(guint b) { return b == 3 ? 2 : 0; }

    gboolean onDraw(GtkWidget *, cairo_t *cr, gpointer user)
    {
        auto *a = static_cast<App *>(user);
        a->target.setContext(cr);
        a->synth.render(a->target, nowMs(*a));
        return FALSE;
    }
    gboolean onTick(gpointer user)
    {
        gtk_widget_queue_draw(static_cast<App *>(user)->area);
        return G_SOURCE_CONTINUE;
    }
    gboolean onButton(GtkWidget *w, GdkEventButton *e, gpointer user)
    {
        auto *a = static_cast<App *>(user);
        gtk_widget_grab_focus(w);
        int kind = e->type == GDK_BUTTON_PRESS ? 0 : 2;
        a->synth.pointer(kind, e->x, e->y, mapButton(e->button), nowMs(*a));
        return TRUE;
    }
    gboolean onMotion(GtkWidget *, GdkEventMotion *e, gpointer user)
    {
        auto *a = static_cast<App *>(user);
        a->synth.pointer(1, e->x, e->y, 0, nowMs(*a));
        return TRUE;
    }
    int midiForKey(guint keyval)
    {
        static const char *row = "awsedftgyhujk"; // 13 keys, C..C
        gunichar u = gdk_keyval_to_unicode(gdk_keyval_to_lower(keyval));
        for (int i = 0; row[i]; ++i)
            if ((gunichar)row[i] == u)
                return 60 + i;
        return -1;
    }
    gboolean onKeyPress(GtkWidget *, GdkEventKey *e, gpointer user)
    {
        auto *a = static_cast<App *>(user);
        if (a->held.count(e->keyval))
            return TRUE; // ignore auto-repeat
        int midi = midiForKey(e->keyval);
        if (midi < 0)
            return FALSE;
        a->held[e->keyval] = midi;
        a->synth.noteOn(midi, 0.85);
        return TRUE;
    }
    gboolean onKeyRelease(GtkWidget *, GdkEventKey *e, gpointer user)
    {
        auto *a = static_cast<App *>(user);
        auto it = a->held.find(e->keyval);
        if (it == a->held.end())
            return FALSE;
        a->synth.noteOff(it->second);
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

    App app;
    app.synth.setSampleRate(kRate);
    app.startUs = g_get_monotonic_time();

    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "Arstro Basic Synth");
    gtk_window_set_default_size(GTK_WINDOW(app.window), kW, kH);
    gtk_window_set_resizable(GTK_WINDOW(app.window), FALSE);

    app.area = gtk_drawing_area_new();
    gtk_widget_set_size_request(app.area, kW, kH);
    gtk_widget_set_can_focus(app.area, TRUE);
    gtk_widget_add_events(app.area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                        GDK_POINTER_MOTION_MASK | GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK);

    g_signal_connect(app.window, "destroy", G_CALLBACK(onDestroy), &app);
    g_signal_connect(app.area, "draw", G_CALLBACK(onDraw), &app);
    g_signal_connect(app.area, "button-press-event", G_CALLBACK(onButton), &app);
    g_signal_connect(app.area, "button-release-event", G_CALLBACK(onButton), &app);
    g_signal_connect(app.area, "motion-notify-event", G_CALLBACK(onMotion), &app);
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
