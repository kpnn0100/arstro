/*
 *  interstellar_v1 tests — Rig: the real App over a FakeService, drawn into a Cairo image surface.
 *
 *  Shared by the shot harness and the UI tests so both drive the app the same way:
 *   * a fixed 16 ms clock — the frame INDEX decides where an animation is, never the machine;
 *   * ONE CairoTarget for the rig's life, re-bound to each frame's context (image ids live in it);
 *   * real text metrics: the embedded faces are registered by the harness main before any rig;
 *   * input through `App::pointer` with the host's codes (0 down, 1 move, 2 up), so a test clicks
 *     exactly the way the window will.
 */
#pragma once
#include "App.h"
#include "adapter/native/CairoTarget.h"
#include "FakeService.h"
#include <cairo/cairo.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace istest
{
    class Rig
    {
    public:
        static constexpr double kFrameMs = 16.0;
        FakeService svc;
        std::unique_ptr<arstro::interstellar_v1::App> app;
        artboard::CairoTarget target;
        int w, h;
        double now = 1000.0;

        Rig(int w_, int h_, const std::function<void(FakeService &)> &setup) : w(w_), h(h_)
        {
            setup(svc);
            app = std::make_unique<arstro::interstellar_v1::App>(svc.hooks(), w, h);
            app->setHomeClock(FakeService::kNow());
            alloc();
        }
        ~Rig() { release(); }
        Rig(const Rig &) = delete;
        Rig &operator=(const Rig &) = delete;

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
        void click(double x, double y)
        {
            app->pointer(1, x, y, 0, now);
            app->pointer(0, x, y, 0, now);
            app->pointer(2, x, y, 0, now + 40.0);
        }
        void press(double x, double y) { app->pointer(1, x, y, 0, now); app->pointer(0, x, y, 0, now); }
        void dragTo(double x, double y) { app->pointer(1, x, y, 0, now); }
        void releaseAt(double x, double y) { app->pointer(2, x, y, 0, now); }
        /** press → several moves → release, one frame between each, as a hand would. */
        void drag(double x0, double y0, double x1, double y1, int steps = 8)
        {
            press(x0, y0);
            frame();
            for (int k = 1; k <= steps; ++k)
            {
                dragTo(x0 + (x1 - x0) * k / steps, y0 + (y1 - y0) * k / steps);
                frame();
            }
            releaseAt(x1, y1);
            frame();
        }
        void typeText(const std::string &s)
        {
            for (char c : s)
            {
                artboard::KeyEvent e;
                e.type = artboard::KeyEvent::Type::Text;
                e.text = std::string(1, c);
                app->key(e);
            }
        }
        void key(int code)
        {
            artboard::KeyEvent e;
            e.type = artboard::KeyEvent::Type::Down;
            e.keyCode = code;
            app->key(e);
        }

        bool write(const std::string &path) { return cairo_surface_write_to_png(mSurf, path.c_str()) == CAIRO_STATUS_SUCCESS; }
        bool uniform() const
        {
            const unsigned char *d = cairo_image_surface_get_data(mSurf);
            const int stride = cairo_image_surface_get_stride(mSurf);
            const uint32_t first = *(const uint32_t *)d;
            for (int y = 0; y < h; y += 2)
                for (int x = 0; x < w; x += 2)
                    if (*(const uint32_t *)(d + y * stride + x * 4) != first) return false;
            return true;
        }
        /** The pixel at (x, y) as 0xRRGGBB. */
        uint32_t pixel(int x, int y) const
        {
            const unsigned char *d = cairo_image_surface_get_data(mSurf);
            const uint32_t v = *(const uint32_t *)(d + y * cairo_image_surface_get_stride(mSurf) + x * 4);
            return v & 0xFFFFFF;
        }

    private:
        void alloc()
        {
            mSurf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
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

    inline artboard::Point world(const artboard::Segment &s, double lx, double ly) { return s.worldTransform().apply(artboard::Point{lx, ly}); }
    inline artboard::Point centre(const artboard::Segment &s, const artboard::Rect &local)
    {
        return world(s, local.x + local.w * 0.5, local.y + local.h * 0.5);
    }
    inline artboard::Rect worldRect(const artboard::Segment &s)
    {
        const artboard::Point a = world(s, 0, 0);
        return artboard::Rect{a.x, a.y, s.width.value(), s.height.value()};
    }
}
