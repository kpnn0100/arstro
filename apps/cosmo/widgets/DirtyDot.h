/*
 *  cosmo_v2 by arstro — DirtyDot: the unsaved dot (R-FAMILY-5, the family standard R-FAMILY-0c).
 *
 *  Unsaved work is said the same way in every app: a small dot AFTER the project name, in the
 *  FOREGROUND colour — not the accent (a selection) and not red (an error): it is a fact about the
 *  document, not a state of the UI. Radius 3. It fades in and out (200 ms), never pops.
 *
 *  A value object, not a Segment: the dot follows a name whose width is only known in the paint,
 *  so the owner draws it at a point it has just measured. Set the target whenever the model says
 *  so (a setter has no clock), advance it once a frame, draw it. The first advance PLACES it —
 *  there is nowhere to travel from — and every later change eases.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include "../Theme.h"

namespace arstro
{
namespace cosmo_v2
{
    class DirtyDot
    {
    public:
        static constexpr double kRadius = 3.0;
        static constexpr double kGap = 8.0;   // from the end of the name to the dot's CENTRE

        /** Whether there is unsaved work. Starts no tween: the next `advance` does. */
        void set(bool dirty) { mWanted = dirty; }
        /** Snap, for a state that is not a change (a project just opened). */
        void setImmediate(bool dirty)
        {
            mWanted = mApplied = dirty;
            mAmount.set(dirty ? 1.0 : 0.0);
            mPlaced = true;
        }
        bool target() const { return mWanted; }

        void advance(double nowMs)
        {
            if (!mPlaced) { setImmediate(mWanted); }
            else if (mWanted != mApplied)
            {
                mAmount.animateTo(mWanted ? 1.0 : 0.0, motion::kCrossFadeMs, artboard::Easing::EaseOutCubic, nowMs);
                mApplied = mWanted;
            }
            mAmount.update(nowMs);
        }

        /** LIVE: 0 saved … 1 unsaved — the dot's opacity. */
        double amount() const { return mAmount.value(); }
        bool isAnimating() const { return mAmount.isAnimating() || (mPlaced && mWanted != mApplied); }

        /** Draw it centred on `centre` (typically name end + kGap, the row's centre line). `alpha`
         *  is the owner's own fade. Draws nothing when there is nothing to see. */
        void draw(artboard::IRenderTarget &t, const artboard::Point &centre, double alpha = 1.0) const
        {
            const double a = mAmount.value() * alpha;
            if (a <= 0.001) return;
            artboard::Color c = palette::foreground();
            c.a *= a;
            artboard::drawCircle(t, centre.x, centre.y, kRadius, artboard::Paint::filled(c));
        }

    private:
        artboard::AnimatedProperty mAmount{0.0};
        bool mWanted = false, mApplied = false, mPlaced = false;
    };
}
}
