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
        mMenu = std::make_shared<cosmo_v2::ContextMenu>();
        mModalRoot = std::make_shared<Segment>();
        mModalRoot->addChild(mConfirm);
        mModalRoot->addChild(mMenu);

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
        mProject->onCommand = [this](const std::string &line) { return dispatch(line); };
        mProject->onNotice = [this](const std::string &s) { showToast(s); };
        mProject->browser().onOpenSettings = [this] { openSettings(); };
        mProject->onMenu = [this](std::vector<cosmo_v2::ContextMenu::Item> items, Point world) { mMenu->open(std::move(items), world.x, world.y); };
        mProject->onRename = [this](const std::string &cur, Point world, std::function<void(const std::string &)> done) {
            mMenu->onRename = std::move(done);
            mMenu->openRename(cur, world.x, world.y);
        };

        mSettings->onCommand = [this](const std::string &line) { dispatch(line); };
        mSettings->onAddFolder = [this] {
            if (onPickFolder) onPickFolder([this](const std::string &path) { dispatch("folder add " + quote(path)); });
        };

        buildMenus();
        mRecognizer.setSink([this](const Gesture &g) {
            if (mMenu->isOpen()) { mMenu->onGesture(g); return; }       // a modal owns input
            if (g.type == Gesture::Type::RightClick && mScreen == "project" && !mConfirm->isOpen() && !mSettings->isOpen() &&
                mProject->contextClick(g.pos))
                return; // a window's parameter row: its menu (a slider would swallow the click)
            if (mConfirm->isOpen()) { mConfirm->onGesture(g); return; }
            if (mSettings->isOpen()) { mSettings->onGesture(g); return; }
            if (mScreen == "home") mHome->onGesture(g);
            else mProject->onGesture(g);
        });
        layoutAll();
    }

    // ── the song bar's menus (R-UI-3 amended): every item a command line or a host picker ────────

    std::string App::selectedClip() const { return mProject->timeline().selectedClip(); }

    void App::buildMenus()
    {
        auto &ms = mProject->bar().menus();
        ms.addMenu({"File", {
            {"New Song\xE2\x80\xA6", [this] { if (onPickSongToCreate) onPickSongToCreate(); }},
            {"Open\xE2\x80\xA6", [this] { if (onPickSongToOpen) onPickSongToOpen(); }},
            {"Save            (Ctrl+S)", [this] { dispatch("project save"); }},
            {"Save As\xE2\x80\xA6", [this] {
                 const auto &m = mHooks.model ? mHooks.model() : emptyModel();
                 if (onPickSave) onPickSave("Save the song as", m.projectName + ".slp", [this](const std::string &p) { dispatch("project save " + quote(p)); });
             }},
            {"Render\xE2\x80\xA6", [this] {
                 const auto &m = mHooks.model ? mHooks.model() : emptyModel();
                 if (onPickSave) onPickSave("Render the mix", m.projectName + ".wav", [this](const std::string &p) { dispatch("render --out " + quote(p)); });
             }},
            {"Render Stems\xE2\x80\xA6", [this] {
                 const auto &m = mHooks.model ? mHooks.model() : emptyModel();
                 std::string ids;
                 for (const auto &s : m.strips) ids += (ids.empty() ? "" : ",") + s.id;
                 if (onPickSave && !ids.empty())
                     onPickSave("Render the mix and a stem per strip", m.projectName + ".wav",
                                [this, ids](const std::string &p) { dispatch("render --out " + quote(p) + " --stems " + ids); });
             }},
            {"Home", [this] { requestHome(); }},
        }});
        ms.addMenu({"Edit", {}});   // filled by refreshMenus: its labels name what undo would take back
        ms.addMenu({"Song", {
            {"Add Mixer", [this] { dispatch("mixer add"); }},
            {"Add Bus", [this] { dispatch("strip add --kind bus"); }},
            {"Add Audio Line", [this] { dispatch("strip add --kind audio"); }},
            {"Add Lane", [this] { dispatch("lane add"); }},
        }});
        ms.addMenu({"View", {}});   // filled by refreshMenus: Show / Hide follows the state
        refreshMenus(mHooks.model ? mHooks.model() : emptyModel());
    }

    void App::refreshMenus(const solaris::AppModel &m)
    {
        const std::string key = m.undoLabel + "|" + m.redoLabel + "|" + (mProject->dockOpen() ? "1" : "0") + (mProject->browserOpen() ? "1" : "0") +
                                (m.settings.metronome ? "1" : "0");
        if (key == mMenuKey) return; // labels change only when what they name does
        mMenuKey = key;
        auto &ms = mProject->bar().menus();
        const std::string undo = m.undoLabel.empty() ? "Undo" : "Undo " + m.undoLabel, redo = m.redoLabel.empty() ? "Redo" : "Redo " + m.redoLabel;
        ms.setItems(1, {
            {undo + "   (Ctrl+Z)", [this] { dispatch("undo"); }},
            {redo + "   (Ctrl+Y)", [this] { dispatch("redo"); }},
            {"Duplicate Clip   (Ctrl+D)", [this] { const auto c = selectedClip(); if (!c.empty()) dispatch("clip duplicate " + c); }},
            {"Delete Clip   (Del)", [this] { const auto c = selectedClip(); if (!c.empty()) dispatch("clip delete " + c); }},
        });
        ms.setItems(3, {
            {mProject->dockOpen() ? "Hide Mixer" : "Show Mixer", [this] { mProject->setDockOpen(!mProject->dockOpen()); mMenuKey.clear(); refreshMenus(mHooks.model ? mHooks.model() : emptyModel()); }},
            {mProject->browserOpen() ? "Hide Browser" : "Show Browser", [this] { mProject->setBrowserOpen(!mProject->browserOpen()); mMenuKey.clear(); refreshMenus(mHooks.model ? mHooks.model() : emptyModel()); }},
            {m.settings.metronome ? "Metronome: On" : "Metronome: Off", [this] {
                 const auto &mm = mHooks.model ? mHooks.model() : emptyModel();
                 dispatch(std::string("settings set metronome=") + (mm.settings.metronome ? "off" : "on"));
             }},
            {"Settings\xE2\x80\xA6   (Ctrl+,)", [this] { openSettings(); }},
        });
    }

    void App::layoutAll()
    {
        for (Segment *s : {(Segment *)mHome.get(), (Segment *)mProject.get(), (Segment *)mSettings.get(), (Segment *)mModalRoot.get(), (Segment *)mConfirm.get(),
                           (Segment *)mMenu.get()})
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
        // live state moves without a revision: the playing transport, a preview being heard (and its end)
        if (m.revision == mBoundRevision && !m.transport.playing && !m.audition.playing && m.audition.playing == mBoundAudition) return;
        mBoundRevision = m.revision;
        mBoundAudition = m.audition.playing;
        mHome->bind(m);
        mProject->bind(m);
        mSettings->bind(m);
        refreshMenus(m);
        if (m.settings.reducedMotion != mAppReducedMotion)
        {
            mAppReducedMotion = m.settings.reducedMotion;
            artboard::setReducedMotion(mOsReducedMotion || mAppReducedMotion); // the setting, or the OS's (design rule §2.6)
        }
    }

    bool App::needsRedraw(double nowMs) const
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        if (m.revision != mBoundRevision || m.transport.playing || m.audition.playing || m.audition.playing != mBoundAudition) return true;
        if (mHome->opacity.isAnimating() || mProject->opacity.isAnimating() || mToast.isAnimating() || mToast.value() > 0.0) return true;
        if (mSettings->appearAmount() > 0.0 || mMenu->isOpen()) return true;
        return nowMs - mLastActivityMs < kActiveWindowMs;
    }

    void App::pointer(int kind, double x, double y, int button, double timeMs, bool alt, bool shift, bool ctrl)
    {
        mLastActivityMs = mNowMs;
        if (kind == 0)
        {
            mProject->setInteracting(true);
            // a press outside the open menu closes it first, as cosmo's does; the press then does its own thing
            auto &ms = mProject->bar().menus();
            if (ms.openIndex() >= 0 && !ms.pointInActiveArea(ms.toLocal(Point{x, y}))) ms.close();
        }
        if (kind == 2) mProject->setInteracting(false);
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
        if (mMenu->isOpen()) { mMenu->dispatchKey(e); return true; }
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
            if (e.ctrl && ((e.keyCode == 'Z' && e.shift) || e.keyCode == 'Y')) return dispatch("redo"); // R-EDM-1
            if (e.ctrl && e.keyCode == 'Z') return dispatch("undo");
            const std::string sel = mProject->timeline().selectedClip();
            if (!sel.empty() && (e.keyCode == 46 || e.keyCode == 8)) return dispatch("clip delete " + sel); // Delete / Backspace
            if (!sel.empty() && e.ctrl && e.keyCode == 'D') return dispatch("clip duplicate " + sel);     // a linked copy after it
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
