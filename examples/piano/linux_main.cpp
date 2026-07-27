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
#include <sched.h>
#include <pthread.h>
#include <sys/mman.h>
#include <cerrno>
#include <chrono>
#include <algorithm>
#include <thread>
#include <vector>
#include <map>
#include <utility>
#include <cstdio>

using arstro::examples::PianoApp;

namespace
{
    constexpr int kW = 800, kH = 340;
    constexpr unsigned kRate = 48000;
    // Latency, not throughput, is what makes a software piano feel wrong: a
    // pianist notices past ~15 ms between keypress and sound. This app originally
    // asked ALSA for a 50 ms buffer with 512-frame periods (~50-60 ms), which was
    // the whole reason it felt laggy.
    //
    // The DSP has plenty of room: measured worst case, a block containing a
    // six-note chord costs 1.1 ms of a 2.67 ms budget at 128 frames (41 %). What a
    // very small buffer actually runs out of is not CPU but SCHEDULING slack — a
    // normal-priority thread sharing a laptop with GTK/Cairo redraw and cpufreq
    // will occasionally not be run for several ms, and at a 10 ms buffer that is
    // an underrun. (It was, which is why these numbers are no longer 10 ms.)
    //
    // So: keep the buffer modest but give it real cushion (4 periods), and ask for
    // real-time scheduling, which is the thing that actually lets a small buffer
    // work. Still ~2.4x lower latency than the original. Push it lower with
    // ARSTRO_PIANO_FRAMES / ARSTRO_PIANO_LATENCY_US once RT priority is granted.
    // Sized for the common desktop case, which is what this machine is: rtprio
    // limit 0 (so SCHED_FIFO is refused) AND PulseAudio between us and the
    // hardware, whose ALSA plugin adds its own scheduling and does not honour
    // very small buffers for a non-realtime client. 30 ms is comfortably
    // glitch-free there and still 40 % better than the 50 ms this started at.
    //
    // To go lower, remove one of those two constraints:
    //   * grant rtprio (add yourself to a group with an rtprio limit, e.g.
    //     /etc/security/limits.d/audio.conf: "@audio - rtprio 95"), then
    //     ARSTRO_PIANO_LATENCY_US=8000 is realistic; or
    //   * bypass PulseAudio: ARSTRO_PIANO_DEVICE=plughw:0
    constexpr int kAudioFrames = 256;         // ~5.33 ms period
    // Two default buffers, picked at runtime by whether SCHED_FIFO was granted.
    // The DSP is NOT the constraint — measured worst case on a fast desktop is a
    // 1.8 ms render of a 5.33 ms period (3x headroom, zero missed deadlines over
    // 20 s). Underruns are purely scheduling: a normal-priority (rtprio 0) audio
    // thread can be denied the CPU for several ms by the UI / cpufreq / a sound
    // server, and at a small buffer that is an xrun. With RT priority the thread
    // wins that race and a small buffer is safe; without it the only defence is a
    // deeper buffer, so we widen it rather than glitch. (ARSTRO_PIANO_LATENCY_US
    // overrides either.)
    constexpr unsigned kLatencyUsRealtime = 30000; // ~30 ms — safe once SCHED_FIFO is granted
    constexpr unsigned kLatencyUsShared = 60000;   // ~60 ms — glitch-safe on a preempted thread

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
        // Voicing sliders, for the Reset button: (GtkScale, default value).
        std::vector<std::pair<GtkWidget *, double>> voicingSliders;
    };

    // Per-slider context: which App and which PianoEngine::Tune param it drives, plus
    // the little value-readout label to update. One `new`ed per slider; lives for the
    // process (a demo tool — no teardown bookkeeping needed).
    struct SliderCtx
    {
        App *app;
        int param;
        GtkWidget *valueLabel;
        const char *unit;
    };

    void formatTuneValue(char *buf, size_t n, double v, const char *unit)
    {
        if (unit && unit[0])
            std::snprintf(buf, n, "%.3g %s", v, unit);
        else
            std::snprintf(buf, n, "%.3g", v);
    }

    void onSliderChanged(GtkRange *range, gpointer user)
    {
        auto *c = static_cast<SliderCtx *>(user);
        const double v = gtk_range_get_value(range);
        c->app->piano.setTuning(c->param, v); // enqueued to the audio thread (SPSC)
        char buf[48];
        formatTuneValue(buf, sizeof buf, v, c->unit);
        gtk_label_set_text(GTK_LABEL(c->valueLabel), buf);
    }

    void onResetVoicing(GtkButton *, gpointer user)
    {
        auto *a = static_cast<App *>(user);
        for (auto &s : a->voicingSliders)
            gtk_range_set_value(GTK_RANGE(s.first), s.second); // fires value-changed -> setTuning
    }

    // Builds the voicing panel: one labelled slider per PianoEngine::Tune param, laid
    // out from the engine's own metadata so the control list never drifts from the DSP.
    GtkWidget *buildVoicingPanel(App &app)
    {
        GtkWidget *grid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(grid), 2);
        gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
        gtk_widget_set_margin_start(grid, 10);
        gtk_widget_set_margin_end(grid, 10);
        gtk_widget_set_margin_top(grid, 8);

        int row = 0;
        GtkWidget *title = gtk_label_new(nullptr);
        gtk_label_set_markup(GTK_LABEL(title), "<b>VOICING</b>  — live, affects held notes too");
        gtk_widget_set_halign(title, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(grid), title, 0, row++, 3, 1);

        for (int p = 0; p < arstro::PianoEngine::TuneCount; ++p)
        {
            const auto spec = arstro::PianoEngine::tuneSpec(p);
            const double cur = spec.def; // a fresh engine is at the neutral defaults

            GtkWidget *name = gtk_label_new(spec.name);
            gtk_widget_set_halign(name, GTK_ALIGN_START);

            GtkWidget *scale = gtk_scale_new_with_range(
                GTK_ORIENTATION_HORIZONTAL, spec.min, spec.max,
                (spec.max - spec.min) / 200.0);
            gtk_range_set_value(GTK_RANGE(scale), cur);
            gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
            gtk_widget_set_hexpand(scale, TRUE);
            gtk_widget_set_size_request(scale, 180, -1);

            GtkWidget *val = gtk_label_new(nullptr);
            gtk_widget_set_halign(val, GTK_ALIGN_END);
            gtk_widget_set_size_request(val, 72, -1);
            char buf[48];
            formatTuneValue(buf, sizeof buf, cur, spec.unit);
            gtk_label_set_text(GTK_LABEL(val), buf);

            auto *ctx = new SliderCtx{&app, p, val, spec.unit};
            g_signal_connect(scale, "value-changed", G_CALLBACK(onSliderChanged), ctx);

            gtk_grid_attach(GTK_GRID(grid), name, 0, row, 1, 1);
            gtk_grid_attach(GTK_GRID(grid), scale, 1, row, 1, 1);
            gtk_grid_attach(GTK_GRID(grid), val, 2, row, 1, 1);
            ++row;
            app.voicingSliders.emplace_back(scale, spec.def);
        }

        GtkWidget *reset = gtk_button_new_with_label("Reset voicing");
        g_signal_connect(reset, "clicked", G_CALLBACK(onResetVoicing), &app);
        gtk_grid_attach(GTK_GRID(grid), reset, 0, row++, 3, 1);

        GtkWidget *scroller = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                       GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_container_add(GTK_CONTAINER(scroller), grid);
        gtk_widget_set_size_request(scroller, 340, -1);
        return scroller;
    }

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

    // Ask the kernel to schedule this thread as real-time. This is the fix that
    // actually makes a small buffer safe: without it the audio thread competes
    // with the UI on equal terms and a few ms of jitter becomes an audible glitch.
    // Requires rtprio limits (usually membership of the `audio` group, or
    // /etc/security/limits.d/audio.conf). Degrades silently to normal priority —
    // which is why the default buffer above is sized to survive without it.
    bool requestRealtimePriority()
    {
        sched_param sp{};
        const int policy = SCHED_FIFO;
        const int lo = sched_get_priority_min(policy), hi = sched_get_priority_max(policy);
        // Mid-range: high enough to beat the UI, low enough to stay under drivers.
        sp.sched_priority = lo + (hi - lo) / 2;
        if (pthread_setschedparam(pthread_self(), policy, &sp) == 0)
        {
            g_message("piano: audio thread running SCHED_FIFO at priority %d", sp.sched_priority);
            return true;
        }
        g_message("piano: no real-time priority (needs rtprio limits, e.g. add yourself to the "
                  "`audio` group or /etc/security/limits.d/audio.conf: \"@audio - rtprio 95\") — "
                  "using the wider shared-scheduler buffer instead");
        return false;
    }

    // Pin the process's pages in RAM so the audio thread can never stall on a major
    // page fault mid-render — a classic, silent cause of xruns that is independent of
    // scheduling priority (it bites even a SCHED_FIFO thread the first time it touches
    // a page). Best-effort: needs an RLIMIT_MEMLOCK budget, and degrades to a warning.
    void lockMemory()
    {
        if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0)
            g_message("piano: could not lock memory (RLIMIT_MEMLOCK too low) — harmless, "
                      "but a page fault could cause an occasional glitch");
    }

    void audioLoop(App *a)
    {
        const bool realtime = requestRealtimePriority();
        lockMemory();
        const char *device = std::getenv("ARSTRO_PIANO_DEVICE");
        if (!device || !*device)
            device = "default";
        snd_pcm_t *pcm = nullptr;
        if (snd_pcm_open(&pcm, device, SND_PCM_STREAM_PLAYBACK, 0) < 0)
        {
            g_warning("piano: cannot open ALSA device '%s' — running silent", device);
            return;
        }
        int frames = kAudioFrames;
        // Without real-time priority the audio thread can be preempted for several ms,
        // so it needs the deeper buffer; with it, the small one is safe. An explicit
        // ARSTRO_PIANO_LATENCY_US overrides the choice.
        unsigned latencyUs = realtime ? kLatencyUsRealtime : kLatencyUsShared;
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
        g_message("piano: audio on '%s', %d frames/period, ~%.1f ms buffer",
                  device, frames, latencyUs / 1000.0);
        std::vector<float> buf((size_t)frames * 2);

        // Prefill so the very first periods are never starved while the render
        // thread is still warming up (first-touch page faults, cold caches).
        std::fill(buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < 2; ++i)
            snd_pcm_writei(pcm, buf.data(), frames);

        // Count underruns instead of recovering silently: a glitch you cannot see
        // is a glitch you cannot tune away. Reported at most once a second.
        long xruns = 0, reported = 0;
        auto lastReport = std::chrono::steady_clock::now();
        while (a->running.load(std::memory_order_relaxed))
        {
            a->piano.renderAudio(buf.data(), frames);
            snd_pcm_sframes_t w = snd_pcm_writei(pcm, buf.data(), frames);
            if (w < 0)
            {
                if (w == -EPIPE)
                    ++xruns;
                snd_pcm_recover(pcm, (int)w, 1); // recover from xrun/underrun
            }
            const auto now = std::chrono::steady_clock::now();
            if (xruns != reported &&
                std::chrono::duration_cast<std::chrono::seconds>(now - lastReport).count() >= 1)
            {
                g_warning("piano: %ld audio underrun(s) — raise ARSTRO_PIANO_LATENCY_US "
                          "(currently %.1f ms) or grant real-time priority",
                          xruns - reported, latencyUs / 1000.0);
                reported = xruns;
                lastReport = now;
            }
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
    // Room for the keyboard plus the voicing panel to its right.
    gtk_window_set_default_size(GTK_WINDOW(app.window), kW + 360, kH);
    gtk_window_set_resizable(GTK_WINDOW(app.window), TRUE);

    app.area = gtk_drawing_area_new();
    gtk_widget_set_size_request(app.area, kW, kH);
    gtk_widget_set_can_focus(app.area, TRUE);
    gtk_widget_add_events(app.area, GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK);

    g_signal_connect(app.window, "destroy", G_CALLBACK(onDestroy), &app);
    g_signal_connect(app.area, "draw", G_CALLBACK(onDraw), &app);
    g_signal_connect(app.window, "key-press-event", G_CALLBACK(onKeyPress), &app);
    g_signal_connect(app.window, "key-release-event", G_CALLBACK(onKeyRelease), &app);

    // Keyboard on the left, the live voicing/tuning panel on the right.
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(hbox), app.area, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), buildVoicingPanel(app), TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(app.window), hbox);
    gtk_widget_show_all(app.window);
    gtk_widget_grab_focus(app.area);

    app.audioThread = std::thread(audioLoop, &app);
    // 33 ms (~30 fps), not 16 ms (~60 fps). Every tick repaints the whole
    // keyboard through Cairo's software rasteriser on the UI thread, and that
    // thread shares a core with a NON-realtime audio thread (rtprio is usually 0
    // on a desktop). Halving the repaint rate halves that contention, and a
    // keyboard with a decaying level meter has nothing that needs 60 fps.
    // Override with ARSTRO_PIANO_UI_MS.
    int uiIntervalMs = 33;
    if (const char *e = std::getenv("ARSTRO_PIANO_UI_MS"))
    {
        const int v = std::atoi(e);
        if (v >= 8 && v <= 200) uiIntervalMs = v;
    }
    g_timeout_add((guint)uiIntervalMs, onTick, &app);
    gtk_main();

    app.running.store(false);
    if (app.audioThread.joinable())
        app.audioThread.join();
    return 0;
}
