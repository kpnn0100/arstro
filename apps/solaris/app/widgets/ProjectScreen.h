/*
 *  solaris_ui — ProjectScreen: the song (R-UI-3, R-BROWSE-3, R-LANE-1).
 *
 *  The SongBar on top; the Browser on the left; the Timeline in the centre with the MixerDock docked
 *  under it (R-UI-3: docked, not a screen of its own, so a clip and its strip are in view together).
 *  The dock's height is the view's: its top edge dragged follows the pointer; its chevron folds it
 *  down to its tab bar and back, eased; and it gives way before the lanes do — the lanes keep at
 *  least `kLanesFloor`, so a short window shrinks the dock (eased) rather than the lanes to nothing.
 *
 *  This class owns what crosses widgets: a DRAG from the browser to the lanes. The browser reports
 *  the pointer; this asks the timeline what lies under it (a lane, a beat — or below the last lane:
 *  a new one), shows the timeline's drop hint and a ghost of the item over everything, and on
 *  release turns the drop into ONE command line (R-BROWSE-3):
 *
 *    a sample      → `clip add --src "<file>" --at <beat> [--lane <ln>]`  (its own strip, R-MIX-2)
 *    an instrument → `clip add --instrument <type> --at <beat> --length 4 [--lane <ln>]`
 *    an effect     → said: it goes on a strip's rack (the dock's "+ Effect")
 *
 *  A double-click in the browser does the same at the playhead, on a new lane.
 */
#pragma once
#include "../Theme.h"
#include "AppModel.h"
#include "Browser.h"
#include "MixerDock.h"
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
        MixerDock &dock() { return *mDock; }
        static constexpr double kLanesFloor = 130.0;  // space::u(40): the lanes never shrink below this
        double dockHeight() const { return mDockH.value(); } // LIVE
        void setDockOpen(bool open) { mDockOpen = open; }
        bool dockOpen() const { return mDockOpen; }
        /** View › Browser: the left panel folds away (its width eased) and comes back. */
        void setBrowserOpen(bool open) { mBrowserOpen = open; }
        bool browserOpen() const { return mBrowserOpen; }
        double browserWidth() const { return mBrowserW.value(); } // LIVE
        /** The pointer is down somewhere: a gesture in flight outranks the model (the device panel). */
        void setInteracting(bool on) { mInteracting = on; }

        /** Dispatch a line; true when the service took it (the App's dispatch, which toasts a refusal). */
        std::function<bool(const std::string &line)> onCommand;
        std::function<void(const std::string &)> onNotice;
        std::function<void(std::vector<cosmo_v2::ContextMenu::Item> items, artboard::Point world)> onMenu;
        std::function<void(const std::string &current, artboard::Point world, std::function<void(const std::string &)> done)> onRename;

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
        double dockTarget() const;

        std::shared_ptr<SongBar> mBar;
        std::shared_ptr<Browser> mBrowser;
        std::shared_ptr<Timeline> mTimeline;
        std::shared_ptr<MixerDock> mDock;
        double mDockWant = 429.0;          // space::u(132): what the user last dragged it to
        bool mDockOpen = true, mDockInit = false, mInteracting = false;
        double mDockLast = 0.0;
        artboard::AnimatedProperty mDockH{0.0};
        bool mBrowserOpen = true, mBrowserInit = false, mBrowserLast = true;
        artboard::AnimatedProperty mBrowserW{0.0};
        double mNowMs = 0.0, mPosition = 0.0;
        artboard::AnimatedProperty mGhost{0.0};
        bool mGhostWanted = false;
        artboard::Point mGhostAt{0, 0};
        std::string mGhostLabel;
    };
}
}
