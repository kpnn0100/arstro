#include "App.h"
#include <algorithm>
#include <cmath>

using namespace artboard;

namespace arstro
{
namespace interstellar_v1
{
    App::App(interstellar::InterstellarService &svc, double width, double height)
        : mSvc(svc), mW(width), mH(height)
    {
        mRoot = std::make_shared<Segment>();
        mBar = std::make_shared<WorkspaceBar>();
        mMonitor = std::make_shared<Monitor>();
        mTransport = std::make_shared<Transport>();
        mTimeline = std::make_shared<TimelineView>();
        mLanes = std::make_shared<LaneStack>();

        // Child order == draw order == reverse hit-test order.
        mRoot->addChild(mMonitor);
        mRoot->addChild(mTimeline);
        mRoot->addChild(mLanes);
        mRoot->addChild(mTransport);
        mRoot->addChild(mBar);   // chrome last, so a menu would open over the deck

        mBar->onWorkspace = [this](interstellar::Workspace w) { showWorkspace(w); };

        // Every control funnels through ONE place that turns a gesture into a Command — the
        // pattern cosmo's EditCommands establishes, and the reason the GUI has no privileged
        // path to the timeline (R-G-4).
        mTimeline->onScrub = [this](double t) {
            interstellar::Command c;
            c.kind = interstellar::Command::Kind::Playhead;
            c.name = std::to_string(t);
            c.ms = t;
            mSvc.dispatch(c);
        };
        mTimeline->onClipMoved = [this](const std::string &clip, double at) {
            interstellar::Command c;
            c.kind = interstellar::Command::Kind::ClipMove;
            c.name = clip;
            // The SNAPPED value is what the command carries: the service must never receive an
            // unsnapped value and re-derive it.
            c.fields.emplace_back("at", interstellar::canonicalTime(at));
            mSvc.dispatch(c);
        };
        mTransport->onScrub = mTimeline->onScrub;
        mTransport->onPlayPause = [this](bool play) {
            interstellar::Command c;
            c.kind = play ? interstellar::Command::Kind::Play : interstellar::Command::Kind::Pause;
            mSvc.dispatch(c);
        };
        mTransport->onStep = [this](int dir) {
            interstellar::Command c;
            c.kind = interstellar::Command::Kind::Playhead;
            c.name = dir > 0 ? "next-cut" : "prev-cut";
            mSvc.dispatch(c);
        };

        mTimeline->setPixelsPerSecond(60.0);   // the FIRST placement SETS; there is nowhere to
        mWorkspaceFade.set(1.0);               // travel from
        layout();
        syncFromModel();
    }

    void App::setSize(double width, double height)
    {
        mW = width;
        mH = height;
        layout();
    }

    void App::showWorkspace(interstellar::Workspace w)
    {
        if (w == mWorkspace) return;
        mFadeFrom = mWorkspace;
        mWorkspace = w;
        mBar->setWorkspace(w);
        // A setter has no clock. Record the intent; `advance` starts the cross-fade.
        mFadePending = true;
    }

    void App::layout()
    {
        // Fixed rows first, the flexible one last, with std::max(0.0, remainder) — so a window
        // too small to hold the chrome degrades instead of producing negative geometry.
        const double barH = WorkspaceBar::kHeight;
        const double transportH = time::transportHeight();
        const double deckH = std::min(time::deckHeight(), std::max(0.0, mH - barH - transportH - 160.0));

        mBar->x.set(0); mBar->y.set(0);
        mBar->width.set(mW); mBar->height.set(barH);

        const double monitorH = std::max(0.0, mH - barH - transportH - deckH);
        mMonitor->x.set(0); mMonitor->y.set(barH);
        mMonitor->width.set(mW); mMonitor->height.set(monitorH);

        mTransport->x.set(0); mTransport->y.set(barH + monitorH);
        mTransport->width.set(mW); mTransport->height.set(transportH);

        const double deckY = barH + monitorH + transportH;
        for (auto *deck : {(Segment *)mTimeline.get(), (Segment *)mLanes.get()})
        {
            deck->x.set(0);
            deck->y.set(deckY);
            deck->width.set(mW);
            deck->height.set(deckH);
        }
    }

    void App::refreshLanes()
    {
        const auto &p = mSvc.project();
        std::vector<LaneStack::LaneRow> rows;
        for (const auto &l : p.autoLinks)
        {
            const interstellar::AutoClip *shape = p.autoClip(l.clip);
            if (!shape) continue;
            LaneStack::LaneRow r;
            r.address = l.target;
            r.shapeName = shape->name;
            // How many links share this shape — the affordance that makes the shared-shape model
            // discoverable rather than surprising.
            r.shapeUsers = 0;
            for (const auto &o : p.autoLinks)
                if (o.clip == l.clip) ++r.shapeUsers;
            r.at = l.at;
            if (!l.scope.empty())
                if (const interstellar::Clip *sc = p.clip(l.scope)) r.at = sc->at + l.at;
            r.dur = l.dur > 0 ? l.dur : shape->dur;
            r.from = l.from;
            r.to = l.to;
            // Sample the shape into the lane's own 0..1 box, so the drawn curve is the shape the
            // renderer will actually use rather than a straight line between endpoints.
            for (int i = 0; i <= 24; ++i)
            {
                const double u = i / 24.0;
                const double v = shape->value(u * shape->dur);
                r.curve.emplace_back(u, std::max(0.0, std::min(1.0, v)));
            }
            rows.push_back(std::move(r));
        }
        // The lint's own answer, drawn on the lane it belongs to.
        for (const auto &f : mSvc.lint())
            for (auto &r : rows)
                if (r.address == f.address && f.detail.rfind("steps by", 0) == 0)
                    r.stepsOnEntry = true;
        mLanes->setLanes(std::move(rows));
    }

    void App::syncFromModel()
    {
        const auto &m = mSvc.model();
        mBar->setProjectName(m.projectName);
        mBar->setDirty(m.dirty);
        mTimeline->setModel(m);
        mTimeline->setPlayhead(m.playhead);
        mLanes->setTimeAxis(mTimeline->pixelsPerSecond(), mTimeline->scroll());
        mLanes->setPlayhead(m.playhead);
        mTransport->setPlayhead(m.playhead, m.duration);
        mTransport->setPlaying(m.playing);
        mTransport->setFps(m.fps);

        if (m.revision != mSeenRevision)
        {
            mSeenRevision = m.revision;
            refreshLanes();
        }

        // The monitor's frame. Rendering it here — in the view, per frame — is deliberate: the
        // SERVICE composites, and the view asks for the frame it needs for the time it is
        // showing (R-SVC-4: which frame to display is presentation).
        if (mSvc.renderFrame(m.playhead, mFrame))
            mMonitor->setFrame(mFrame, m.frameSeq);
        else
        {
            mMonitor->setFrame(interstellar::Raster{}, m.frameSeq);
            mMonitor->setEmptyReason(m.clips.empty() ? "no clips yet — cut something on the timeline"
                                                     : "no clip at the playhead");
        }
        char tc[40];
        const long long f = (long long)std::llround(m.playhead * m.fps);
        const long long fps = std::max(1LL, (long long)std::llround(m.fps));
        std::snprintf(tc, sizeof tc, "%02lld:%02lld:%02lld:%02lld", (f / fps) / 3600,
                      ((f / fps) / 60) % 60, (f / fps) % 60, f % fps);
        mMonitor->setTimecode(tc);
    }

    void App::advance(double nowMs)
    {
        mLastMs = nowMs;
        if (mFadePending)
        {
            // 260 ms — cosmo's shell cross-fade. The columns swap through opacity, never through
            // `visible`, which is what keeps the switch from being a single-frame pop.
            mWorkspaceFade.set(0.0);
            mWorkspaceFade.animateTo(1.0, 260.0, Easing::EaseOutCubic, nowMs);
            mFadePending = false;
        }
        mWorkspaceFade.update(nowMs);

        // The deck shows the timeline in Cut and the lanes in Mix, cross-fading between them.
        // Both are advanced either way: gating `advance` on visibility freezes a close mid-fade,
        // which is the trap arstro.design.rule §1 names explicitly.
        const bool lanesUp = mWorkspace == interstellar::Workspace::Mix;
        const double f = std::max(0.0, std::min(1.0, mWorkspaceFade.value()));
        mLanes->opacity.set(lanesUp ? f : 1.0 - f);
        mTimeline->opacity.set(lanesUp ? 1.0 - f : f);

        mRoot->advance(nowMs);
    }

    void App::render(IRenderTarget &target, double nowMs)
    {
        advance(nowMs);
        syncFromModel();
        layout();   // every frame, so geometry stays correct while a property is mid-tween

        target.setFill(palette::background());
        target.beginPath();
        target.moveTo(0, 0); target.lineTo(mW, 0); target.lineTo(mW, mH); target.lineTo(0, mH);
        target.closePath();
        target.fillPath();

        mRoot->render(target);
        mRoot->renderOverlay(target);
    }

    void App::pointer(int kind, double x, double y, int button, double timeMs)
    {
        Gesture g;
        g.type = kind == 0 ? Gesture::Type::Down : (kind == 1 ? Gesture::Type::Move : Gesture::Type::Up);
        (void)button;
        (void)timeMs;
        g.pos = Point{x, y};
        g.start = g.pos;
        mRoot->onGesture(g);
        if (kind == 2)
        {
            // A Click is hit-tested fresh at the RELEASE point, which is why a widget that wants
            // one must be hittable there.
            Gesture c = g;
            c.type = Gesture::Type::Click;
            mRoot->onGesture(c);
        }
    }

    void App::wheel(double x, double y, double delta, bool ctrl)
    {
        Gesture g;
        g.type = Gesture::Type::Scroll;
        g.pos = Point{x, y};
        g.delta = Point{0.0, delta};
        g.ctrl = ctrl;
        mRoot->onGesture(g);
    }
}
}
