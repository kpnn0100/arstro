/*
 *  cosmo_v2 by arstro — BrandMark: the three app logos and their three animations (R-BRAND).
 *
 *  The copy of record for what these look like and how they move is the brand pages —
 *  docs/brand/app-logos.html (geometry, storyboards, timing tables) — and the two laws,
 *  `arstro.rule.brand` and `arstro.rule.animation`. This file is those pages in C++:
 *
 *    Cosmo         a white ring and one blue dot        (a lens / a planet and its moon)
 *    Interstellar  "Warp": a white ship, three streaks  (a play button and three clips)
 *    Solaris       "Synth sun": a half sun, 3 stripes   (a sun over water / level meters)
 *
 *  It lives in cosmo because Interstellar and Solaris already compile cosmo's widgets (their
 *  R-UI-5 / R-UI-4: linked, never copied), so one file serves all three and a logo cannot drift
 *  between apps.
 *
 *  Why it is split in two:
 *    poseAt()  a PURE function of (mark, phase, time) -> where every part is. No clock, no
 *              target, no state: a test can ask for the pose at any millisecond, which is the
 *              only way to check the animation law by measuring instead of by looking
 *              (arstro.rule.animation's checklist; arstro.rule.design §1 "expose the live value").
 *    draw()    paints one pose into a box.
 *  Player is the small state machine a widget owns to walk a mark through its phases.
 *
 *  The animation law is enforced by construction, not by care:
 *    - Rule 1 (never stretch): a Pose has translations, one rotation and ONE uniform scale.
 *      There is no field that could express a non-uniform scale.
 *    - Rule 2 (speed has a slope): every key names its easing; none is linear.
 *    - Rule 3 (never vanish suddenly): every exit track fades across the move it belongs to,
 *      and Player only leaves the working loop at a loop boundary.
 *
 *  All geometry is on the brand's 96-unit grid and mapped into the caller's box, so the mark
 *  is the same drawing at 16 px and at 160 px.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"

namespace arstro
{
namespace cosmo_v2
{
namespace brand
{
    enum class Mark { Cosmo, Interstellar, Solaris };

    /** Rest = the still logo. In = show up. Idle = the working loop. Out = disappear.
     *  Gone = nothing is drawn. */
    enum class Phase { Rest, In, Idle, Settle, Out, Gone };

    /** How a mark leaves its working loop. The animation law allows exactly two ways: at a loop
     *  boundary, or by easing out of the loop first. */
    enum class Leave
    {
        AtBoundary,   // keep flying until the loop is back at its start pose (up to one period)
        EaseOut       // ease every part home in kSettleMs — for a launch, which must not wait
    };
    constexpr double kSettleMs = 240.0;

    constexpr double kInMs = 1000.0;    // every mark shows up in the same time ...
    constexpr double kOutMs = 720.0;    // ... and leaves in the same time, so the suite has one tempo
    constexpr double kReducedMs = 150.0; // reduced motion: a fade, nothing else moves
    /** One period of the working loop. Every part is back at its start pose at each multiple. */
    double loopMs(Mark m);

    /** Where every part of a mark is. Units are the 96 grid; alphas are 0..1. Only the fields
     *  of the mark being drawn are read. The defaults ARE the rest pose. */
    struct Pose
    {
        double alpha = 1.0;                 // the whole mark (reduced-motion fades, Gone)
        // Cosmo
        double ringScale = 1.0, ringAlpha = 1.0;   // uniform scale about the ring's centre
        double orbitDeg = 0.0;                     // the moon's angle from its rest place
        double dotAlpha = 1.0, trailAlpha = 0.0;
        // Interstellar — Warp
        double shipX = 0.0, shipY = 0.0, shipAlpha = 1.0;
        double barX[3] = {0.0, 0.0, 0.0}, barAlpha[3] = {1.0, 1.0, 1.0};
        double passX[2] = {0.0, 0.0}, passAlpha[2] = {0.0, 0.0};
        // Solaris — Synth sun
        double sunY = 0.0, sunAlpha = 1.0;
        double stripeY[3] = {0.0, 0.0, 0.0}, stripeAlpha[3] = {1.0, 1.0, 1.0};   // top, middle, bottom
    };

    /** The pose `ms` into `phase`. Pure. In and Out clamp at their ends; Idle wraps. */
    Pose poseAt(Mark m, Phase phase, double ms);

    /** The mark's two-tone colour: start at the bottom left, end at the top right. Cosmo's two
     *  are equal — its dot is one flat blue. These are LOGO colours; an app's UI accent is its
     *  own token (arstro.rule.brand §7). */
    void colours(Mark m, artboard::Color &start, artboard::Color &end);

    /** Paint one pose into `box` (the mark fills the largest centred square). `structure` is the
     *  colour of the white parts — foreground on dark, ink on light. */
    void draw(artboard::IRenderTarget &t, const artboard::Rect &box, Mark m, const Pose &pose,
              const artboard::Color &structure, double alpha = 1.0);

    /** Walks one mark through its life. A widget owns one, calls advance() from its own
     *  advance(), and draws pose(). */
    class Player
    {
    public:
        explicit Player(Mark m) : mMark(m) {}
        Mark mark() const { return mMark; }

        /** Show up from nothing, then either fly (the working loop) or come to rest. */
        void showUp(double nowMs, bool thenIdle);
        /** Ask to leave. From the working loop the exit starts at the NEXT loop boundary, never
         *  mid-loop (arstro.rule.animation, Loops); from In it starts when In has finished. */
        void requestOut(double nowMs, Leave how = Leave::AtBoundary);
        /** Stop flying and settle at rest — also at the next loop boundary. */
        void requestRest(double nowMs, Leave how = Leave::AtBoundary);
        void advance(double nowMs);

        Phase phase() const { return mPhase; }
        bool gone() const { return mPhase == Phase::Gone; }
        bool leaving() const { return mPhase == Phase::Out; }
        /** 0..1 through the exit; 0 before it starts. A host fades its own chrome on this. */
        double outProgress() const;
        Pose pose() const;

    private:
        void enter(Phase p, double atMs);
        void latchBoundary(double nowMs);
        Mark mMark;
        Phase mPhase = Phase::Gone;
        double mT0 = 0.0, mNow = 0.0, mBoundary = 0.0;
        bool mThenIdle = false, mWantOut = false, mWantRest = false;
        Pose mFrom;   // where the loop was when it was asked to settle
    };
}
}
}
