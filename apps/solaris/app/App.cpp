#include "App.h"
#include "../../interstellar/app/widgets/TextFit.h"
#include <algorithm>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;
    namespace textfit = interstellar_v1::textfit;

    namespace
    {
        constexpr double kActiveWindowMs = 600.0; // redraw this long after any input: hovers finish
    }

    const solaris::AppModel &App::emptyModel()
    {
        static const solaris::AppModel m;
        return m;
    }

    std::string App::quote(const std::string &s)
    {
        return s.find_first_of(" \t\"") == std::string::npos && !s.empty() ? s : "\"" + s + "\"";
    }

    App::App(AppHooks hooks, double width, double height) : mHooks(std::move(hooks)), mW(width), mH(height)
    {
        installSolarisAccent(); // FIRST: widgets read the accent when they are built
        mHome = std::make_shared<HomeScreen>();
        mProject = std::make_shared<ProjectScreen>();
        mSettings = std::make_shared<SettingsSheet>();
        mConfirm = std::make_shared<cosmo_v2::ConfirmDialog>(palette::primary());
        mModalRoot = std::make_shared<Segment>();
        mModalRoot->addChild(mConfirm);

        mHome->onNewSong = [this] { if (onPickSongToCreate) onPickSongToCreate(); };
        mHome->onOpenSong = [this] { if (onPickSongToOpen) onPickSongToOpen(); };
        mHome->onSettings = [this] { openSettings(); };
        mHome->onOpenRecent = [this](const std::string &p) { dispatch("project open " + quote(p)); };
        mHome->onForgetRecent = [this](const std::string &p) { dispatch("recents remove " + quote(p)); };

        SongBar &bar = mProject->bar();
        bar.onHome = [this] { requestHome(); };
        bar.onPlayToggle = [this] {
            const auto &m = mHooks.model ? mHooks.model() : emptyModel();
            dispatch(m.transport.playing ? "transport stop" : "transport play");
        };
        bar.onSave = [this] { dispatch("project save"); };
        bar.onSettings = [this] { openSettings(); };

        mSettings->onCommand = [this](const std::string &line) { dispatch(line); };
        mSettings->onAddFolder = [this] {
            if (onPickFolder) onPickFolder([this](const std::string &path) { dispatch("folder add " + quote(path)); });
        };

        mRecognizer.setSink([this](const Gesture &g) {
            if (mConfirm->isOpen()) { mConfirm->onGesture(g); return; } // a modal owns input
            if (mSettings->isOpen()) { mSettings->onGesture(g); return; }
            if (mScreen == "home") mHome->onGesture(g);
            else mProject->onGesture(g);
        });
        layoutAll();
    }

    void App::layoutAll()
    {
        for (Segment *s : {(Segment *)mHome.get(), (Segment *)mProject.get(), (Segment *)mSettings.get(), (Segment *)mModalRoot.get(), (Segment *)mConfirm.get()})
        {
            s->x.set(0);
            s->y.set(0);
            s->width.set(mW);
            s->height.set(mH);
        }
    }

    void App::setSize(double width, double height)
    {
        mW = std::max(1.0, width);
        mH = std::max(1.0, height);
        mLastActivityMs = mNowMs;
        layoutAll();
    }

    Segment *App::activeRoot() { return mScreen == "home" ? (Segment *)mHome.get() : (Segment *)mProject.get(); }

    double App::screenOpacity(const std::string &screen) const
    {
        return screen == "home" ? mHome->opacity.value() : mProject->opacity.value();
    }

    void App::showToast(const std::string &text)
    {
        mToastText = text;
        mToastWanted = true;
    }

    bool App::dispatch(const std::string &line)
    {
        mLastActivityMs = mNowMs;
        if (!mHooks.dispatch) return false;
        std::string err;
        if (mHooks.dispatch(line, err)) return true;
        showToast(err.empty() ? "refused: " + line : err);
        return false;
    }

    void App::openSongPicked(const std::string &path) { dispatch("project open " + quote(path)); }

    void App::newSongPicked(const std::string &path)
    {
        std::string p = path;
        if (p.size() < 4 || p.compare(p.size() - 4, 4, ".slp") != 0) p += ".slp";
        dispatch("project new " + quote(p));
    }

    void App::openSettings()
    {
        dispatch("devices list"); // the device chips are what is connected NOW
        mSettings->show();
    }

    void App::requestHome()
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        if (!m.dirty) { dispatch("project close"); return; }
        cosmo_v2::ConfirmDialog::Button save, discard, cancel;
        save.label = "Save";
        save.primary = true;
        save.onClick = [this] { if (dispatch("project save")) dispatch("project close"); };
        discard.label = "Discard";
        discard.destructive = true;
        discard.onClick = [this] { dispatch("project close"); };
        cancel.label = "Cancel";
        mConfirm->show("Unsaved changes", "Save \"" + m.projectName + "\" before going Home?", {cancel, discard, save});
    }

    void App::bindIfStale()
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        if (m.revision == mBoundRevision && !m.transport.playing) return;
        mBoundRevision = m.revision;
        mHome->bind(m);
        mProject->bind(m);
        mSettings->bind(m);
    }

    bool App::needsRedraw(double nowMs) const
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        if (m.revision != mBoundRevision || m.transport.playing) return true;
        if (mHome->opacity.isAnimating() || mProject->opacity.isAnimating() || mToast.isAnimating() || mToast.value() > 0.0) return true;
        if (mSettings->appearAmount() > 0.0) return true;
        return nowMs - mLastActivityMs < kActiveWindowMs;
    }

    void App::pointer(int kind, double x, double y, int button, double timeMs, bool alt, bool shift, bool ctrl)
    {
        mLastActivityMs = mNowMs;
        RawPointer rp{};
        rp.kind = kind == 0 ? RawPointer::Kind::Down : (kind == 2 ? RawPointer::Kind::Up : RawPointer::Kind::Move);
        rp.pos = Point{x, y};
        rp.button = button == 2 ? PointerButton::Right : PointerButton::Left;
        rp.timeMs = timeMs;
        rp.alt = alt; rp.shift = shift; rp.ctrl = ctrl;
        mRecognizer.feed(rp);
    }

    void App::wheel(double x, double y, double notches, bool ctrl)
    {
        mLastActivityMs = mNowMs;
        RawPointer rp{};
        rp.kind = RawPointer::Kind::Scroll;
        rp.pos = Point{x, y};
        rp.timeMs = mNowMs;
        rp.ctrl = ctrl;
        rp.scroll = Point{0.0, -notches * 40.0}; // + notches = wheel up = toward the start (Interstellar's notch)
        mRecognizer.feed(rp);
    }

    bool App::key(const KeyEvent &e)
    {
        mLastActivityMs = mNowMs;
        if (mConfirm->isOpen()) { mConfirm->handleKey(e); return true; }
        if (mSettings->isOpen()) return mSettings->handleKey(e);
        if (e.type != KeyEvent::Type::Down) return false;
        if (mScreen == "project")
        {
            if (e.keyCode == 32) // Space: play / stop, as every DAW
            {
                const auto &m = mHooks.model ? mHooks.model() : emptyModel();
                return dispatch(m.transport.playing ? "transport stop" : "transport play");
            }
            if (e.ctrl && e.keyCode == 'S') return dispatch("project save");
            if (e.keyCode == 13 && !e.ctrl) return dispatch("transport seek 0");
        }
        if (e.ctrl && e.keyCode == ',') { openSettings(); return true; }
        return false;
    }

    void App::render(IRenderTarget &target, double nowMs)
    {
        mNowMs = nowMs;
        bindIfStale();
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        // the screen cross-fade: the model says which screen; the view travels there
        const std::string want = m.screen == "project" ? "project" : "home";
        if (!mScreenInit)
        {
            mHome->opacity.set(want == "home" ? 1.0 : 0.0);
            mProject->opacity.set(want == "project" ? 1.0 : 0.0);
            mScreen = want;
            mScreenInit = true;
        }
        else if (want != mScreen)
        {
            mHome->opacity.animateTo(want == "home" ? 1.0 : 0.0, motion::kScreenFadeMs, Easing::EaseInOutCubic, nowMs);
            mProject->opacity.animateTo(want == "project" ? 1.0 : 0.0, motion::kScreenFadeMs, Easing::EaseInOutCubic, nowMs);
            mScreen = want;
        }
        // the toast: fades in, HOLDS, fades out slowly
        if (mToastWanted)
        {
            mToast.animateTo(1.0, motion::kToastInMs, Easing::EaseOutCubic, nowMs);
            mToastShownAt = nowMs;
            mToastWanted = false;
        }
        if (mToast.value() > 0.0 && !mToast.isAnimating() && nowMs - mToastShownAt > motion::kToastHoldMs)
            mToast.animateTo(0.0, motion::kToastOutMs, Easing::EaseInOutCubic, nowMs);
        mToast.update(nowMs);
        mRecognizer.advance(nowMs);

        drawRoundedRect(target, Rect{0, 0, mW, mH}, 0.0, Paint::filled(palette::background()));
        for (Segment *s : {(Segment *)mHome.get(), (Segment *)mProject.get()})
        {
            const bool wanted = (s == mHome.get()) == (mScreen == "home");
            if (!wanted && s->opacity.value() <= 0.0 && !s->opacity.isAnimating()) continue;
            if (s == mHome.get()) mHome->layout();
            else mProject->layout();
            s->advance(nowMs);
        }
        for (Segment *s : {(Segment *)mHome.get(), (Segment *)mProject.get()})
        {
            if (s->opacity.value() <= Segment::kOpacityEpsilon) continue;
            s->render(target);
            s->renderOverlay(target);
        }
        // the modals float over whichever screen opened them; advanced always, so a close can finish
        mSettings->advance(nowMs);
        mSettings->renderOverlay(target);
        mModalRoot->advance(nowMs);
        mModalRoot->renderOverlay(target);

        const double ta = mToast.value();
        if (ta > 0.001 && !mToastText.empty())
        {
            const double px = 12.0;
            const std::string s = textfit::ellipsize(target, mToastText, std::min(640.0, mW - 64.0), px, font::sans());
            const double w = target.measureText(s, px, font::sans()) + 32.0, h = 34.0;
            const Rect r{(mW - w) * 0.5, mH - 24.0 - h + (1.0 - ta) * 12.0, w, h};
            Color bg = palette::popover(), bd = palette::destructive(), fg = palette::foreground();
            bg.a *= ta; bd.a *= 0.6 * ta; fg.a *= ta;
            drawRoundedRect(target, r, radius::control(), Paint::filledStroked(bg, bd, 1.0));
            target.setFill(fg);
            target.drawText(s, r.x + 16.0, textfit::baseline(r.y + h * 0.5, px), px, font::sans());
        }
    }
}
}
