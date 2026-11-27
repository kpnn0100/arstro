/*
 *  arstro-shell — Scene: where everything is, this frame (R-NAV-1..7, R-ANIM-1..4).
 *
 *  The model says WHAT is in front (home, an app, recents; shade open or not). The Scene turns
 *  that into poses — a rectangle, a corner radius and an opacity per app card, plus a handful of
 *  scalars for the home layer, the shade and the back hint — and moves them there.
 *
 *  Two rules shape it:
 *
 *   - Everything is a critically damped spring, never a fixed-length tween, because every one of
 *     these values can be grabbed by a finger mid-flight. A spring is re-aimed from wherever it
 *     is; a tween restarted from the middle changes speed in one frame.
 *   - While a system gesture is in flight the finger IS the animation: the value is set straight
 *     from the gesture, with no easing. On release the spring carries on from that value.
 *
 *  An app card is a CONTAINER whose size changes; the app's picture inside it is only ever scaled
 *  uniformly and cropped (arstro.rule.animation, Rule 1). A card that leaves fades while it
 *  travels and is gone exactly when it arrives (Rule 3).
 *
 *  Pure: no SDL, no Cairo, no clock of its own. Lengths are dp.
 */
#pragma once
#include "../core/ShellService.h"
#include "artboard/artboard.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace shell
{
    struct CardPose
    {
        int task = 0;
        std::string app;
        artboard::Rect rect;   // dp
        double alpha = 1.0;
        double radius = 0.0;   // dp
        double header = 0.0;   // opacity of the name chip above the card (recents only)
        double iconMix = 0.0;  // 0 = showing the app .. 1 = it has become its home icon
    };

    class Scene
    {
    public:
        /** Where an app's icon sits on the home screen, if it is there. */
        using IconRect = std::function<bool(const std::string &appId, artboard::Rect &out)>;

        void advance(const Model &m, double dtSeconds, const IconRect &iconRect);

        /** Back to front. Cards that are fully transparent are left out. */
        const std::vector<CardPose> &cards() const { return mPoses; }
        double home() const { return mHome.value(); }      // 1 = the home layer fully shown
        double shade() const { return mShade.value(); }    // 0 closed .. 1 open
        double zoom() const { return mZoom.value(); }      // 0 = an app fills the screen .. 1 = recents row
        double emptyRecents() const { return mEmpty.value(); }  // opacity of "No recent apps"

        struct BackHint
        {
            double amount = 0.0;  // 0..1
            int edge = -1;        // -1 left, +1 right
            double y = 0.0;       // dp
            double pull = 0.0;    // dp the bubble has been dragged in from the edge
        };
        BackHint back() const { return {mBack.value(), mBackEdge, mBackY, mBackPull.value()}; }

        /** True while anything is still travelling — the host draws frames until this is false. */
        bool animating() const { return mAnimating; }

        /** A touch that is not a system gesture, while recents is showing: scroll the row, flick a
         *  card away, tap a card. Appends the commands that result. */
        void recentsTouch(const TouchSample &s, const Model &m, std::vector<std::string> &commands);
        /** The system took the touch: forget the drag in progress. */
        void cancelTouch();

        // Read by tests: the live values a snap-versus-ease assertion needs.
        double presenceOf(int task) const;
        double scroll() const { return mScroll.value(); }

        static constexpr double kRecentsScale = 0.62;  // a recents card is the screen, this much smaller
        static constexpr double kRecentsGap = 20.0;

    private:
        struct Card
        {
            int task = 0;
            std::string app;
            artboard::Spring presence{0.0};  // 0 = gone .. 1 = in the row
            artboard::Spring slot{0.0};      // its place in the row (0 = most recent)
            artboard::Spring dismiss{0.0};   // dp it has been flicked upward
            bool toIcon = false;             // it collapses into its home icon, not just fades
            artboard::Rect icon;
            int dir = 1;                     // when it merely fades: +1 arrives from below, -1 leaves upward
            bool dying = false;              // its task is gone; it is only finishing its exit
        };
        Card *card(int task);
        artboard::Rect rowRect(const Card &c, double w, double h) const;
        double pitch(double w) const;

        std::vector<Card> mCards;
        std::vector<CardPose> mPoses;
        artboard::Spring mZoom{0.0}, mHome{1.0}, mShade{0.0}, mScroll{0.0};
        artboard::Spring mLiftX{0.0}, mLiftY{0.0}, mBack{0.0}, mBackPull{0.0}, mEmpty{0.0};
        int mBackEdge = -1;
        double mBackY = 0.0;
        NavState mPrevNav = NavState::Home;
        int mPrevFocused = 0;
        bool mGestureSeen = false, mAnimating = false, mFirst = true;
        double mScrollTarget = 0.0;

        // recents drag
        enum class Drag { None, Pending, Scroll, Dismiss };
        Drag mDrag = Drag::None;
        int mDragTask = 0;
        double mDownX = 0, mDownY = 0, mDownScroll = 0, mLastX = 0, mLastY = 0, mLastT = 0, mVelX = 0, mVelY = 0;
        double mW = 1280, mH = 800;
    };
}
}
