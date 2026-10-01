/*
 *  interstellar_v1 — FadePage: one tab's content, shown and hidden by an opacity FADE (R1, R-UI-3).
 *
 *  The three Edit tabs (Grade · Cut · Deliver) are three of these stacked in one place; a tab
 *  switch cross-fades them over `motion::kCrossFadeMs`. Show/hide is opacity, never a `visible`
 *  flip (design rule §1) — and nothing is rebuilt or reloaded, because the page and every widget
 *  in it exist for the life of the app.
 *
 *  Why a class rather than a bare Segment: the OUTGOING page is still drawn for 200 ms while it
 *  fades, and a page drawn at opacity 0.4 is still hittable by Segment's rules (only <= 1e-3 stops
 *  hit-testing). A click during the fade would then land on a control the user is leaving. So
 *  hit-testing follows the INTENT (`shown()`), not the animated opacity: the page you asked for
 *  takes input from the first frame, the one you left takes none.
 *
 *  A setter has no clock: `setShown` records intent and `advance` starts the tween.
 */
#pragma once
#include "../Theme.h"

namespace arstro
{
namespace interstellar_v1
{
    class FadePage : public artboard::Segment
    {
    public:
        FadePage() { clipToBounds = true; }

        void setShown(bool on) { mWanted = on; }
        /** First placement only — there is nowhere to fade from. */
        void setShownImmediate(bool on) { mWanted = mApplied = on; opacity.set(on ? 1.0 : 0.0); }
        bool shown() const { return mWanted; }
        /** The LIVE eased opacity — what a test reads to tell a fade from a snap. */
        double fadeValue() const { return opacity.value(); }

        void advance(double nowMs) override
        {
            if (mWanted != mApplied)
            {
                opacity.animateTo(mWanted ? 1.0 : 0.0, motion::kCrossFadeMs, artboard::Easing::EaseOutCubic, nowMs);
                mApplied = mWanted;
            }
            Segment::advance(nowMs);
        }

        bool hitTest(const artboard::Point &p) const override { return mWanted && Segment::hitTest(p); }

    protected:
        /** A page is a container: it is not itself a target, so a click between its columns falls
         *  to nobody rather than being "consumed" by the page. */
        bool hitTestSelf(const artboard::Point &) const override { return false; }

    private:
        bool mWanted = false, mApplied = false;
    };
}
}
