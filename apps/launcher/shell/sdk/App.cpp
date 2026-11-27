#include "App.h"
#include "../kit/Fonts.h"
#include "../kit/Theme.h"
#include "../protocol/Channel.h"
#include "adapter/native/CairoTarget.h"
#include <cairo/cairo.h>
#include <cstdio>
#include <cstdlib>
#include <poll.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

namespace arstro
{
namespace app
{
    namespace
    {
        double monotonicMs()
        {
            timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
        }

        /** How long after the last reason to draw the SDK keeps drawing: longer than the longest
         *  tween in the kit, so an animation a tap started always plays to its end. */
        constexpr double kTailMs = 700.0;

        struct Runtime
        {
            wire::Channel channel;
            Context ctx;
            bool configured = false, resumed = false, awaitingDone = false, running = true, continuous = false;
            double drawUntil = 0.0;
            // the shared pixel buffer
            int shmFd = -1;
            uint8_t *pixels = nullptr;
            size_t mapped = 0;
            int pxW = 0, pxH = 0, stride = 0;
            cairo_surface_t *surface = nullptr;

            void dropBuffer()
            {
                if (surface) cairo_surface_destroy(surface);
                if (pixels) munmap(pixels, mapped);
                if (shmFd >= 0) close(shmFd);
                surface = nullptr;
                pixels = nullptr;
                shmFd = -1;
            }

            bool makeBuffer(int w, int h)
            {
                dropBuffer();
                pxW = w;
                pxH = h;
                stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, w);
                mapped = (size_t)stride * h;
                shmFd = wire::createShm(mapped);
                if (shmFd < 0) return false;
                pixels = (uint8_t *)mmap(nullptr, mapped, PROT_READ | PROT_WRITE, MAP_SHARED, shmFd, 0);
                if (pixels == MAP_FAILED) { pixels = nullptr; return false; }
                surface = cairo_image_surface_create_for_data(pixels, CAIRO_FORMAT_ARGB32, w, h, stride);
                wire::BufferInfo info{w, h, stride};
                return channel.send(wire::Msg::Buffer, info, shmFd);
            }
        };

        Runtime *gRuntime = nullptr;
    }

    void Context::invalidate() { if (gRuntime) gRuntime->drawUntil = monotonicMs() + kTailMs; }
    void Context::setContinuous(bool on) { if (gRuntime) { gRuntime->continuous = on; invalidate(); } }
    void Context::finish() { if (gRuntime) gRuntime->channel.send(wire::Msg::Finish); }
    void Context::notify(const std::string &title, const std::string &body)
    {
        if (gRuntime) gRuntime->channel.sendText(wire::Msg::Notify, title + "\n" + body);
    }
    void Context::request(const std::string &line)
    {
        if (gRuntime) gRuntime->channel.sendText(wire::Msg::Request, line);
    }

    int run(App &app, const std::string &appId)
    {
        const char *sock = std::getenv("ARSTRO_SHELL_SOCKET");
        if (!sock || !*sock)
        {
            std::fprintf(stderr, "%s: this is an Arstro touch app. Start it from the shell (arstro-shell); "
                                 "ARSTRO_SHELL_SOCKET is not set.\n", appId.c_str());
            return 2;
        }
        Runtime rt;
        gRuntime = &rt;
        rt.channel = wire::Channel(wire::connectTo(sock));
        if (!rt.channel.open())
        {
            std::fprintf(stderr, "%s: cannot reach the shell at %s\n", appId.c_str(), sock);
            return 2;
        }
        wire::Hello hello{};
        hello.version = wire::kVersion;
        hello.task = std::getenv("ARSTRO_TASK") ? std::atoi(std::getenv("ARSTRO_TASK")) : 0;
        std::snprintf(hello.appId, sizeof hello.appId, "%s", appId.c_str());
        rt.channel.send(wire::Msg::Hello, hello);

        kit::registerFonts();
        artboard::CairoTarget target;
        artboard::GestureRecognizer recognizer;
        recognizer.setDoubleClickMs(0.0);     // touch has no double-click: every tap is a tap
        recognizer.setDragThreshold(8.0);     // dp — a fingertip wobbles more than a mouse
        bool created = false;
        recognizer.setSink([&](const artboard::Gesture &g) { if (created) app.root().onGesture(g); });

        std::vector<wire::Message> inbox;
        while (rt.running)
        {
            // Sleep until there is something to do. While a frame is owed the wait is short, and
            // while one is in flight the shell's FrameDone is what wakes us — that message IS the
            // frame clock, so the app never draws faster than the screen shows.
            const double now0 = monotonicMs();
            const bool wantsFrame = rt.configured && rt.resumed && (rt.continuous || rt.drawUntil > now0);
            pollfd pfd{rt.channel.fd(), POLLIN, 0};
            ::poll(&pfd, 1, wantsFrame ? (rt.awaitingDone ? 100 : 0) : 500);

            inbox.clear();
            const bool alive = rt.channel.pump(inbox);
            for (const wire::Message &m : inbox)
            {
                switch (m.type)
                {
                case wire::Msg::Configure:
                {
                    wire::Configure c;
                    if (!m.as(c) || c.width <= 0 || c.height <= 0 || c.scale <= 0) break;
                    rt.ctx.scale = c.scale;
                    rt.ctx.width = c.width / c.scale;
                    rt.ctx.height = c.height / c.scale;
                    rt.ctx.insetTop = c.insetTop / c.scale;
                    rt.ctx.insetBottom = c.insetBottom / c.scale;
                    rt.ctx.accent = artboard::Color::hex(c.accent);
                    kit::palette::setAccent(rt.ctx.accent);
                    artboard::setReducedMotion(c.reducedMotion != 0);
                    if (c.width != rt.pxW || c.height != rt.pxH)
                    {
                        if (!rt.makeBuffer(c.width, c.height)) { rt.running = false; break; }
                        rt.awaitingDone = false;
                    }
                    rt.configured = true;
                    if (!created)
                    {
                        rt.ctx.nowMs = monotonicMs();
                        app.create(rt.ctx);
                        created = true;
                    }
                    rt.ctx.invalidate();
                    break;
                }
                case wire::Msg::FrameDone: rt.awaitingDone = false; break;
                case wire::Msg::Touch:
                {
                    wire::TouchEvent e;
                    if (!m.as(e) || e.id != 0) break;  // one finger reaches Artboard today (ledger: multi-touch)
                    artboard::RawPointer p{};
                    p.touch = true;
                    p.timeMs = monotonicMs();
                    p.pos = artboard::Point{e.x / rt.ctx.scale, e.y / rt.ctx.scale};
                    if (e.phase == wire::TouchPhase::Down) p.kind = artboard::RawPointer::Kind::Down;
                    else if (e.phase == wire::TouchPhase::Move) p.kind = artboard::RawPointer::Kind::Move;
                    else
                    {
                        p.kind = artboard::RawPointer::Kind::Up;
                        // A cancelled touch was taken by the system: release far outside, so the
                        // control un-presses and nothing under the finger is clicked.
                        if (e.phase == wire::TouchPhase::Cancel) p.pos = artboard::Point{-1e7, -1e7};
                    }
                    recognizer.feed(p);
                    rt.ctx.invalidate();
                    break;
                }
                case wire::Msg::Lifecycle:
                {
                    wire::Lifecycle l;
                    if (!m.as(l)) break;
                    if (l.state == wire::LifecycleState::Resume && !rt.resumed)
                    {
                        rt.resumed = true;
                        if (created) app.resumed();
                        rt.ctx.invalidate();
                    }
                    else if (l.state == wire::LifecycleState::Pause && rt.resumed)
                    {
                        rt.resumed = false;
                        if (created) app.paused();
                    }
                    else if (l.state == wire::LifecycleState::Stop)
                        rt.running = false;
                    break;
                }
                case wire::Msg::Back:
                {
                    wire::BackResult r{created && app.back() ? 1 : 0};
                    rt.channel.send(wire::Msg::BackResult, r);
                    rt.ctx.invalidate();
                    break;
                }
                case wire::Msg::ShellEvent:
                    if (created) app.shellEvent(m.text());
                    rt.ctx.invalidate();
                    break;
                default: break;
                }
            }
            if (!alive) break;  // the shell is gone: an app has no reason to outlive it

            const double now = monotonicMs();
            if (!created || !rt.configured || !rt.resumed || rt.awaitingDone || !rt.surface) continue;
            if (!rt.continuous && rt.drawUntil <= now) continue;

            rt.ctx.nowMs = now;
            artboard::Segment &root = app.root();
            root.width.set(rt.ctx.width);
            root.height.set(rt.ctx.height);
            recognizer.advance(now);
            root.advance(now);
            app.layout(rt.ctx);

            cairo_t *cr = cairo_create(rt.surface);
            target.setContext(cr);
            const artboard::Transform dp = artboard::Transform::scaling(rt.ctx.scale, rt.ctx.scale);
            root.render(target, dp);
            root.renderOverlay(target, dp);
            target.setContext(nullptr);
            cairo_destroy(cr);
            cairo_surface_flush(rt.surface);
            rt.channel.send(wire::Msg::Commit);
            rt.awaitingDone = true;
        }
        rt.dropBuffer();
        gRuntime = nullptr;
        return 0;
    }
}
}
