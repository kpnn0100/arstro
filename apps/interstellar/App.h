/*
 *  interstellar — App: the shell. Four workspaces over one project, one monitor that never
 *  leaves, and a service it is a VIEW of (R-UI-1, R-SVC-4).
 *
 *  Two structural decisions, both load-bearing:
 *
 *  * **The Monitor lives OUTSIDE the workspace host.** One widget showing one frame in all four
 *    workspaces; a frame that looks different in two workspaces is a defect (R-UI-2). Inside the
 *    cross-fading host it would be two widgets with two states.
 *  * **Switching workspace is a CROSS-FADE, not a reload.** The project stays loaded, the
 *    playhead does not move, the monitor does not re-render. A workspace switch that reloads
 *    anything is a defect.
 *
 *  Platform-free: it renders through `artboard::IRenderTarget` and dispatches `Command`s. The
 *  host owns the window, the clock and the codecs — which is what lets `interstellar_shots`
 *  build this exact tree with no display.
 */
#pragma once
#include "Theme.h"
#include "core/service/InterstellarService.h"
#include "widgets/LaneStack.h"
#include "widgets/Monitor.h"
#include "widgets/TimelineView.h"
#include "widgets/Transport.h"
#include "widgets/WorkspaceBar.h"
#include <memory>
#include <string>

namespace arstro
{
namespace interstellar_v1
{
    class App
    {
    public:
        App(interstellar::InterstellarService &svc, double width, double height);

        void render(artboard::IRenderTarget &target, double nowMs);
        void advance(double nowMs);
        void setSize(double width, double height);
        void pointer(int kind, double x, double y, int button, double timeMs);
        void wheel(double x, double y, double delta, bool ctrl);

        void showWorkspace(interstellar::Workspace w);
        interstellar::Workspace workspace() const { return mWorkspace; }
        /** The live eased cross-fade of the workspace columns, 0..1. Exposed so a test can
         *  assert it differs from its target mid-transition — the only assertion that can tell
         *  an eased implementation from a snapping one. */
        double workspaceFade() const { return mWorkspaceFade.value(); }

        /** Pull the newest model into every widget. Called from `render`, so a view can never
         *  be a frame behind the service. */
        void syncFromModel();

        // Published for the shot harness and the UI tests — a test that hard-codes where it
        // thinks a widget is keeps passing after the widget moves.
        Monitor &monitor() { return *mMonitor; }
        TimelineView &timeline() { return *mTimeline; }
        LaneStack &lanes() { return *mLanes; }
        Transport &transport() { return *mTransport; }
        WorkspaceBar &bar() { return *mBar; }
        artboard::Segment &root() { return *mRoot; }

    private:
        void layout();
        /** Rebuild the lane rows from the project — the mixer's projection of the links
         *  (R-AUTO-3). A view of the links, never a stored structure. */
        void refreshLanes();

        interstellar::InterstellarService &mSvc;
        double mW = 1440, mH = 900;
        double mLastMs = 0;
        interstellar::Workspace mWorkspace = interstellar::Workspace::Cut;
        interstellar::Workspace mFadeFrom = interstellar::Workspace::Cut;
        artboard::Property mWorkspaceFade{1.0};
        bool mFadePending = false;
        unsigned mSeenRevision = 0;

        std::shared_ptr<artboard::Segment> mRoot;
        std::shared_ptr<WorkspaceBar> mBar;
        std::shared_ptr<Monitor> mMonitor;
        std::shared_ptr<Transport> mTransport;
        std::shared_ptr<TimelineView> mTimeline;
        std::shared_ptr<LaneStack> mLanes;
        interstellar::Raster mFrame;
    };
}
}
