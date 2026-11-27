/*
 *  arstro-shell — Navigator: the system gestures (R-GEST-1..6).
 *
 *  The one place that decides whether a touch belongs to the system or to whatever is under the
 *  finger. It lives in the core, not in a view, because a second front end would otherwise have
 *  to re-derive "how far is far enough to go home" and would answer differently.
 *
 *  A press in an edge zone is only a CANDIDATE: it is still delivered to the app, exactly as a
 *  tap there must be. Once the finger has travelled past the slop in the gesture's direction
 *  the Navigator CLAIMS the touch; the host then cancels it in the app and every later sample
 *  is the system's. That is Android's contract, and it is what keeps the bottom 24dp of an app
 *  tappable.
 *
 *  Pure: no clock of its own (time arrives on each sample), no model access. The service feeds
 *  it and applies the result.
 */
#pragma once
#include "Model.h"
#include <vector>

namespace arstro
{
namespace shell
{
    struct TouchSample
    {
        enum class Kind { Down, Move, Up, Cancel };
        Kind kind = Kind::Down;
        double x = 0.0, y = 0.0;  // dp
        double tMs = 0.0;
    };

    /** The thresholds, named so the API document and the tests read the same numbers (dp, dp/s). */
    namespace gesture
    {
        constexpr double kBottomZone = 24.0;    // press here to start Home
        constexpr double kSideZone = 20.0;      // press here (inside an app) to start Back
        constexpr double kTopZone = 32.0;       // press here to pull the shade down
        constexpr double kShadeCloseZone = 72.0;// with the shade open: press here to push it up
        constexpr double kSlop = 10.0;          // travel before a candidate is claimed
        constexpr double kBackCommit = 56.0;    // sideways travel that means Back
        constexpr double kMinTravel = 28.0;     // less than this is never a decision
        constexpr double kFlingSpeed = 700.0;   // upward speed that means Home from an app
        constexpr double kRecentsFraction = 0.12;  // of the screen height: pause here for Recents
        constexpr double kHomeFraction = 0.45;     // of the screen height: this far is Home anyway
        constexpr double kShadeCommit = 0.4;       // of the shade travel
        constexpr double kShadeFling = 500.0;
        constexpr double kVelocityWindowMs = 90.0;
    }

    class Navigator
    {
    public:
        struct Feed
        {
            bool claimedNow = false;  // this sample turned a candidate into a system gesture
            bool consumed = false;    // the sample belongs to the system; do not deliver it
            bool ended = false;       // the gesture finished on this sample
            GestureResult result = GestureResult::Cancelled;
        };

        /** `nav`/`shadeOpen` are the state the press started in; `w`,`h` the screen in dp. */
        Feed feed(const TouchSample &s, NavState nav, bool shadeOpen, double w, double h);

        const GestureModel &gesture() const { return mGesture; }
        bool active() const { return mGesture.kind != GestureKind::None; }

    private:
        GestureResult decide(double w, double h) const;
        void velocity(double &vx, double &vy) const;

        GestureKind mCandidate = GestureKind::None;
        GestureModel mGesture;
        NavState mNavAtDown = NavState::Home;
        int mEdge = 0;
        double mDownX = 0.0, mDownY = 0.0;
        bool mPressed = false;
        std::vector<TouchSample> mSamples;  // the recent window, for the release velocity
    };
}
}
