#include "ShellHost.h"
#include "../kit/Fonts.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace arstro
{
namespace shell
{
    using namespace kit;
    using artboard::Color;
    using artboard::Rect;

    namespace
    {
        std::string field(const Event &e, const char *key)
        {
            for (const auto &f : e.fields)
                if (f.first == key) return f.second;
            return "";
        }
        double lerp(double a, double b, double t) { return a + (b - a) * t; }
        double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

        void mkdirs(const std::string &path)
        {
            for (size_t i = 1; i < path.size(); ++i)
                if (path[i] == '/') ::mkdir(path.substr(0, i).c_str(), 0755);
        }
    }

    // ── Layer ────────────────────────────────────────────────────────────────────────────────

    bool Layer::ensure(SDL_Renderer *r, int width, int height)
    {
        width = std::max(1, width);
        height = std::max(1, height);
        if (surface && width == w && height == h) return false;
        release();
        w = width;
        h = height;
        surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        texture = r ? SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h) : nullptr;
        if (texture)
        {
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
            SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
        }
        return true;
    }

    cairo_t *Layer::begin()
    {
        cairo_t *cr = cairo_create(surface);
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        return cr;
    }

    void Layer::end(cairo_t *cr)
    {
        cairo_destroy(cr);
        cairo_surface_flush(surface);
        if (texture)
            SDL_UpdateTexture(texture, nullptr, cairo_image_surface_get_data(surface), cairo_image_surface_get_stride(surface));
    }

    void Layer::release()
    {
        if (texture) SDL_DestroyTexture(texture);
        if (surface) cairo_surface_destroy(surface);
        texture = nullptr;
        surface = nullptr;
        w = h = 0;
    }

    bool writePng(const std::string &path, const void *pixels, int w, int h, int stride)
    {
        cairo_surface_t *s = cairo_image_surface_create_for_data((unsigned char *)pixels, CAIRO_FORMAT_RGB24, w, h, stride);
        const bool ok = cairo_surface_write_to_png(s, path.c_str()) == CAIRO_STATUS_SUCCESS;
        cairo_surface_destroy(s);
        return ok;
    }

    // ── construction ─────────────────────────────────────────────────────────────────────────

    ShellHost::ShellHost(SDL_Renderer *renderer, const HostOptions &options) : mRenderer(renderer), mOptions(options)
    {
        kit::registerFonts();
        // An app that dies with a frame half-sent must not take the shell down with SIGPIPE.
        std::signal(SIGPIPE, SIG_IGN);

        mService.subscribe([this](const Event &e) { onEvent(e); });
        const std::vector<std::string> roots = mOptions.appRoots.empty() ? defaultAppRoots() : mOptions.appRoots;
        mService.setScanner([roots] { return scanApps(roots); });

        const Dispatch send = [this](const std::string &line) { dispatch(line); };
        mHome = std::make_shared<HomeView>(send, [this](const AppInfo &a) { return iconImage(a); });
        mStatus = std::make_shared<StatusBarView>();
        mShade = std::make_shared<ShadeView>(send);
        for (artboard::GestureRecognizer *r : {&mHomeInput, &mShadeInput})
        {
            r->setDoubleClickMs(0.0);  // touch has no double-click: every tap is a tap
            r->setDragThreshold(8.0);
        }
        mHomeInput.setSink([this](const artboard::Gesture &g) { mHome->onGesture(g); });
        mShadeInput.setSink([this](const artboard::Gesture &g) { mShade->onGesture(g); });

        loadSettings();
        applyTheme();
        mService.run("apps rescan");

        mSocketPath = mOptions.socketPath;
        if (mSocketPath.empty())
        {
            const char *rt = std::getenv("XDG_RUNTIME_DIR");
            mSocketPath = std::string(rt && *rt ? rt : "/tmp") + "/arstro-shell-" + std::to_string(getpid()) + ".sock";
        }
        mListen = wire::listenOn(mSocketPath);
        if (mListen < 0) SDL_Log("arstro-shell: cannot listen on %s — apps will not be able to attach", mSocketPath.c_str());
        if (!mOptions.controlPath.empty())
        {
            mControlListen = wire::listenOn(mOptions.controlPath);
            if (mControlListen < 0) SDL_Log("arstro-shell: cannot open the control socket %s", mOptions.controlPath.c_str());
        }
    }

    ShellHost::~ShellHost()
    {
        for (auto &c : mClients)
        {
            wire::Lifecycle l{wire::LifecycleState::Stop};
            c->channel.send(wire::Msg::Lifecycle, l);
            if (c->pixels) munmap(c->pixels, c->mapped);
        }
        for (auto &kv : mTasks)
        {
            if (kv.second.pid > 0) ::kill(kv.second.pid, SIGTERM);
            releaseTask(kv.second);
        }
        if (mCanvas) cairo_surface_destroy(mCanvas);
        for (Layer *l : {&mHomeLayer, &mStatusLayer, &mShadeLayer, &mWallpaper, &mPill, &mBackBubble, &mEmptyText}) l->release();
        if (mListen >= 0) { ::close(mListen); ::unlink(mSocketPath.c_str()); }
        if (mControlListen >= 0) { ::close(mControlListen); ::unlink(mOptions.controlPath.c_str()); }
        for (ControlPeer &p : mControl) ::close(p.fd);
    }

    // ── settings and theme ───────────────────────────────────────────────────────────────────

    void ShellHost::loadSettings()
    {
        if (mOptions.configPath.empty()) return;
        std::ifstream in(mOptions.configPath);
        std::string line;
        while (std::getline(in, line))
            if (!line.empty() && line[0] != '#') mService.run("set " + line);  // an unknown or stale key is skipped
    }

    void ShellHost::saveSettings()
    {
        if (mOptions.configPath.empty()) return;
        mkdirs(mOptions.configPath);
        std::ofstream out(mOptions.configPath + ".tmp");
        out << mService.settingsText();
        out.close();
        ::rename((mOptions.configPath + ".tmp").c_str(), mOptions.configPath.c_str());  // never a half-written file
    }

    void ShellHost::applyTheme()
    {
        const Model &m = mService.model();
        palette::setAccent(parseColor(m.settings.at("accent"), Color::hex(0x4F7EF7)));
        artboard::setReducedMotion(m.settings.at("reduced-motion") == "true");
        mSpritesDrawn = false;
        mViewsFresh = true;
    }

    int ShellHost::iconImage(const AppInfo &app)
    {
        if (app.icon.empty()) return 0;
        auto it = mIconImages.find(app.icon);
        if (it != mIconImages.end()) return it->second;
        int id = 0;
        cairo_surface_t *s = cairo_image_surface_create_from_png(app.icon.c_str());
        if (cairo_surface_status(s) == CAIRO_STATUS_SUCCESS && cairo_image_surface_get_format(s) != CAIRO_FORMAT_INVALID)
        {
            // Through an ARGB32 copy, then to the HAL's straight-alpha RGBA.
            const int w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s);
            cairo_surface_t *argb = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
            cairo_t *cr = cairo_create(argb);
            cairo_set_source_surface(cr, s, 0, 0);
            cairo_paint(cr);
            cairo_destroy(cr);
            cairo_surface_flush(argb);
            const unsigned char *src = cairo_image_surface_get_data(argb);
            const int stride = cairo_image_surface_get_stride(argb);
            std::vector<uint8_t> rgba((size_t)w * h * 4);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    uint32_t px;
                    std::memcpy(&px, src + y * stride + x * 4, 4);
                    const unsigned a = px >> 24;
                    const auto un = [a](unsigned c) { return (uint8_t)(a ? std::min(255u, c * 255u / a) : 0u); };
                    uint8_t *d = &rgba[((size_t)y * w + x) * 4];
                    d[0] = un((px >> 16) & 255);
                    d[1] = un((px >> 8) & 255);
                    d[2] = un(px & 255);
                    d[3] = (uint8_t)a;
                }
            id = mTarget.registerImage(rgba.data(), w, h);
            cairo_surface_destroy(argb);
        }
        cairo_surface_destroy(s);
        mIconImages[app.icon] = id;
        return id;
    }

    // ── commands and events ──────────────────────────────────────────────────────────────────

    void ShellHost::dispatch(const std::string &line)
    {
        // A command born inside an event handler waits until the service has finished the one it
        // is in the middle of; the service is not re-entrant.
        if (mInEvent) { mDeferred.push_back(line); return; }
        mService.run(line);
        runDeferred();
    }

    void ShellHost::runDeferred()
    {
        while (!mDeferred.empty() && !mInEvent)
        {
            const std::string line = mDeferred.front();
            mDeferred.erase(mDeferred.begin());
            mService.run(line);
        }
    }

    ShellHost::Client *ShellHost::clientFor(int task)
    {
        for (auto &c : mClients)
            if (c->task == task && !c->dead) return c.get();
        return nullptr;
    }

    void ShellHost::onEvent(const Event &e)
    {
        mInEvent = true;
        const Model &m = mService.model();
        const int task = std::atoi(field(e, "task").c_str());
        if (e.name == "task.spawn")
        {
            if (const AppInfo *app = m.app(field(e, "app"))) spawn(task, *app);
        }
        else if (e.name == "task.kill")
        {
            if (Client *c = clientFor(task))
            {
                wire::Lifecycle l{wire::LifecycleState::Stop};
                c->channel.send(wire::Msg::Lifecycle, l);
            }
            auto it = mTasks.find(task);
            if (it != mTasks.end() && it->second.pid > 0) ::kill(it->second.pid, SIGTERM);
        }
        else if (e.name == "task.back")
        {
            Client *c = clientFor(task);
            if (c) c->channel.send(wire::Msg::Back);
            else mDeferred.push_back("task back-unhandled " + std::to_string(task));
        }
        else if (e.name == "task.state")
        {
            if (Client *c = clientFor(task))
            {
                const std::string s = field(e, "state");
                wire::Lifecycle l{s == "running" ? wire::LifecycleState::Resume : wire::LifecycleState::Pause};
                if (s != "starting") c->channel.send(wire::Msg::Lifecycle, l);
            }
        }
        else if (e.name == "setting.changed")
        {
            const std::string key = field(e, "key");
            applyTheme();
            saveSettings();
            if (key == "accent" || key == "reduced-motion")
                for (auto &c : mClients) configure(*c);
        }
        // System apps and control peers see the stream. A gesture in flight is not forwarded to
        // apps: it is sixty lines a second about something that is not theirs.
        const std::string line = formatEvent(e);
        if (e.name.compare(0, 8, "gesture.") != 0)
            for (auto &c : mClients)
                if (c->system && !c->dead) c->channel.sendText(wire::Msg::ShellEvent, line);
        for (ControlPeer &p : mControl)
            if (p.events)
            {
                const std::string out = "[evt] " + line + "\n";
                (void)!::write(p.fd, out.data(), out.size());
            }
        mViewsFresh = true;
        mInEvent = false;
    }

    // ── app processes ────────────────────────────────────────────────────────────────────────

    void ShellHost::spawn(int task, const AppInfo &app)
    {
        TaskView &tv = mTasks[task];
        const pid_t pid = ::fork();
        if (pid == 0)
        {
            ::setenv("ARSTRO_SHELL_SOCKET", mSocketPath.c_str(), 1);
            ::setenv("ARSTRO_TASK", std::to_string(task).c_str(), 1);
            ::setenv("ARSTRO_APP_ID", app.id.c_str(), 1);
            if (::chdir(app.dir.c_str()) != 0) { /* run where we are */ }
            ::execl(app.exec.c_str(), app.exec.c_str(), (char *)nullptr);
            ::_exit(127);
        }
        tv.pid = pid > 0 ? (int)pid : 0;
        if (pid < 0) mDeferred.push_back("task exited " + std::to_string(task));
    }

    void ShellHost::reap()
    {
        int status = 0;
        pid_t pid;
        while ((pid = ::waitpid(-1, &status, WNOHANG)) > 0)
            for (auto &kv : mTasks)
                if (kv.second.pid == (int)pid)
                {
                    kv.second.pid = 0;
                    const Task *t = mService.model().task(kv.first);
                    if (!t) continue;
                    if (WIFEXITED(status) && WEXITSTATUS(status) == 127)
                    {
                        // The executable could not be started: say so where the user will see it.
                        const AppInfo *app = mService.model().app(t->appId);
                        Command c;
                        c.kind = CommandKind::NotifyPost;
                        c.fields = {{"title", (app ? app->name : t->appId) + " could not start"},
                                    {"body", "Its program is missing or cannot be run."}, {"app", t->appId}};
                        mService.dispatch(c);
                    }
                    dispatch("task exited " + std::to_string(kv.first));
                }
    }

    void ShellHost::configure(Client &c)
    {
        if (c.dead || !c.task) return;
        const Color accent = palette::primary();
        wire::Configure cfg{};
        cfg.width = mPxW;
        cfg.height = mPxH;
        cfg.scale = (float)mScale;
        cfg.insetTop = (int32_t)std::lround(metric::statusBar() * mScale);
        cfg.insetBottom = (int32_t)std::lround(metric::navBar() * mScale);
        cfg.accent = ((uint32_t)std::lround(accent.r * 255) << 16) | ((uint32_t)std::lround(accent.g * 255) << 8) |
                     (uint32_t)std::lround(accent.b * 255);
        cfg.reducedMotion = artboard::reducedMotion() ? 1 : 0;
        c.channel.send(wire::Msg::Configure, cfg);
    }

    void ShellHost::handleMessage(Client &c, const wire::Message &m)
    {
        const Model &model = mService.model();
        switch (m.type)
        {
        case wire::Msg::Hello:
        {
            wire::Hello h;
            if (!m.as(h) || h.version != wire::kVersion) { c.dead = true; break; }
            h.appId[sizeof h.appId - 1] = 0;
            const Task *t = model.task(h.task);
            // Only a process the shell started for this task may attach to it.
            if (!t || t->appId != h.appId || clientFor(h.task)) { c.dead = true; break; }
            c.task = h.task;
            const AppInfo *app = model.app(t->appId);
            c.system = app && app->system;
            configure(c);
            if (c.system)
                for (const auto &kv : model.settings)
                    c.channel.sendText(wire::Msg::ShellEvent, "setting.changed key=" + kv.first + " value=" + quoteValue(kv.second));
            dispatch("task connected " + std::to_string(c.task));
            break;
        }
        case wire::Msg::Buffer:
        {
            wire::BufferInfo b;
            if (!m.as(b) || m.fd < 0) break;
            if (c.pixels) munmap(c.pixels, c.mapped);
            c.pixels = nullptr;
            const bool sane = b.width > 0 && b.height > 0 && b.width <= 8192 && b.height <= 8192 && b.stride >= b.width * 4;
            if (sane)
            {
                c.mapped = (size_t)b.stride * b.height;
                struct stat st{};
                // The mapping must be backed by the whole file, or reading it would fault.
                if (::fstat(m.fd, &st) == 0 && (size_t)st.st_size >= c.mapped)
                {
                    void *p = mmap(nullptr, c.mapped, PROT_READ, MAP_SHARED, m.fd, 0);
                    if (p != MAP_FAILED)
                    {
                        c.pixels = (uint8_t *)p;
                        c.w = b.width;
                        c.h = b.height;
                        c.stride = b.stride;
                    }
                }
            }
            ::close(m.fd);
            break;
        }
        case wire::Msg::Commit:
            if (c.task && c.pixels) c.committed = true;
            break;
        case wire::Msg::BackResult:
        {
            wire::BackResult r;
            if (m.as(r) && !r.handled && c.task) dispatch("task back-unhandled " + std::to_string(c.task));
            break;
        }
        case wire::Msg::Request:
            if (c.system) dispatch(m.text());
            break;
        case wire::Msg::Notify:
        {
            if (!c.task) break;
            const std::string s = m.text();
            const size_t nl = s.find('\n');
            const Task *t = model.task(c.task);
            Command cmd;
            cmd.kind = CommandKind::NotifyPost;
            cmd.fields = {{"title", s.substr(0, nl)}, {"body", nl == std::string::npos ? "" : s.substr(nl + 1)},
                          {"app", t ? t->appId : ""}};
            mService.dispatch(cmd);
            runDeferred();
            break;
        }
        case wire::Msg::Finish:
            if (c.task) dispatch("app close " + std::to_string(c.task));
            break;
        default: break;
        }
    }

    void ShellHost::pumpClients()
    {
        if (mListen >= 0)
            for (int fd; (fd = wire::acceptOn(mListen)) >= 0;)
            {
                auto c = std::make_unique<Client>();
                c->channel = wire::Channel(fd);
                mClients.push_back(std::move(c));
            }
        std::vector<wire::Message> inbox;
        for (size_t i = 0; i < mClients.size(); ++i)
        {
            Client &c = *mClients[i];
            inbox.clear();
            const bool alive = c.channel.pump(inbox);
            for (const wire::Message &m : inbox)
                if (!c.dead) handleMessage(c, m);
            if (!alive) c.dead = true;
        }
        for (size_t i = 0; i < mClients.size();)
        {
            Client &c = *mClients[i];
            if (!c.dead) { ++i; continue; }
            const int task = c.task;
            if (c.pixels) munmap(c.pixels, c.mapped);
            mClients.erase(mClients.begin() + i);
            // An app that hung up is an app that has ended, whatever its process is doing.
            if (task && mService.model().task(task))
            {
                auto it = mTasks.find(task);
                if (it != mTasks.end() && it->second.pid > 0) ::kill(it->second.pid, SIGTERM);
                dispatch("task exited " + std::to_string(task));
            }
        }
    }

    // ── the control socket ───────────────────────────────────────────────────────────────────

    void ShellHost::pumpControl()
    {
        if (mControlListen < 0) return;
        for (int fd; (fd = wire::acceptOn(mControlListen)) >= 0;) mControl.push_back({fd, "", false});
        for (size_t i = 0; i < mControl.size();)
        {
            ControlPeer &p = mControl[i];
            char buf[4096];
            bool gone = false;
            std::string in;
            for (;;)
            {
                const ssize_t n = ::read(p.fd, buf, sizeof buf);
                if (n > 0) { in.append(buf, (size_t)n); continue; }
                if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) gone = true;
                break;
            }
            p.in += in;
            size_t nl;
            while ((nl = p.in.find('\n')) != std::string::npos)
            {
                const std::string line = p.in.substr(0, nl);
                p.in.erase(0, nl + 1);
                std::string reply;
                if (line == "events") { p.events = true; reply = "ok"; }
                else reply = runLine(line);
                reply += "\n";
                (void)!::write(p.fd, reply.data(), reply.size());
            }
            if (gone) { ::close(p.fd); mControl.erase(mControl.begin() + i); }
            else ++i;
        }
    }

    std::string ShellHost::takeShotRequest()
    {
        std::string s;
        s.swap(mShot);
        return s;
    }

    std::string ShellHost::runLine(const std::string &line)
    {
        const std::vector<std::string> w = splitWords(line);
        if (w.empty()) return "ok";
        const auto numAt = [&](size_t i) { return i < w.size() ? std::atof(w[i].c_str()) : 0.0; };
        if (w[0] == "state") return mService.dump() + "ok";
        if (w[0] == "quit") { mQuit = true; return "ok"; }
        if (w[0] == "scene")
        {
            // The LIVE eased values — what a test needs to tell a tween from a snap.
            std::ostringstream o;
            o << "zoom=" << mScene.zoom() << " home=" << mScene.home() << " shade=" << mScene.shade()
              << " scroll=" << mScene.scroll() << " animating=" << (mScene.animating() ? "true" : "false") << "\n";
            for (const CardPose &c : mScene.cards())
                o << "card task=" << c.task << " x=" << c.rect.x << " y=" << c.rect.y << " w=" << c.rect.w << " h=" << c.rect.h
                  << " alpha=" << c.alpha << " radius=" << c.radius << "\n";
            return o.str() + "ok";
        }
        if (w[0] == "shot")
        {
            if (w.size() != 2) return "error: usage: shot <file.png>";
            mShot = w[1];
            return "ok";
        }
        if (w[0] == "clock")
        {
            if (w.size() < 2) return "error: usage: clock <HH:MM> <date…>";
            mFixedTime = w[1];
            mFixedDate.clear();
            for (size_t i = 2; i < w.size(); ++i) mFixedDate += (i > 2 ? " " : "") + w[i];
            return "ok";
        }
        if (w[0] == "tap" || w[0] == "swipe")
        {
            const bool tap = w[0] == "tap";
            if (w.size() != (tap ? 3u : 6u)) return tap ? "error: usage: tap <x> <y>" : "error: usage: swipe <x0> <y0> <x1> <y1> <ms>";
            // Scripted fingers go through the same entry point a real one does, one sample per
            // frame, so they exercise the real routing and the real gesture thresholds.
            double at = std::max(mNow, mSynth.empty() ? 0.0 : mSynth.back().at) + 1.0;
            const double x0 = numAt(1), y0 = numAt(2);
            mSynth.push_back({at, TouchSample::Kind::Down, x0, y0});
            if (tap) mSynth.push_back({at + 50.0, TouchSample::Kind::Up, x0, y0});
            else
            {
                const double x1 = numAt(3), y1 = numAt(4), ms = std::max(16.0, numAt(5));
                const int steps = std::max(2, (int)(ms / 12.0));
                for (int i = 1; i <= steps; ++i)
                {
                    const double k = (double)i / steps;
                    mSynth.push_back({at + ms * k, TouchSample::Kind::Move, lerp(x0, x1, k), lerp(y0, y1, k)});
                }
                mSynth.push_back({at + ms + 1.0, TouchSample::Kind::Up, x1, y1});
            }
            return "ok";
        }
        if (mInEvent) { mDeferred.push_back(line); return "ok"; }
        const bool ok = mService.run(line);
        runDeferred();
        return ok ? "ok" : "error: " + mService.model().lastError;
    }

    // ── input ────────────────────────────────────────────────────────────────────────────────

    void ShellHost::releaseTask(TaskView &tv)
    {
        if (tv.texture) SDL_DestroyTexture(tv.texture);
        if (tv.frame) cairo_surface_destroy(tv.frame);
        tv.texture = nullptr;
        tv.frame = nullptr;
        tv.header.release();
        tv.splash.release();
    }

    void ShellHost::resize(int pxW, int pxH)
    {
        mPxW = std::max(1, pxW);
        mPxH = std::max(1, pxH);
        if (!mRenderer)
        {
            if (mCanvas) cairo_surface_destroy(mCanvas);
            mCanvas = cairo_image_surface_create(CAIRO_FORMAT_RGB24, mPxW, mPxH);
        }
        mLast = -1.0;  // the next frame re-derives the scale and tells everyone
    }

    void ShellHost::cancelTarget()
    {
        artboard::RawPointer up{};
        up.kind = artboard::RawPointer::Kind::Up;
        up.touch = true;
        up.timeMs = mNow;
        up.pos = artboard::Point{-1e7, -1e7};  // released far outside: un-press, and click nothing
        if (mTarget_ == Target::Home) mHomeInput.feed(up);
        else if (mTarget_ == Target::Shade) mShadeInput.feed(up);
        else if (mTarget_ == Target::Recents) mScene.cancelTouch();
        else if (mTarget_ == Target::App)
            if (Client *c = clientFor(mTargetTask))
            {
                wire::TouchEvent e{wire::TouchPhase::Cancel, 0, 0, 0, mNow};
                c->channel.send(wire::Msg::Touch, e);
            }
        mTarget_ = Target::None;
    }

    void ShellHost::touch(TouchSample::Kind kind, double pxX, double pxY, double tMs)
    {
        const double x = pxX / mScale, y = pxY / mScale;
        // The system sees every sample first; what it does not claim goes to whatever is under
        // the finger.
        Command c;
        c.kind = CommandKind::Touch;
        c.touch = TouchSample{kind, x, y, tMs};
        mService.dispatch(c);
        runDeferred();
        const Navigator::Feed feed = mService.lastTouch();
        if (feed.claimedNow) cancelTarget();
        if (feed.consumed) return;

        const Model &m = mService.model();
        if (kind == TouchSample::Kind::Down)
        {
            mTargetTask = 0;
            if (m.shadeOpen || mScene.shade() > 0.5) mTarget_ = Target::Shade;
            else if (m.nav == NavState::Home) mTarget_ = Target::Home;
            else if (m.nav == NavState::Recents) mTarget_ = Target::Recents;
            else
            {
                mTarget_ = Target::App;
                mTargetTask = m.focusedTask;
            }
        }
        if (mTarget_ == Target::None) return;

        if (mTarget_ == Target::Home || mTarget_ == Target::Shade)
        {
            artboard::RawPointer p{};
            p.touch = true;
            p.timeMs = tMs;
            p.pos = artboard::Point{x, y};
            p.kind = kind == TouchSample::Kind::Down ? artboard::RawPointer::Kind::Down
                     : kind == TouchSample::Kind::Move ? artboard::RawPointer::Kind::Move
                                                       : artboard::RawPointer::Kind::Up;
            if (kind == TouchSample::Kind::Cancel) p.pos = artboard::Point{-1e7, -1e7};
            if (mTarget_ == Target::Home) { mHomeInput.feed(p); mHome->touched(mNow); }
            else { mShadeInput.feed(p); mShade->touched(mNow); }
        }
        else if (mTarget_ == Target::Recents)
        {
            std::vector<std::string> commands;
            mScene.recentsTouch(TouchSample{kind, x, y, tMs}, m, commands);
            for (const std::string &line : commands) dispatch(line);
        }
        else if (Client *cl = clientFor(mTargetTask))
        {
            wire::TouchEvent e{};
            e.phase = kind == TouchSample::Kind::Down ? wire::TouchPhase::Down
                      : kind == TouchSample::Kind::Move ? wire::TouchPhase::Move
                      : kind == TouchSample::Kind::Up ? wire::TouchPhase::Up : wire::TouchPhase::Cancel;
            e.x = (float)pxX;
            e.y = (float)pxY;
            e.timeMs = tMs;
            cl->channel.send(wire::Msg::Touch, e);
        }
        if (kind == TouchSample::Kind::Up || kind == TouchSample::Kind::Cancel) mTarget_ = Target::None;
    }

    // ── drawing ──────────────────────────────────────────────────────────────────────────────

    void ShellHost::timeStrings(std::string &time, std::string &date) const
    {
        if (!mFixedTime.empty()) { time = mFixedTime; date = mFixedDate; return; }
        const std::time_t now = std::time(nullptr);
        std::tm tm{};
        localtime_r(&now, &tm);
        char b[64];
        const bool h24 = mService.model().settings.at("clock24") == "true";
        if (h24) std::strftime(b, sizeof b, "%H:%M", &tm);
        else
        {
            std::strftime(b, sizeof b, "%I:%M", &tm);
            if (b[0] == '0') std::memmove(b, b + 1, std::strlen(b));
        }
        time = b;
        std::strftime(b, sizeof b, "%A, %e %B", &tm);
        date = b;
        const size_t two = date.find("  ");  // %e pads a single-digit day with a space
        if (two != std::string::npos) date.erase(two, 1);
    }

    void ShellHost::drawStaticSprites()
    {
        const double s = mScale;
        const auto draw = [&](Layer &l, double wDp, double hDp, const std::function<void(double, double)> &paint) {
            l.ensure(mRenderer, (int)std::ceil(wDp * s), (int)std::ceil(hDp * s));
            cairo_t *cr = l.begin();
            mTarget.setContext(cr);
            mTarget.save();
            mTarget.setTransform(artboard::Transform::scaling(s, s));
            paint(wDp, hDp);
            mTarget.restore();
            mTarget.setContext(nullptr);
            l.end(cr);
        };
        const double W = mPxW / s, H = mPxH / s;
        // The wallpaper: near-black, with one soft bloom of the accent high on the screen. It is
        // the only place the accent appears at rest, and it is barely there.
        draw(mWallpaper, W, H, [&](double w, double h) {
            roundedRectPath(mTarget, {0, 0, w, h}, 0);
            mTarget.setLinearFill(0, 0, 0, h, surface::wallpaperTop(), surface::wallpaperBottom());
            mTarget.fillPath();
            const Color a = palette::primary();
            roundedRectPath(mTarget, {0, 0, w, h}, 0);
            mTarget.setRadialFill(w * 0.5, -h * 0.25, std::max(w, h) * 0.9, Color{a.r, a.g, a.b, 0.11}, Color{a.r, a.g, a.b, 0.0});
            mTarget.fillPath();
        });
        draw(mPill, 120.0, 5.0, [&](double w, double h) { fillRounded(mTarget, {0, 0, w, h}, radius::pill(), palette::white()); });
        draw(mBackBubble, 44.0, 44.0, [&](double w, double h) {
            fillRounded(mTarget, {0, 0, w, h}, radius::pill(), Color{0.16, 0.17, 0.19, 0.96});
            drawIcon(mTarget, icons::kBackArrow, {(w - 20) * 0.5, (h - 20) * 0.5, 20, 20}, palette::white(), 2.0);
        });
        draw(mEmptyText, 360.0, 72.0, [&](double w, double h) {
            drawLabel(mTarget, "No recent apps", 0, h * 0.36, w, type::title(), font::sansMedium(), palette::foreground(), Align::Center);
            drawLabel(mTarget, "Apps you open appear here", 0, h * 0.74, w, type::label(), font::sans(), palette::mutedForeground(), Align::Center);
        });
        for (auto &kv : mTasks) kv.second.headerDrawn = false;
        mSpritesDrawn = true;
    }

    void ShellHost::drawTaskSprites(TaskView &tv, const AppInfo *app)
    {
        const double s = mScale;
        AppInfo fallback;
        fallback.name = "App";
        const AppInfo &a = app ? *app : fallback;
        const int image = app ? iconImage(*app) : 0;
        tv.header.ensure(mRenderer, (int)std::ceil(280.0 * s), (int)std::ceil(36.0 * s));
        cairo_t *cr = tv.header.begin();
        mTarget.setContext(cr);
        mTarget.save();
        mTarget.setTransform(artboard::Transform::scaling(s, s));
        {
            // Icon and name as one centred group, so a short name is not left hanging at an edge.
            const double tw = std::min(220.0, mTarget.measureText(a.name, type::label(), font::sansMedium()));
            const double x = (280.0 - (24.0 + 10.0 + tw)) * 0.5;
            drawAppIcon(mTarget, a, image, {x, 6.0, 24.0, 24.0});
            drawLabel(mTarget, a.name, x + 34.0, 18.0, tw + 1.0, type::label(), font::sansMedium(), palette::foreground());
        }
        mTarget.restore();
        mTarget.setContext(nullptr);
        tv.header.end(cr);

        tv.splash.ensure(mRenderer, (int)std::ceil(96.0 * s), (int)std::ceil(96.0 * s));
        cr = tv.splash.begin();
        mTarget.setContext(cr);
        mTarget.save();
        mTarget.setTransform(artboard::Transform::scaling(s, s));
        drawAppIcon(mTarget, a, image, {0, 0, 96.0, 96.0});
        mTarget.restore();
        mTarget.setContext(nullptr);
        tv.splash.end(cr);
        tv.headerDrawn = true;
    }

    void ShellHost::renderViews(double nowMs)
    {
        const double s = mScale;
        const artboard::Transform dp = artboard::Transform::scaling(s, s);
        const auto paint = [&](Layer &l, artboard::Segment &root, int w, int h, bool force) {
            const bool fresh = l.ensure(mRenderer, w, h);
            if (!fresh && !force) return;
            cairo_t *cr = l.begin();
            mTarget.setContext(cr);
            root.render(mTarget, dp);
            root.renderOverlay(mTarget, dp);
            mTarget.setContext(nullptr);
            l.end(cr);
        };
        if (mViewsFresh) { mHomeStale = mStatusStale = mShadeStale = true; mViewsFresh = false; }
        // A hidden layer is not redrawn at all — the home screen under a running app, the shade
        // while it is up. It stays marked stale and is drawn the first frame it is seen again.
        const bool homeSeen = mScene.home() > 0.004 || !mHomeLayer.surface;
        const bool shadeSeen = mScene.shade() > 0.004 || mService.model().shadeOpen || !mShadeLayer.surface;
        if (homeSeen)
        {
            paint(mHomeLayer, *mHome, mPxW, mPxH, mHomeStale || mHome->busy(nowMs));
            mHomeStale = false;
        }
        paint(mStatusLayer, *mStatus, mPxW, (int)std::ceil(metric::statusBar() * s), mStatusStale || mStatus->busy(nowMs));
        mStatusStale = false;
        if (shadeSeen)
        {
            paint(mShadeLayer, *mShade, mPxW, mPxH, mShadeStale || mShade->busy(nowMs));
            mShadeStale = false;
        }
    }

    void ShellHost::quad(const Pic &pic, const Rect &d, const Rect &uv, double radius, double alpha, const Color &solid)
    {
        if (alpha <= 0.003 || d.w <= 0.0 || d.h <= 0.0) return;
        if (!mRenderer)
        {
            // Headless: the same quad, drawn by Cairo.
            cairo_t *cr = mCanvasCr;
            if (!cr) return;
            cairo_save(cr);
            const double r = std::min(radius, std::min(d.w, d.h) * 0.5);
            if (r < 0.75) cairo_rectangle(cr, d.x, d.y, d.w, d.h);
            else
            {
                cairo_new_sub_path(cr);
                cairo_arc(cr, d.x + d.w - r, d.y + r, r, -M_PI / 2, 0);
                cairo_arc(cr, d.x + d.w - r, d.y + d.h - r, r, 0, M_PI / 2);
                cairo_arc(cr, d.x + r, d.y + d.h - r, r, M_PI / 2, M_PI);
                cairo_arc(cr, d.x + r, d.y + r, r, M_PI, 3 * M_PI / 2);
                cairo_close_path(cr);
            }
            cairo_clip(cr);
            if (pic.surface)
            {
                const double sx = d.w / (uv.w * pic.w), sy = d.h / (uv.h * pic.h);
                cairo_translate(cr, d.x - uv.x * pic.w * sx, d.y - uv.y * pic.h * sy);
                cairo_scale(cr, sx, sy);
                cairo_set_source_surface(cr, pic.surface, 0, 0);
                cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
                cairo_paint_with_alpha(cr, clamp01(alpha));
            }
            else
            {
                cairo_set_source_rgba(cr, solid.r, solid.g, solid.b, solid.a * clamp01(alpha));
                cairo_paint(cr);
            }
            cairo_restore(cr);
            return;
        }
        SDL_Texture *tex = pic.texture;
        const float a = (float)clamp01(alpha);
        // Textures are premultiplied, so fading one scales all four channels.
        const SDL_FColor col = tex ? SDL_FColor{a, a, a, a}
                                   : SDL_FColor{(float)(solid.r * solid.a) * a, (float)(solid.g * solid.a) * a,
                                                (float)(solid.b * solid.a) * a, (float)solid.a * a};
        const auto vertex = [&](double x, double y, const SDL_FColor &c) {
            SDL_Vertex v;
            v.position = SDL_FPoint{(float)x, (float)y};
            v.color = c;
            v.tex_coord = SDL_FPoint{(float)(uv.x + (x - d.x) / d.w * uv.w), (float)(uv.y + (y - d.y) / d.h * uv.h)};
            return v;
        };
        std::vector<SDL_Vertex> verts;
        std::vector<int> idx;
        const double r = std::min(radius, std::min(d.w, d.h) * 0.5);
        if (r < 0.75)
        {
            verts = {vertex(d.x, d.y, col), vertex(d.x + d.w, d.y, col), vertex(d.x + d.w, d.y + d.h, col), vertex(d.x, d.y + d.h, col)};
            idx = {0, 1, 2, 0, 2, 3};
        }
        else
        {
            // A fan over the rounded outline, plus a one-pixel ring that fades to nothing: the
            // ring is the anti-aliasing, so a card's corners stay smooth while it moves.
            const int seg = 10;
            std::vector<artboard::Point> pts, normals;
            const double cx[4] = {d.x + d.w - r, d.x + d.w - r, d.x + r, d.x + r};
            const double cy[4] = {d.y + r, d.y + d.h - r, d.y + d.h - r, d.y + r};
            const double a0[4] = {-M_PI / 2, 0, M_PI / 2, M_PI};
            for (int c = 0; c < 4; ++c)
                for (int i = 0; i <= seg; ++i)
                {
                    const double ang = a0[c] + (M_PI / 2) * i / seg;
                    normals.push_back({std::cos(ang), std::sin(ang)});
                    pts.push_back({cx[c] + std::cos(ang) * r, cy[c] + std::sin(ang) * r});
                }
            const int n = (int)pts.size();
            const SDL_FColor clear{0, 0, 0, 0};
            verts.push_back(vertex(d.x + d.w * 0.5, d.y + d.h * 0.5, col));
            for (int i = 0; i < n; ++i) verts.push_back(vertex(pts[i].x - normals[i].x * 0.5, pts[i].y - normals[i].y * 0.5, col));
            for (int i = 0; i < n; ++i) verts.push_back(vertex(pts[i].x + normals[i].x * 0.5, pts[i].y + normals[i].y * 0.5, clear));
            for (int i = 0; i < n; ++i)
            {
                const int j = (i + 1) % n;
                idx.insert(idx.end(), {0, 1 + i, 1 + j});
                idx.insert(idx.end(), {1 + i, 1 + n + i, 1 + n + j, 1 + i, 1 + n + j, 1 + j});
            }
        }
        SDL_RenderGeometry(mRenderer, tex, verts.data(), (int)verts.size(), idx.data(), (int)idx.size());
    }

    void ShellHost::composite()
    {
        const double s = mScale, W = mPxW, H = mPxH;
        const Rect full{0, 0, 1, 1};
        const Pic none;
        if (mRenderer)
        {
            SDL_SetRenderDrawBlendMode(mRenderer, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
            SDL_SetRenderDrawColor(mRenderer, 0, 0, 0, 255);
            SDL_RenderClear(mRenderer);
        }
        else if (mCanvas)
        {
            mCanvasCr = cairo_create(mCanvas);
            cairo_set_source_rgb(mCanvasCr, 0, 0, 0);
            cairo_paint(mCanvasCr);
        }

        quad(mWallpaper, {0, 0, W, H}, full, 0, 1.0);

        // Home recedes a little as it fades — uniformly, about its centre.
        const double home = clamp01(mScene.home()), hu = lerp(0.95, 1.0, home);
        quad(mHomeLayer, {W * (1 - hu) * 0.5, H * (1 - hu) * 0.5, W * hu, H * hu}, full, 0, home);

        const double empty = mScene.emptyRecents();
        quad(mEmptyText, {(W - mEmptyText.w) * 0.5, (H - mEmptyText.h) * 0.5, (double)mEmptyText.w, (double)mEmptyText.h}, full, 0, empty);

        const double dpW = W / s;
        for (const CardPose &c : mScene.cards())
        {
            auto it = mTasks.find(c.task);
            if (it == mTasks.end()) continue;
            TaskView &tv = it->second;
            const Rect r{c.rect.x * s, c.rect.y * s, c.rect.w * s, c.rect.h * s};
            const double rad = c.radius * s;
            quad(none, r, full, rad, c.alpha, surface::appBackground());
            const double reveal = clamp01(tv.reveal.value());
            // The card shows the app for most of its way and turns into its icon only near the
            // icon: two pictures half-mixed for the whole trip would read as a smudge.
            const double tk = clamp01((c.iconMix - 0.45) / 0.4), toIcon = tk * tk * (3.0 - 2.0 * tk);
            Pic app;
            app.texture = tv.texture;
            app.surface = tv.frame;
            app.w = tv.texW;
            app.h = tv.texH;
            if (!app.empty() && reveal > 0.0)
            {
                // The app's picture covers the card at ONE scale and is cropped — never squeezed
                // to the card's shape (arstro.rule.animation, Rule 1).
                const double k = std::max(r.w / tv.texW, r.h / tv.texH);
                const double uw = r.w / (tv.texW * k), uh = r.h / (tv.texH * k);
                quad(app, r, {(1 - uw) * 0.5, (1 - uh) * 0.5, uw, uh}, rad, c.alpha * reveal * (1.0 - toIcon));
            }
            // The icon: the splash before the app's first frame, and what the card turns back
            // into on its way home.
            const double iconAlpha = std::max(1.0 - reveal, toIcon);
            if (iconAlpha > 0.0 && tv.splash.surface)
            {
                const double base = lerp(metric::iconSize(), 88.0, clamp01((c.rect.w - metric::iconSize()) / std::max(1.0, dpW - metric::iconSize())));
                const double size = std::min({base * s, r.w, r.h});
                quad(tv.splash, {r.x + (r.w - size) * 0.5, r.y + (r.h - size) * 0.5, size, size}, full, 0, c.alpha * iconAlpha);
            }
            if (c.header > 0.0 && tv.header.surface)
                quad(tv.header, {r.x + (r.w - tv.header.w) * 0.5, r.y - tv.header.h - 8.0 * s, (double)tv.header.w, (double)tv.header.h},
                     full, 0, c.header);
        }

        const double shade = clamp01(mScene.shade());
        if (shade > 0.0)
        {
            quad(none, {0, 0, W, H}, full, 0, 0.55 * shade, Color{0, 0, 0, 1});
            // The sheet fades while it travels and is gone exactly when it is fully up.
            quad(mShadeLayer, {0, -(1.0 - shade) * H, W, H}, full, 0, clamp01(shade * 1.8));
        }
        quad(mStatusLayer, {0, 0, (double)mStatusLayer.w, (double)mStatusLayer.h}, full, 0, 1.0);

        const double pillAlpha = lerp(0.75, 0.32, home) * (1.0 - shade);
        quad(mPill, {(W - mPill.w) * 0.5, H - (metric::navBar() * 0.5) * s - mPill.h * 0.5, (double)mPill.w, (double)mPill.h}, full, 0, pillAlpha);

        const Scene::BackHint back = mScene.back();
        if (back.amount > 0.003)
        {
            const double bw = mBackBubble.w, travel = (back.pull * 0.55 - 30.0) * s;
            const double x = back.edge < 0 ? travel : W - travel - bw;
            // Mirrored for the right edge by flipping the texture coordinates, not the shape.
            quad(mBackBubble, {x, back.y * s - bw * 0.5, bw, bw}, back.edge < 0 ? full : Rect{1, 0, -1, 1}, 0, clamp01(back.amount * 1.4));
        }

        const double dim = mDim.value();
        if (dim > 0.003) quad(none, {0, 0, W, H}, full, 0, dim, Color{0, 0, 0, 1});
        if (mCanvasCr)
        {
            cairo_destroy(mCanvasCr);
            mCanvasCr = nullptr;
            cairo_surface_flush(mCanvas);
        }
    }

    void ShellHost::frame(double nowMs)
    {
        mNow = nowMs;
        const bool first = mLast < 0.0;
        const double dt = first ? 0.0 : std::min(0.05, (nowMs - mLast) / 1000.0);
        mLast = nowMs;

        pumpClients();
        reap();
        pumpControl();
        while (!mSynth.empty() && mSynth.front().at <= nowMs)
        {
            const Synth sy = mSynth.front();
            mSynth.erase(mSynth.begin());
            touch(sy.kind, sy.x * mScale, sy.y * mScale, sy.at);
        }

        // Density: the display's own, times the user's setting. With no DPI to read, a screen's
        // short side is taken as 540 dp at most — a 5" 800x480 panel and a 10" 1280x800 one then
        // show the same layout at a size a finger can use.
        //
        // The setting is the largest visible change the shell can make, so it EASES: the scale is
        // a spring, and while it moves every frame re-derives the dp screen from the live value.
        const Model &m0 = mService.model();
        const double want = (mOptions.scale > 0.0 ? mOptions.scale : std::clamp(std::min(mPxW, mPxH) / 540.0, 1.0, 3.0)) *
                            std::atof(m0.settings.at("scale").c_str());
        if (first) mScaleSpring.reset(want);
        mScaleSpring.setTarget(want);
        const double live = mScaleSpring.isMoving(5e-4) ? mScaleSpring.advance(dt, motion::springNav()) : want;
        const double dpW = mPxW / live, dpH = mPxH / live;
        if (first || std::fabs(live - mScale) > 1e-6 || std::fabs(m0.screenW - dpW) > 0.01 || std::fabs(m0.screenH - dpH) > 0.01)
        {
            mScale = live;
            char b[96];
            std::snprintf(b, sizeof b, "screen %.3f %.3f", dpW, dpH);
            dispatch(b);
            mSpritesDrawn = false;
            mViewsFresh = true;
            for (auto &c : mClients) configure(*c);
        }
        const Model &m = mService.model();

        std::string time, date;
        timeStrings(time, date);
        mHome->sync(m, time, date, nowMs);
        mStatus->sync(m, time, nowMs);
        mShade->sync(m, time, date, nowMs);
        mHomeInput.advance(nowMs);
        mShadeInput.advance(nowMs);
        mStatus->width.set(m.screenW);
        mStatus->height.set(metric::statusBar());
        mHome->advance(nowMs);
        mStatus->advance(nowMs);
        mShade->advance(nowMs);
        mHome->layout(m.screenW, m.screenH);
        mShade->layout(m.screenW, m.screenH);

        mScene.advance(m, dt, [this](const std::string &id, Rect &out) { return mHome->iconRect(id, out); });

        // Brightness with no backlight to drive is a dim over everything; it eases like any value.
        mDim.setTarget((1.0 - std::atof(m.settings.at("brightness").c_str())) * 0.78);
        if (first) mDim.reset(mDim.target());
        mDim.advance(dt, motion::springFade());

        // The apps' new frames.
        for (auto &c : mClients)
        {
            if (!c->committed || !c->pixels) continue;
            c->committed = false;
            c->owesDone = true;
            TaskView &tv = mTasks[c->task];
            if (tv.texW != c->w || tv.texH != c->h || (!tv.texture && !tv.frame))
            {
                if (tv.texture) SDL_DestroyTexture(tv.texture);
                if (tv.frame) cairo_surface_destroy(tv.frame);
                tv.texture = nullptr;
                tv.frame = nullptr;
                tv.texW = c->w;
                tv.texH = c->h;
                if (mRenderer)
                {
                    tv.texture = SDL_CreateTexture(mRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, c->w, c->h);
                    if (tv.texture)
                    {
                        SDL_SetTextureBlendMode(tv.texture, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
                        SDL_SetTextureScaleMode(tv.texture, SDL_SCALEMODE_LINEAR);
                    }
                }
                else tv.frame = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, c->w, c->h);
            }
            // The copy is the hand-over: after it the buffer is the app's to draw in again.
            if (tv.texture) SDL_UpdateTexture(tv.texture, nullptr, c->pixels, c->stride);
            if (tv.frame)
            {
                cairo_surface_flush(tv.frame);
                const int stride = cairo_image_surface_get_stride(tv.frame);
                unsigned char *dst = cairo_image_surface_get_data(tv.frame);
                for (int y = 0; y < c->h; ++y) std::memcpy(dst + (size_t)y * stride, c->pixels + (size_t)y * c->stride, (size_t)c->w * 4);
                cairo_surface_mark_dirty(tv.frame);
            }
            tv.reveal.setTarget(1.0);
        }

        if (!mSpritesDrawn) drawStaticSprites();
        for (auto it = mTasks.begin(); it != mTasks.end();)
        {
            TaskView &tv = it->second;
            const Task *t = m.task(it->first);
            const bool onScreen = std::any_of(mScene.cards().begin(), mScene.cards().end(),
                                              [&](const CardPose &c) { return c.task == it->first; });
            if (!tv.headerDrawn) drawTaskSprites(tv, t ? m.app(t->appId) : nullptr);
            tv.reveal.advance(dt, motion::springFade());
            // A task's pictures outlive the task until its card has finished leaving the screen.
            tv.idleFrames = (t || onScreen) ? 0 : tv.idleFrames + 1;
            if (tv.idleFrames > 30 && tv.pid == 0)
            {
                releaseTask(tv);
                it = mTasks.erase(it);
            }
            else ++it;
        }

        renderViews(nowMs);
        composite();
    }

    void ShellHost::presented(double nowMs)
    {
        for (auto &c : mClients)
            if (c->owesDone && !c->dead)
            {
                c->owesDone = false;
                wire::FrameDone d{nowMs};
                c->channel.send(wire::Msg::FrameDone, d);
            }
    }
}
}
