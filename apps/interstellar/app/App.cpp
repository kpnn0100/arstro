#include "App.h"
#include "widgets/CommandLine.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace interstellar_v1
{
    using namespace artboard;
    using interstellar::Screen;

    namespace
    {
        constexpr double kActiveWindowMs = 700.0;   // longer than the longest tween (520 ms in cosmo)
        constexpr int kKeyBackspace = 8, kKeyEsc = 27, kKeySpace = 32, kKeyLeft = 37, kKeyRight = 39, kKeyDelete = 46;

        /** Display-referred 256-bin histogram of the monitor frame — what the reused cosmo
         *  HistogramWidget plots. The pixels are the view's own (it is showing them). */
        HistogramData histogramOf(const interstellar::Raster &r)
        {
            HistogramData h;
            const size_t n = (size_t)r.width * r.height;
            const size_t step = std::max<size_t>(1, n / 65536);   // sample: the plot is 280 px wide
            for (size_t i = 0; i < n; i += step)
            {
                const uint8_t *p = &r.rgba[i * 4];
                h.r[p[0]]++; h.g[p[1]]++; h.b[p[2]]++;
                h.lum[std::min(255, (int)std::lround(0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]))]++;
            }
            for (int b = 0; b < HistogramData::kBins; ++b)
                h.maxCount = std::max({h.maxCount, h.r[b], h.g[b], h.b[b], h.lum[b]});
            return h;
        }
    }

    const interstellar::AppModel &App::emptyModel()
    {
        static const interstellar::AppModel m;
        return m;
    }

    App::App(AppHooks hooks, double width, double height)
        : mHooks(std::move(hooks)), mW(width), mH(height)
    {
        installInterstellarAccent();   // FIRST: before any widget reads the accent

        mHome = std::make_shared<HomeScreen>();
        mHome->thumbnail = mHooks.thumbnail;
        mHome->onNewProject = [this] { if (onPickProjectToCreate) onPickProjectToCreate(); };
        mHome->onOpenProject = [this] { if (onPickProjectToOpen) onPickProjectToOpen(); };
        mHome->onSettings = [this] { mSettings->show(); };
        mHome->onOpenRecent = [this](int i) {
            const auto &m = mHooks.model ? mHooks.model() : emptyModel();
            std::vector<interstellar::RecentModel> rec = m.recents;
            std::stable_sort(rec.begin(), rec.end(), [](const auto &a, const auto &b) { return a.lastOpened > b.lastOpened; });
            if (i < 0 || i >= (int)rec.size()) return;
            mLoading->setProjectName(rec[i].name);   // the loading screen names what is opening
            dispatch("project open " + cmd::quote(rec[i].path));
        };
        mLoading = std::make_shared<LoadingView>();
        mEdit = std::make_shared<EditScreen>();
        mEdit->onCommand = [this](const std::string &l) { return dispatch(l); };
        mEdit->onAddFootage = [this] { if (onPickFootage) onPickFootage(); };
        mEdit->onHome = [this] { requestHome(); };
        mEdit->gradeDeck()->thumbnail = mHooks.thumbnail;
        mSettings = std::make_shared<SettingsDialog>();

        mRecognizer.setSink([this](const Gesture &g) {
            if (mScreen == Screen::Home)
            {
                if (mSettings->isOpen()) mSettings->onGesture(g);
                else mHome->onGesture(g);
            }
            else if (mScreen == Screen::Edit)
                mEdit->onGesture(g);
            // Screen::Loading swallows input: the transition is not interactive
        });
        layoutAll();
    }

    void App::setSize(double width, double height)
    {
        mW = std::max(1.0, width);
        mH = std::max(1.0, height);
        noteActivity();
        layoutAll();
    }

    void App::layoutAll()
    {
        for (Segment *s : std::initializer_list<Segment *>{mHome.get(), mLoading.get(), mEdit.get(), mSettings.get()})
        {
            s->x.set(0); s->y.set(0); s->width.set(mW); s->height.set(mH);
        }
        mHome->layout();
        mEdit->layout();
    }

    double App::screenOpacity(Screen s) const
    {
        switch (s)
        {
        case Screen::Home: return mHome->opacity.value();
        case Screen::Loading: return mLoading->opacity.value();
        default: return mEdit->opacity.value();
        }
    }

    Segment *App::activeRoot()
    {
        switch (mScreen)
        {
        case Screen::Home: return mHome.get();
        case Screen::Loading: return mLoading.get();
        default: return mEdit.get();
        }
    }

    bool App::dispatch(const std::string &line)
    {
        noteActivity();
        if (!mHooks.dispatch) return false;
        std::string err;
        const bool ok = mHooks.dispatch(line, err);
        if (!ok) mEdit->showRefusal(err.empty() ? "Refused: " + line : err);
        return ok;
    }

    void App::openProjectPicked(const std::string &path)
    {
        if (path.empty()) return;
        const auto slash = path.find_last_of('/');
        mLoading->setProjectName(slash == std::string::npos ? path : path.substr(slash + 1));
        dispatch("project open " + cmd::quote(path));
    }

    void App::newProjectPicked(const std::string &path)
    {
        if (path.empty()) return;
        const auto slash = path.find_last_of('/');
        mLoading->setProjectName(slash == std::string::npos ? path : path.substr(slash + 1));
        dispatch("project new " + cmd::quote(path));
    }

    void App::footagePicked(const std::vector<std::string> &paths)
    {
        if (paths.empty()) return;
        std::string line = "rack add";
        for (const auto &p : paths) line += " " + cmd::quote(p);
        dispatch(line);
    }

    void App::requestHome()
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        if (!m.dirty) { dispatch("project close"); return; }
        mEdit->confirm()->show("Save changes to " + (m.projectName.empty() ? std::string("this project") : m.projectName) + "?",
                               "Edits since the last save are lost if you close without saving.",
                               {{"Cancel", false, false, [] {}},
                                {"Don't save", true, false, [this] { dispatch("project close"); }},
                                {"Save", false, true, [this] { if (dispatch("project save")) dispatch("project close"); }}});
    }

    bool App::textEditing() const
    {
        Segment *f = Segment::focusedInGroup(0);
        if (!f || !f->hasFocus() || !dynamic_cast<TextBox *>(f)) return false;
        // focused AND actually on screen: every ancestor shown, and its tab page the one up
        for (const Segment *s = f; s; s = s->parent())
        {
            if (s->isFadedOut()) return false;
            if (auto *page = dynamic_cast<const FadePage *>(s); page && !page->shown()) return false;
        }
        return true;
    }

    void App::pointer(int kind, double x, double y, int button, double timeMs, bool alt, bool shift, bool ctrl)
    {
        noteActivity();
        const RawPointer::Kind k = kind == 0 ? RawPointer::Kind::Down : (kind == 2 ? RawPointer::Kind::Up : RawPointer::Kind::Move);
        if (k == RawPointer::Kind::Down) mPointerDown = true;
        RawPointer rp{};
        rp.kind = k;
        rp.pos = Point{x, y};
        rp.button = button == 2 ? PointerButton::Right : PointerButton::Left;
        rp.timeMs = timeMs;
        rp.alt = alt; rp.shift = shift; rp.ctrl = ctrl;
        mRecognizer.feed(rp);
        if (k == RawPointer::Kind::Up) mPointerDown = false;
    }

    void App::wheel(double x, double y, double notches, bool ctrl)
    {
        noteActivity();
        RawPointer rp{};
        rp.kind = RawPointer::Kind::Scroll;
        rp.pos = Point{x, y};
        rp.timeMs = mNowMs;
        rp.ctrl = ctrl;
        rp.scroll = Point{0.0, -notches * shell::wheelNotchPx()};   // + notches = wheel up = toward the start
        mRecognizer.feed(rp);
    }

    bool App::key(const KeyEvent &e)
    {
        noteActivity();
        if (mScreen == Screen::Loading) return true;
        if (mScreen == Screen::Home)
        {
            if (mSettings->isOpen()) return mSettings->handleKey(e);
            mHome->dispatchKey(e);
            return true;   // the launcher owns the keyboard
        }
        // Edit: a modal owns the keyboard while it is up
        if (mEdit->confirm()->isOpen()) return mEdit->confirm()->handleKey(e) || true;
        if (mEdit->namePrompt()->isOpen()) return mEdit->namePrompt()->handleKey(e);
        auto versions = mEdit->topBar()->versions();
        if (versions->isOpen())
        {
            if (e.type == KeyEvent::Type::Down && e.keyCode == kKeyEsc) versions->close();
            return true;
        }
        if (textEditing()) return mEdit->dispatchKey(e);
        if (e.type != KeyEvent::Type::Down) return false;

        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        const double fps = m.fps > 0 ? m.fps : 24.0;
        switch (e.keyCode)
        {
        case kKeySpace: dispatch(m.playing ? "pause" : "play"); return true;
        case kKeyLeft: dispatch("playhead " + cmd::seconds(m.playhead - 1.0 / fps, fps)); return true;
        case kKeyRight: dispatch("playhead " + cmd::seconds(m.playhead + 1.0 / fps, fps)); return true;
        case kKeyDelete:
        case kKeyBackspace:
            if (mEdit->tab() == EditScreen::Cut && !m.selectedClip.empty()) { dispatch("clip delete " + cmd::quote(m.selectedClip)); return true; }
            return false;
        case 'S':
            if (mEdit->tab() == EditScreen::Cut && !m.selectedClip.empty())
            {
                dispatch("clip split " + cmd::quote(m.selectedClip) + " --at " + cmd::seconds(m.playhead, fps));
                return true;
            }
            return false;
        case '1': case '2': case '3':
            mEdit->setTab(e.keyCode - '1');
            return true;
        default:
            return false;
        }
    }

    void App::bindIfStale(double nowMs)
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        if (mBound && m.revision == mSeenRevision)
        {
            if (m.screen == Screen::Edit) fetchFrame(m, false);   // the monitor may have resized
            return;
        }
        mBound = true;
        mSeenRevision = m.revision;
        noteActivity();
        // Home's recents are unknown until the service has published once (revision 0):
        // that is the loading state, drawn as skeleton cards.
        mHome->setLoading(m.revision == 0);
        mHome->bind(m);
        if (m.screen != Screen::Home && !m.projectName.empty()) mLoading->setProjectName(m.projectName);
        mEdit->bind(m, mPointerDown, nowMs);
        if (m.screen == Screen::Edit) fetchFrame(m, false);
    }

    void App::fetchFrame(const interstellar::AppModel &m, bool force)
    {
        auto mon = mEdit->monitor();
        const int edge = mon->wantedProxyEdge();
        mon->setProxyEdge(edge);
        mon->setTimecode(cmd::timecode(m.playhead, m.fps));
        std::string cap;
        for (const auto &tl : m.timelines) if (tl.id == m.currentTimeline) cap = tl.name.empty() ? tl.id : tl.name;
        if (m.width > 0 && m.height > 0) cap += (cap.empty() ? "" : "  \xC2\xB7  ") + std::to_string(m.width) + "\xC3\x97" + std::to_string(m.height);
        mon->setCaption(cap);

        // is there a picture to show? (a clip under the playhead in the resolved timeline)
        bool clipHere = false;
        for (const auto &c : m.clips)
        {
            if (c.audio) continue;
            const double dur = c.duration > 0 ? c.duration : (c.out - c.in) / std::max(1e-6, c.speed);
            if (m.playhead >= c.at - 1e-9 && m.playhead < c.at + dur) { clipHere = true; break; }
        }
        if (!clipHere) { mon->setState(Monitor::State::Empty); return; }

        const bool changed = force || m.frameSeq != mFetchedSeq || edge != mFetchedEdge || m.currentTimeline != mFetchedTimeline ||
                             std::fabs(m.playhead - mFetchedAt) > 1e-9 || m.revision != mFetchedRevision;
        if (!changed) return;
        const bool samePlace = std::fabs(m.playhead - mFetchedAt) <= 1e-9;
        const bool scrubbing = mEdit->transport()->scrubbing() || mEdit->timeline()->dragging();
        mFetchedSeq = m.frameSeq;
        mFetchedEdge = edge;
        mFetchedAt = m.playhead;
        mFetchedTimeline = m.currentTimeline;
        mFetchedRevision = m.revision;
        if (!mHooks.renderFrame || !mHooks.renderFrame(m.playhead, edge, mFrame) || mFrame.empty())
        {
            mon->setState(Monitor::State::Loading);   // a clip is there; its pixels are not (yet)
            return;
        }
        // a new picture at the SAME time is a content change (a grade, a version) and dissolves;
        // a new time is the video moving, which is its own animation (gotcha 10)
        mon->setFrame(mFrame, samePlace && !m.playing && !scrubbing);
        mon->setState(Monitor::State::Frame);
        mEdit->gradeInspector()->setHistogram(histogramOf(mFrame));
    }

    bool App::needsRedraw(double nowMs) const
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        if (m.revision != mPaintedRevision || m.frameSeq != mPaintedFrameSeq) return true;
        if (m.playing || m.screen == Screen::Loading) return true;
        if (mHome->opacity.isAnimating() || mLoading->opacity.isAnimating() || mEdit->opacity.isAnimating()) return true;
        for (const auto &r : m.renders) if (r.state == "running") return true;
        for (const auto &n : m.rack) if (n.pending) return true;   // spinner cells turn
        return nowMs - mLastActivityMs < kActiveWindowMs;
    }

    void App::render(IRenderTarget &target, double nowMs)
    {
        mNowMs = nowMs;
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        bindIfStale(nowMs);

        // screen cross-fade: the model says which screen; the view travels there
        const Screen want = m.screen;
        auto rootOf = [this](Screen s) -> Segment * {
            return s == Screen::Home ? (Segment *)mHome.get() : (s == Screen::Loading ? (Segment *)mLoading.get() : (Segment *)mEdit.get());
        };
        if (!mScreenInit)
        {
            for (Screen s : {Screen::Home, Screen::Loading, Screen::Edit}) rootOf(s)->opacity.set(s == want ? 1.0 : 0.0);
            mScreen = want;
            mScreenInit = true;
        }
        else if (want != mScreen)
        {
            for (Screen s : {Screen::Home, Screen::Loading, Screen::Edit})
                if (s == want || rootOf(s)->opacity.value() > 0.0)
                    rootOf(s)->opacity.animateTo(s == want ? 1.0 : 0.0, motion::kScreenFadeMs, Easing::EaseInOutCubic, nowMs);
            if (want != Screen::Home) mSettings->close();
            mScreen = want;
            noteActivity();
        }
        mRecognizer.advance(nowMs);

        drawRoundedRect(target, Rect{0, 0, mW, mH}, 0.0, Paint::filled(palette::background()));
        target.save();
        target.setTransform(Transform::identity());
        // draw order: the outgoing layer under the incoming one; Edit is laid out every frame
        // (cosmo's convention) so geometry follows any animated property mid-tween
        for (Screen s : {Screen::Home, Screen::Loading, Screen::Edit})
        {
            Segment *r = rootOf(s);
            const bool wanted = s == mScreen;
            if (!wanted && r->opacity.value() <= 0.0 && !r->opacity.isAnimating()) continue;
            if (s == Screen::Home) mHome->layout();
            if (s == Screen::Edit) mEdit->layout();
            r->advance(nowMs);
        }
        for (Screen s : {Screen::Home, Screen::Loading, Screen::Edit})
        {
            Segment *r = rootOf(s);
            if (r->opacity.value() <= Segment::kOpacityEpsilon) continue;
            r->render(target, Transform::identity());
            r->renderOverlay(target, Transform::identity());
        }
        // Settings floats over Home; advanced unconditionally so its close can finish (design rule §1)
        mSettings->advance(nowMs);
        if (mHome->opacity.value() > Segment::kOpacityEpsilon)
        {
            mSettings->render(target, Transform::identity());
            mSettings->renderOverlay(target, Transform::identity());
        }
        target.restore();
        mPaintedRevision = m.revision;
        mPaintedFrameSeq = m.frameSeq;
    }
}
}
