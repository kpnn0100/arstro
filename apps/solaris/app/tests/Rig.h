/*
 *  solaris_ui tests — Rig: the REAL App over the REAL SolarisService, rendered headlessly.
 *
 *  Interstellar's shots run over a fake service because its real one hosts all of Cosmo. Solaris's
 *  service is headless and cheap, so the rig binds the app's hooks to a real one — every shot and
 *  every UI test is also a check that the window and the service agree (R-UI-6). The host's
 *  machine-facing functions are faked deterministically: three devices, a folder listing, a sine
 *  for any audio file, settings and recents in a scratch folder, no audio output.
 *
 *  The clock is a fixed 16 ms tick, never the wall clock: a mid-transition frame is reproducible only
 *  when the frame INDEX decides where it is. One CairoTarget per rig, re-bound each frame.
 */
#pragma once
#include <chrono>
#include <thread>
#include "App.h"
#include "EmbeddedFonts.h"
#include "SolarisService.h"
#include "adapter/native/CairoTarget.h"
#include <cairo/cairo.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace sltest
{
    namespace fs = std::filesystem;
    using arstro::solaris::SolarisService;

    /** A widget-local rect → window coordinates (the App draws its tree at the identity transform). */
    inline artboard::Rect world(const artboard::Segment &s, const artboard::Rect &r)
    {
        const artboard::Point o = s.worldTransform().apply(artboard::Point{0, 0});
        return artboard::Rect{r.x + o.x, r.y + o.y, r.w, r.h};
    }
    inline double cx(const artboard::Rect &r) { return r.x + r.w * 0.5; }
    inline double cy(const artboard::Rect &r) { return r.y + r.h * 0.5; }

    inline std::string scratch(const std::string &sub)
    {
        const char *t = std::getenv("SOLARIS_UI_TEST_DIR");
        fs::path d = (t ? fs::path(t) : fs::temp_directory_path() / "solaris_ui_tests") / sub;
        fs::create_directories(d);
        return d.string();
    }

    /** R-VST-6: other makers' plugins, in process behind the host's seam — Sweet Pad (an instrument: a quiet sine per
     *  held note) and Air Verb (an effect passing its input). One named by `rigPluginCrash()` has crashed — offline
     *  from then on; its own editor is a flag (no window). */
    inline std::string &rigPluginCrash()
    {
        static std::string uid;
        return uid;
    }
    class RigPlugin : public arstro::solaris::IPluginInstance
    {
    public:
        RigPlugin(const std::string &uid, double rate) : mUid(uid), mRate(rate)
        {
            mInst = uid == "0123456789ABCDEF0123456789ABCDEF";
            mParams = mInst ? std::vector<arstro::solaris::PluginParamInfo>{{1, "Level", "", 0.5, 0}, {2, "Brightness", "%", 0.25, 0}}
                            : std::vector<arstro::solaris::PluginParamInfo>{{7, "Mix", "%", 0.3, 0}, {8, "Size", "", 0.6, 0}, {9, "Damping", "", 0.5, 0}};
        }
        bool isInstrument() const override { return mInst; }
        int latency() const override { return 0; }
        const std::vector<arstro::solaris::PluginParamInfo> &params() const override { return mParams; }
        bool process(const float *const *in, float *const *out, int frames, bool reset, const arstro::solaris::PluginNote *notes, int nNotes,
                     const arstro::solaris::PluginChange *, int, double) override
        {
            if (offline()) return false;
            if (reset) mHeld = 0;
            for (int k = 0; k < nNotes; ++k) mHeld += notes[k].on ? 1 : -1;
            for (int i = 0; i < frames; ++i)
                for (int c = 0; c < 2; ++c)
                    out[c][i] = mInst ? (mHeld > 0 ? (float)(0.2 * std::sin(mPhase)) : 0.0f) : (in && in[c] ? in[c][i] : 0.0f);
            mPhase += 2.0 * M_PI * 440.0 / mRate * frames;
            return true;
        }
        bool offline() const override { return mDead || rigPluginCrash() == mUid; } // named: it has crashed, at once
        std::string why() const override { return offline() ? "its process ended (Segmentation fault)" : std::string(); }
        bool getState(std::string &s, std::string &) override { s = "rig"; return true; }
        bool setState(const std::string &, std::string &) override { return true; }
        bool openEditor(const std::string &, std::string &) override { mEditor = true; return true; }
        bool closeEditor(std::string &) override { mEditor = false; return true; }
        bool editorOpen() const override { return mEditor && !offline(); }

    private:
        std::string mUid;
        double mRate, mPhase = 0.0;
        bool mInst = false, mDead = false, mEditor = false;
        int mHeld = 0;
        std::vector<arstro::solaris::PluginParamInfo> mParams;
    };

    inline SolarisService::Host fakeHost(const std::string &dir, bool audio = true)
    {
        SolarisService::Host h;
        h.decodeAudio = [](const std::string &, int rate, arstro::solaris::engine::Pcm &out, std::string &) {
            out.channels = 2;
            out.frames = rate / 2;
            out.samples.resize((size_t)out.frames * 2);
            for (long long i = 0; i < out.frames; ++i)
                out.samples[(size_t)i * 2] = out.samples[(size_t)i * 2 + 1] = (float)(0.4 * std::sin(i * 0.05));
            return true;
        };
        h.writeWav = [](const std::string &, const std::vector<std::vector<float>> &, int, int, std::string &) { return true; };
        // R-VIDEO-1: a picture is any .mp4/.mov/.mkv — ten seconds, 640×360 at 25 frames a second
        h.probeVideo = [](const std::string &path, arstro::solaris::VideoInfo &vi, std::string &err) {
            const auto ext = fs::path(path).extension().string();
            if (ext != ".mp4" && ext != ".mov" && ext != ".mkv") { err = "it has no picture"; return false; }
            vi.duration = 10.0;
            vi.width = 640;
            vi.height = 360;
            vi.fps = 25.0;
            return true;
        };
        h.muxVideo = [](const std::string &, double, const std::vector<std::vector<float>> &, int, const std::string &, std::string &) { return true; };
        h.listDir = [](const std::string &path, std::vector<arstro::solaris::BrowserEntry> &out, std::string &err) {
            if (path.find("nowhere") != std::string::npos) { err = "no such folder"; return false; }
            out = {{"Kicks", path + "/Kicks", "dir"}, {"808 kick.wav", path + "/808 kick.wav", "audio"},
                   {"open hat.wav", path + "/open hat.wav", "audio"}, {"bass loop 128.wav", path + "/bass loop 128.wav", "audio"},
                   {"groove.mid", path + "/groove.mid", "midi"}}; // R-MIDI-1 — last, so no other row moves
            return true;
        };
        h.listDevices = [](std::vector<arstro::solaris::DeviceInfo> &out, std::string &) {
            out = {{"alsa_output.analog", "Built-in Audio Analog Stereo", "out", 2, 48000},
                   {"alsa_output.hdmi", "HDMI / DisplayPort", "out", 2, 48000},
                   {"usb_interface", "Scarlett 2i2 USB", "out", 2, 48000},
                   {"alsa_input.analog", "Built-in Audio Analog Stereo", "in", 2, 48000},
                   {"usb_mic", "Brio 100 Mono", "in", 1, 48000}};
            return true;
        };
        // an output device that keeps time and plays nowhere (the browser's audition, R-EDM-9)
        struct SilentOut : arstro::solaris::IAudioOut
        {
            bool open(const std::string &, int, int, int, std::string &) override { return true; }
            bool write(const float *, int n) override
            {
                // a quarter speed: a half-second sample previews for two real seconds, so a test sees it playing
                std::this_thread::sleep_for(std::chrono::microseconds((long long)n * 4000000 / 48000));
                return true;
            }
            double latency() override { return 0.0; }
        };
        if (audio) h.audioOut = [] { return std::unique_ptr<arstro::solaris::IAudioOut>(new SilentOut()); };
        // R-MIDI-2: a keyboard that is there and plays nothing — `strip arm` works, the monitor runs silent
        struct QuietMidi : arstro::solaris::IMidiIn
        {
            std::vector<std::string> on;
            bool open(const std::vector<std::string> &ports, std::function<void(const arstro::solaris::MidiEvent &)>, std::string &) override
            {
                on = ports.empty() ? std::vector<std::string>{"Keystation 49"} : ports;
                return true;
            }
            void close() override {}
            std::vector<std::string> listening() const override { return on; }
        };
        h.listMidiInputs = [](std::vector<arstro::solaris::MidiPort> &out, std::string &) {
            out = {arstro::solaris::MidiPort{"Keystation 49", "Keystation 49"}};
            return true;
        };
        h.midiIn = [] { return std::unique_ptr<arstro::solaris::IMidiIn>(new QuietMidi()); };
        /** R-CTRL-2: a surface's output that takes anything and goes nowhere. */
        struct QuietOut : arstro::solaris::IMidiOut
        {
            bool open(const std::string &, std::string &) override { return true; }
            void send(const std::vector<uint8_t> &) override {}
            void close() override {}
        };
        h.midiOut = [] { return std::unique_ptr<arstro::solaris::IMidiOut>(new QuietOut()); };
        /** R-REC-1: a capture device that hears nothing — enough to arm an audio line, monitor it, record silence. */
        struct QuietIn : arstro::solaris::IAudioIn
        {
            int ch = 2;
            bool open(const std::string &, int, int channels, int, std::string &) override { ch = channels; return true; }
            int read(float *x, int n) override { std::fill(x, x + (size_t)n * (size_t)ch, 0.0f); return n; }
            double latency() override { return 0.0; }
        };
        if (audio) h.audioIn = [] { return std::unique_ptr<arstro::solaris::IAudioIn>(new QuietIn()); };
        h.settingsPath = dir + "/settings.txt";
        h.recentsPath = dir + "/recents";
        h.presetsPath = dir + "/presets/user";       // R-PRESET-1: the rig's own, never the machine's
        h.factoryPresetsPath = dir + "/presets/factory";
        h.templatesPath = dir + "/templates/user";   // R-PRESET-2: likewise
        h.factoryTemplatesPath = dir + "/templates/factory";
        h.libraryPath = dir + "/library";                 // R-PRESET-3: likewise
        h.pluginsPath = dir + "/plugins";                 // R-VST-6: likewise — and two plugins, played in process
        h.scanPlugins = [](std::vector<arstro::solaris::PluginModel> &out, std::vector<std::string> &, std::string &) {
            out = {arstro::solaris::PluginModel{"0123456789ABCDEF0123456789ABCDEF", "Sweet Pad", "Acme", "instrument", "/vst3/Sweet.vst3"},
                   arstro::solaris::PluginModel{"FEDCBA9876543210FEDCBA9876543210", "Air Verb", "Acme", "effect", "/vst3/Air.vst3"}};
            return true;
        };
        h.loadPlugin = [](const std::string &, const std::string &uid, double rate, int, std::string &) -> std::unique_ptr<arstro::solaris::IPluginInstance> {
            return std::make_unique<RigPlugin>(uid, rate);
        };
        return h;
    }

    class Rig
    {
    public:
        static constexpr double kFrameMs = 16.0;
        std::string dir;
        std::unique_ptr<SolarisService> svc;
        std::unique_ptr<arstro::solaris_ui::App> app;
        artboard::CairoTarget target;
        int w, h;
        double now = 1000.0;
        std::vector<std::string> sent;   // every line the app dispatched, in order
        std::vector<std::string> copied; // the host's clipboard: every text the app copied (R-UI-11)
        struct VideoAsk { std::string file; double t; int w, h; };
        std::vector<VideoAsk> videoAsks; // R-VIDEO-1: every frame the Video window asked the host for
        std::vector<VideoAsk> thumbAsks; // … and every thumbnail the lane asked for (a decoder of its own, as the host's)

        /** `name` picks a scratch folder, emptied: no songs, settings or recents from a previous run. */
        /** `audio` = false: a machine with no output device (a refusal to test). */
        Rig(const std::string &name, int w_, int h_, bool audio = true) : dir(scratch(name)), w(w_), h(h_)
        {
            // a fresh folder every run: songs, settings and recents from the last run would change the story
            for (const auto &e : fs::directory_iterator(dir)) fs::remove_all(e.path());
            svc = std::make_unique<SolarisService>(fakeHost(dir, audio));
            arstro::solaris_ui::AppHooks hooks;
            hooks.model = [this]() -> const arstro::solaris::AppModel & { return svc->model(); };
            hooks.dispatch = [this](const std::string &line, std::string &err) {
                sent.push_back(line);
                return svc->dispatchText(line, err);
            };
            app = std::make_unique<arstro::solaris_ui::App>(hooks, w, h);
            app->onCopy = [this](const std::string &text) { copied.push_back(text); };
            // R-VIDEO-1: the host's decoder, faked — a 16:9 picture fitted to the box: a dusk sky, an amber band whose
            // left edge is the frame's time (a second a tenth of the width), so each frame reads as its own
            auto paint = [](double t, int bw, int bh, arstro::solaris_ui::VideoImage &out) {
                const double k = std::min(bw / 640.0, bh / 360.0);
                out.width = std::max(1, (int)std::lround(640 * k));
                out.height = std::max(1, (int)std::lround(360 * k));
                out.time = std::floor(t * 25.0 + 1e-9) / 25.0;
                out.rgba.assign((size_t)out.width * (size_t)out.height * 4, 255);
                const int edge = (int)(out.width * std::fmod(out.time, 10.0) / 10.0);
                for (int y = 0; y < out.height; ++y)
                    for (int x = 0; x < out.width; ++x)
                    {
                        uint8_t *p = &out.rgba[((size_t)y * (size_t)out.width + (size_t)x) * 4];
                        const double v = (double)y / out.height;
                        const bool band = v > 0.62 && v < 0.70 && x >= edge;
                        p[0] = band ? 233 : (uint8_t)(27 + 31 * v);
                        p[1] = band ? 162 : (uint8_t)(42 - 14 * v);
                        p[2] = band ? 59 : (uint8_t)(73 + 1 * v);
                    }
                return true;
            };
            app->onVideoFrame = [this, paint](const std::string &file, double t, int bw, int bh, arstro::solaris_ui::VideoImage &out) {
                videoAsks.push_back({file, t, bw, bh});
                return paint(t, bw, bh, out);
            };
            app->onVideoThumb = [this, paint](const std::string &file, double t, int bw, int bh, arstro::solaris_ui::VideoImage &out) {
                thumbAsks.push_back({file, t, bw, bh});
                return paint(t, bw, bh, out);
            };
            alloc();
        }
        ~Rig() { release(); }
        Rig(const Rig &) = delete;
        Rig &operator=(const Rig &) = delete;

        /** A command straight to the service (setting up a state), not through the app. */
        void cmd(const std::string &line)
        {
            std::string err;
            if (!svc->dispatchText(line, err)) std::fprintf(stderr, "rig: `%s` refused: %s\n", line.c_str(), err.c_str());
        }
        /** A song's path WITHOUT the extension (`project new` adds it if missing — as the picker does). */
        std::string song(const std::string &name) const { return dir + "/" + name; }

        void resize(int nw, int nh)
        {
            release();
            w = nw; h = nh;
            alloc();
            app->setSize(w, h);
        }
        void frame()
        {
            svc->pump(); // as the window's tick does: live state (the transport, a preview) reaches the model
            cairo_save(mCr);
            cairo_set_operator(mCr, CAIRO_OPERATOR_SOURCE);
            cairo_set_source_rgb(mCr, 0, 0, 0);
            cairo_paint(mCr);
            cairo_restore(mCr);
            target.setContext(mCr);
            app->render(target, now);
            cairo_surface_flush(mSurf);
            now += kFrameMs;
        }
        void pump(double ms) { for (double t = 0; t < ms; t += kFrameMs) frame(); }
        void settle() { pump(700.0); }
        void move(double x, double y) { app->pointer(1, x, y, 0, now); }
        void click(double x, double y, int button = 0)
        {
            app->pointer(1, x, y, 0, now);
            app->pointer(0, x, y, button, now);
            app->pointer(2, x, y, button, now + 40.0);
        }
        void click(const artboard::Rect &r, int button = 0) { click(r.x + r.w * 0.5, r.y + r.h * 0.5, button); }
        /** press → several moves → release, a frame between each, as a hand would. */
        void drag(double x0, double y0, double x1, double y1, int steps = 8, bool releaseIt = true, bool shift = false)
        {
            app->pointer(1, x0, y0, 0, now, false, shift);
            app->pointer(0, x0, y0, 0, now, false, shift);
            frame();
            for (int k = 1; k <= steps; ++k)
            {
                app->pointer(1, x0 + (x1 - x0) * k / steps, y0 + (y1 - y0) * k / steps, 0, now, false, shift);
                frame();
            }
            if (releaseIt) { app->pointer(2, x1, y1, 0, now, false, shift); frame(); }
        }
        void key(int code, bool ctrl = false, bool shift = false)
        {
            artboard::KeyEvent e;
            e.type = artboard::KeyEvent::Type::Down;
            e.keyCode = code;
            e.ctrl = ctrl;
            e.shift = shift;
            app->key(e);
        }
        bool write(const std::string &path) { return cairo_surface_write_to_png(mSurf, path.c_str()) == CAIRO_STATUS_SUCCESS; }
        uint32_t pixel(int x, int y) const
        {
            const unsigned char *d = cairo_image_surface_get_data(mSurf);
            const uint32_t p = *(const uint32_t *)(d + y * cairo_image_surface_get_stride(mSurf) + x * 4);
            return p & 0xFFFFFF;
        }

    private:
        void alloc()
        {
            mSurf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
            mCr = cairo_create(mSurf);
        }
        void release()
        {
            if (mCr) cairo_destroy(mCr);
            if (mSurf) cairo_surface_destroy(mSurf);
            mCr = nullptr;
            mSurf = nullptr;
        }
        cairo_surface_t *mSurf = nullptr;
        cairo_t *mCr = nullptr;
    };
}
