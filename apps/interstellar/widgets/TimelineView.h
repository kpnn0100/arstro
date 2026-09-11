/*
 *  interstellar/widgets — TimelineView: cuts, and only cuts (R-UI-3).
 *
 *  No colour, no curves, and NO automation lanes. Putting a lane under a clip is what makes
 *  people believe the automation belongs to the clip, and R-AUTO-1 says it does not — a shape is
 *  a named object that several parameters can share.
 *
 *  The hardest thing in this widget is the zoom. `pixelsPerSecond` is an ANIMATED property and
 *  every geometric read goes through its LIVE eased value, re-derived every frame: clip rects,
 *  the ruler's tick spacing, the playhead's x, the visibility cull and the snap threshold in
 *  seconds. Deriving once zooms the ruler and leaves the clips behind — that is gotcha 19, and
 *  the defect it comes from was written by someone who had just read the rule.
 */
#pragma once
#include "../Theme.h"
#include "../core/service/AppModel.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar_v1
{
    class TimelineView : public artboard::Segment
    {
    public:
        TimelineView();

        void setModel(const interstellar::AppModel &m);
        void setPlayhead(double t) { mPlayhead = t; }
        void setSelected(const std::string &clipName) { mSelected = clipName; }

        /** The zoom. `set` snaps (the first placement has nowhere to travel from); `zoomTo`
         *  eases, and every read below goes through the LIVE value. */
        void setPixelsPerSecond(double pps) { mPps.set(pps); mPpsTarget = pps; }
        void zoomTo(double pps) { mPpsTarget = pps; }
        double pixelsPerSecond() const { return mPps.value(); }   // LIVE — a test needs this
        double pixelsPerSecondTarget() const { return mPpsTarget; }

        void setScroll(double seconds) { mScrollTarget = std::max(0.0, seconds); }
        double scroll() const { return mScroll.value(); }

        /** Published geometry, so a shot or a test aims at the real rect rather than at where it
         *  thinks the rect is. Empty when the clip is not visible. */
        artboard::Rect clipRect(const std::string &clipName) const;
        double playheadX() const;

        std::function<void(const std::string &, double)> onClipMoved;   // name, snapped `at`
        std::function<void(double)> onScrub;
        std::function<void(const std::string &)> onClipPicked;

        void layout();
        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        /** The time axis begins at the header column, in both directions — so a click in the
         *  ruler lands on the time that is drawn under it. */
        double xForTime(double t) const
        {
            return time::headerWidth() + (t - mScroll.value()) * mPps.value();
        }
        double timeForX(double x) const
        {
            return mScroll.value() + (x - time::headerWidth()) / std::max(1e-6, mPps.value());
        }
        double trackY(int order) const;
        /** The nearest snap point in SECONDS, or `t` when nothing is within the threshold. The
         *  threshold is in pixels and converted through the LIVE zoom, so snapping feels the
         *  same at every zoom level. */
        double snap(double t) const;

        const interstellar::AppModel *mModel = nullptr;
        std::vector<double> mSnapPoints;
        std::string mSelected, mDragging;
        double mPlayhead = 0;
        double mDragGrab = 0;      // clip start MINUS pointer time: a drag must never teleport
        bool mScrubbing = false;
        artboard::Property mPps{60.0};
        double mPpsTarget = 60.0;
        artboard::Property mScroll{0.0};
        double mScrollTarget = 0.0;
        double mLastMs = 0;
    };
}
}
