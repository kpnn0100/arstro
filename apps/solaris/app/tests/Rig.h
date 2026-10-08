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
#include "App.h"
#include "EmbeddedFonts.h"
#include "SolarisService.h"
#include "adapter/native/CairoTarget.h"
#include <cairo/cairo.h>
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

    inline SolarisService::Host fakeHost(const std::string &dir)
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
        h.listDir = [](const std::string &path, std::vector<arstro::solaris::BrowserEntry> &out, std::string &err) {
            if (path.find("nowhere") != std::string::npos) { err = "no such folder"; return false; }
            out = {{"Kicks", path + "/Kicks", "dir"}, {"808 kick.wav", path + "/808 kick.wav", "audio"},
                   {"open hat.wav", path + "/open hat.wav", "audio"}, {"bass loop 128.wav", path + "/bass loop 128.wav", "audio"}};
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
        h.settingsPath = dir + "/settings.txt";
        h.recentsPath = dir + "/recents";
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

        /** `name` picks a scratch folder, emptied: no songs, settings or recents from a previous run. */
        Rig(const std::string &name, int w_, int h_) : dir(scratch(name)), w(w_), h(h_)
        {
            // a fresh folder every run: songs, settings and recents from the last run would change the story
            for (const auto &e : fs::directory_iterator(dir)) fs::remove_all(e.path());
            svc = std::make_unique<SolarisService>(fakeHost(dir));
            arstro::solaris_ui::AppHooks hooks;
            hooks.model = [this]() -> const arstro::solaris::AppModel & { return svc->model(); };
            hooks.dispatch = [this](const std::string &line, std::string &err) {
                sent.push_back(line);
                return svc->dispatchText(line, err);
            };
            app = std::make_unique<arstro::solaris_ui::App>(hooks, w, h);
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
        void drag(double x0, double y0, double x1, double y1, int steps = 8, bool releaseIt = true)
        {
            app->pointer(1, x0, y0, 0, now);
            app->pointer(0, x0, y0, 0, now);
            frame();
            for (int k = 1; k <= steps; ++k)
            {
                app->pointer(1, x0 + (x1 - x0) * k / steps, y0 + (y1 - y0) * k / steps, 0, now);
                frame();
            }
            if (releaseIt) { app->pointer(2, x1, y1, 0, now); frame(); }
        }
        void key(int code, bool ctrl = false)
        {
            artboard::KeyEvent e;
            e.type = artboard::KeyEvent::Type::Down;
            e.keyCode = code;
            e.ctrl = ctrl;
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
