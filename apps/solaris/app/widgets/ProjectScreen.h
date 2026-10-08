/*
 *  solaris_ui — ProjectScreen: the song (R-UI-3, R-BROWSE-3, R-LANE-1).
 *
 *  The SongBar on top; the Browser on the left; the Timeline in the centre (the mixer dock joins it
 *  below with U3). This class owns what crosses widgets: a DRAG from the browser to the lanes. The
 *  browser reports the pointer; this asks the timeline what lies under it (a lane, a beat — or below
 *  the last lane: a new one), shows the timeline's drop hint and a ghost of the item over everything,
 *  and on release turns the drop into command lines:
 *
 *    a sample      → `clip add --src "<file>" --at <beat> [--lane <ln>]`  (its own strip, R-MIX-2)
 *    an instrument → `strip add --kind instrument --instrument <type>`, then
 *                    `clip add --strip <the new strip> --at <beat> [--lane <ln>]`
 *    an effect     → said: it goes on a mixer strip (the dock, U3)
 *
 *  A double-click in the browser does the same at the playhead, on a new lane.
 */
#pragma once
#include "../Theme.h"
#include "AppModel.h"
#include "Browser.h"
#include "SongBar.h"
#include "Timeline.h"
#include <functional>
#include <memory>

namespace arstro
{
namespace solaris_ui
{
    class ProjectScreen : public artboard::Segment
    {
    public:
        ProjectScreen();
        void bind(const solaris::AppModel &m);
        void layout();
        SongBar &bar() { return *mBar; }
        Browser &browser() { return *mBrowser; }
        Timeline &timeline() { return *mTimeline; }

        /** Dispatch a line; true when the service took it (the App's dispatch, which toasts a refusal). */
        std::function<bool(const std::string &line)> onCommand;
        std::function<void(const std::string &)> onNotice;

        /** The ghost's live opacity (a test tells a fade from a cut). */
        double ghostAmount() const { return mGhost.value(); }

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        void onOverlay(artboard::IRenderTarget &t) const override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        bool place(const Browser::Item &item, double beat, int row);
        bool overLanes(const artboard::Point &world, double &beat, int &row) const;

        std::shared_ptr<SongBar> mBar;
        std::shared_ptr<Browser> mBrowser;
        std::shared_ptr<Timeline> mTimeline;
        double mNowMs = 0.0, mPosition = 0.0;
        artboard::AnimatedProperty mGhost{0.0};
        bool mGhostWanted = false;
        artboard::Point mGhostAt{0, 0};
        std::string mGhostLabel;
    };
}
}
