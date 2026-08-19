/*
 *  cosmo_v2 by arstro — SplashScreen: the application-open animation (R-SPLASH).
 *
 *  Ported from the reference design's boot `LoadingScreen`
 *  (ref/cosmo/File Reader Design(1).zip -> src/app/App.tsx), scaled from a
 *  fullscreen page to the SMALL, undecorated window cosmo actually opens with:
 *  the `cosmo.` wordmark rises + scales in, the tagline fades in behind it,
 *  three dots pulse in sequence, and a 2px accent progress bar is pinned to the
 *  bottom edge of the window.
 *
 *  Self-drawn (no child Segments) and non-interactive — it is chrome for a
 *  fixed, short moment, not a screen you can touch. Every part is an eased
 *  AnimatedProperty and the whole thing collapses under reducedMotion().
 *
 *  The host renders this into its own borderless GTK window through the same
 *  CairoTarget as the editor, so the wordmark is drawn by exactly the same code
 *  path (and the same R-G-2a letter-spacing) as everywhere else in the app.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include "../UiInspectable.h"
#include <string>

namespace arstro
{
namespace cosmo_v2
{
    class SplashScreen : public artboard::Segment, public UiInspectable
    {
    public:
        // The splash window's size. Small and fixed: this is a launch moment, not a
        // screen that reflows (R-SPLASH-1).
        static constexpr double kWidth = 420.0;
        static constexpr double kHeight = 260.0;
        /** Total intro length; the host defers its startup work until this has played. */
        static constexpr double kIntroMs = 900.0;

        SplashScreen() { inputTransparent = true; }   // R-SPLASH-4: takes no input

        /** Start the intro at `nowMs`. */
        void begin(double nowMs);
        /** True once the intro has finished playing — the host starts loading here. */
        bool introDone() const { return mNowMs - mT0 >= kIntroMs; }
        /** Real startup progress, 0..1, shown by the bar. `done`/`total` are optional and add
         *  the count beside it — R-LOADUX-3's "n of N" convention, because a fraction with no
         *  denominator tells a photographer how far but never how much. */
        void setProgress(double p, int done = -1, int total = -1);
        /** R-SPLASH-2a: name what is being loaded right now. The FIRST call hands the
         *  slot over from the pulsing dots to a spinner + this text (a cross-fade, so
         *  nothing jumps); later calls just change the string, because the text is data,
         *  not motion — cross-fading each swap would flicker at the rate items land. */
        void setStatus(const std::string &text);
        /** Ask to fade out; `isGone()` turns true when the host may destroy us. The fade does
         *  not necessarily start here: if the bar has not yet caught up to 100%, it starts
         *  once it has (see mExitRequested). Measured, not guessed — `ui dump --root splash`
         *  sampled at 80 ms showed the bar peaking at 23% opacity for one 80 ms window, all
         *  of it inside the exit fade, because a cover now decodes in ~7 ms (R-SPLASH-5) and
         *  the work was over before the 260 ms fade-in had gone anywhere. A progress bar
         *  nobody can see is not a progress bar. */
        void beginExit();
        bool isGone() const { return mExiting && mExit.value() <= 0.001; }

        void advance(double nowMs) override;

        /** P0.6 — the splash draws itself, so a tree dump would otherwise report a 420x260
         *  rectangle and nothing about the bar inside it. This is how "is the progress bar
         *  showing, and how full" is answered over the socket instead of by squinting at a
         *  screenshot of a 4 px stripe (`ui dump --root splash`). */
        std::string uiDetail() const override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        double mT0 = 0.0, mNowMs = 0.0;
        bool mStarted = false, mExiting = false;
        bool mExitRequested = false;   // asked to go; waiting for the bar to read 100%
        artboard::AnimatedProperty mRise{0.0};      // wordmark: opacity + rise + scale
        artboard::AnimatedProperty mTag{0.0};       // tagline, staggered behind it
        artboard::AnimatedProperty mDots{0.0};      // the pulsing dot row (pre-status)
        artboard::AnimatedProperty mStatusMix{0.0}; // 0 = dots hold the slot, 1 = status line
        std::string mStatus;
        artboard::AnimatedProperty mProgress{0.0};  // eased real progress
        artboard::AnimatedProperty mBarFade{0.0};   // the bar's track, faded in with the intro
        int mDone = -1, mTotal = -1;
        artboard::AnimatedProperty mExit{1.0};      // whole-splash fade-out
    };
}
}
