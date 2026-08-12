/*
 *  Genesis — SplashScreen: the application-open animation.
 *
 *  The same launch moment cosmo has, in Genesis's own words: the wordmark rises and scales
 *  in, the tagline fades in behind it, three dots pulse in sequence until there is something
 *  to name, and a 2px accent bar tracks real startup progress along the bottom edge.
 *
 *  Self-drawn and input-transparent — this is chrome for a fixed, short moment, not a screen
 *  you can touch. Every part is an eased Property, so the whole thing collapses under
 *  reducedMotion(). The host shows it in its own small, undecorated window and does its
 *  startup work behind it, so the first thing on screen is never a blank frame.
 */
#pragma once
#include "../Theme.h"
#include "Panel.h"
#include <artboard/artboard.h>
#include <string>

namespace genesis
{
namespace ui
{
    class SplashScreen : public artboard::Segment
    {
    public:
        static constexpr double kWidth = 420.0;
        static constexpr double kHeight = 240.0;
        /** Total intro length; the host defers its startup work until this has played. */
        static constexpr double kIntroMs = 820.0;

        SplashScreen() { inputTransparent = true; width.set(kWidth); height.set(kHeight); }

        void begin(double nowMs);
        bool introDone() const { return mStarted && mNowMs - mT0 >= kIntroMs; }
        /** Real startup progress, 0..1, shown by the bottom bar. */
        void setProgress(double p);
        /** Name what is being loaded. The FIRST call hands the slot over from the pulsing
         *  dots to the status line as a cross-fade; later calls just change the string,
         *  because the text is data, not motion. */
        void setStatus(const std::string &text);
        void beginExit();
        bool isGone() const { return mExiting && mExit.value() <= 0.001; }

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        double mT0 = 0.0, mNowMs = 0.0;
        bool mStarted = false, mExiting = false;
        std::string mStatus;
        artboard::Property mRise{0.0};
        artboard::Property mTag{0.0};
        artboard::Property mDots{0.0};
        artboard::Property mStatusMix{0.0};
        artboard::Property mProgress{0.0};
        artboard::Property mExit{1.0};
    };
}
}
