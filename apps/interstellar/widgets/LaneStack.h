/*
 *  interstellar/widgets — LaneStack: the mixer's automation lanes (R-UI-4).
 *
 *  One row per automated ADDRESS; each link drawn as a clip in time. This is where the user's
 *  DAW analogy lives, and three details are what make the shared-shape model discoverable
 *  rather than surprising:
 *
 *  * **a shape used by more than one link says so** (`ac_push · 2 links`), and selecting one
 *    highlights its siblings. Without that, editing a shape and watching a second parameter move
 *    feels like a bug.
 *  * **it shares the timeline's time axis by DERIVING from the same zoom and scroll**, never by
 *    keeping its own copy. Two copies of one fact drift, and the symptom is a lane three pixels
 *    out of step with the cut above it (gotcha 15).
 *  * **the boundary discontinuity is drawn** — a step marker where a link's first value differs
 *    from the static value with no fade. A lint finding nobody can see is one that does not work.
 *
 *  It clips AND scrolls, clamped both ends, with ONE viewport rectangle shared by the measure,
 *  the row placement, the visibility test and the paint (R6 — a clip with no scroll silently
 *  deletes content).
 */
#pragma once
#include "../Theme.h"
#include "../core/Project.h"
#include "../core/service/AppModel.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar_v1
{
    class LaneStack : public artboard::Segment
    {
    public:
        LaneStack();

        struct LaneRow
        {
            std::string address;
            std::string shapeName;
            int shapeUsers = 1;          // how many links share this shape
            double at = 0, dur = 0;      // the link's placement, in timeline seconds
            double from = 0, to = 1;
            bool stepsOnEntry = false;   // the lint finding, drawn
            std::vector<std::pair<double, double>> curve;   // sampled (localT 0..1, value 0..1)
        };

        void setLanes(std::vector<LaneRow> lanes);
        /** Derived from the timeline, never owned: one zoom, one scroll, one time axis. */
        void setTimeAxis(double pixelsPerSecond, double scrollSeconds)
        {
            mPps = pixelsPerSecond;
            mScroll = scrollSeconds;
        }
        void setPlayhead(double t) { mPlayhead = t; }
        void setSelectedAddress(const std::string &a) { mSelected = a; }

        /** Expanding a lane EASES its height (34 -> 96); it does not swap between two heights. */
        void toggleExpanded(const std::string &address);
        double rowHeight(const std::string &address) const;
        artboard::Rect linkRect(const std::string &address) const;
        /** The live eased expansion of a row, 0..1 — exposed so a test can tell a tween from a
         *  snap (arstro.design.rule §1). */
        double expansion(const std::string &address) const;

        void scrollBy(double dy);
        double scrollOffset() const { return mVScroll.value(); }
        double contentHeight() const;

        std::function<void(const std::string &)> onLanePicked;

        void layout();
        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        /** ONE viewport rect, shared by the measure, the placement, the cull and the paint. Two
         *  copies drift, and the symptom is a last row unreachable at any offset. */
        artboard::Rect viewport() const { return artboard::Rect{0.0, 0.0, width.value(), height.value()}; }
        double yForRow(size_t i) const;

        std::vector<LaneRow> mLanes;
        std::vector<artboard::Property> mExpand;   // one per lane, eased
        std::vector<double> mExpandTarget;
        std::string mSelected;
        double mPps = 60.0, mScroll = 0.0, mPlayhead = 0.0;
        artboard::Property mVScroll{0.0};
        double mVScrollTarget = 0.0;
        double mLastMs = 0;
    };
}
}
