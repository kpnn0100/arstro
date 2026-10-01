/*
 *  interstellar_v1 — RenderQueue: the Deliver tab's deck — every render, each NAMING its timeline.
 *
 *  R-RENDER-1: "the timeline being rendered is named explicitly in the queue row, never implied
 *  by the open tab". So the first thing on each row is the version's name, in the medium weight,
 *  then the format as a mono chip and the output path (mono, ellipsized against the room left).
 *  The right of the row is the job's state: queued (muted), running (an eased progress bar, the
 *  percentage and a frame RATE — ui-brief §4 "per-job progress with a rate"), done (success),
 *  failed (destructive, with the reason).
 *
 *  The bar follows the model's done/total as a value catching up to a source it does not control
 *  (220 ms) — five uneven jumps from a service must not read as five stalls — and it never draws
 *  a pill shorter than its own height (gotcha 12). Rows travel on insert/remove; the list scrolls.
 *  Empty: "nothing queued".
 */
#pragma once
#include "../Theme.h"
#include "../AppHooks.h"
#include "AnimatedRows.h"
#include "EasedScroll.h"
#include <map>
#include <string>

namespace arstro
{
namespace interstellar_v1
{
    class RenderQueue : public artboard::Segment
    {
    public:
        static constexpr double kRowH = 32.5;
        static constexpr double kHeaderH = 29.25;

        RenderQueue();
        /** `nowMs` timestamps progress, so a frame rate can be derived from successive models. */
        void bind(const interstellar::AppModel &m, double nowMs);

        double shownFraction(const std::string &jobId) const;
        double rate(const std::string &jobId) const;
        /** The live eased amount of a job's state k (0 queued, 1 running, 2 done, 3 failed). */
        double stateAmount(const std::string &jobId, int k) const;
        const EasedScroll &scroll() const { return mScroll; }
        int count() const { return (int)mJobs.size(); }
        artboard::Rect rowRect(int i) const;

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        struct Progress
        {
            artboard::AnimatedProperty frac{0.0};
            double target = 0.0, last = -1.0;
            int lastDone = -1;
            double lastMs = 0.0, rate = 0.0;
            /** queued / running / done / failed, each an eased 0..1: running → done cross-fades the
             *  glyph, the bar's colour and the status line instead of swapping them in one frame. */
            artboard::AnimatedProperty state[4];
            int want = 0, applied = -1;
        };
        static int stateIndex(const std::string &s);
        std::vector<interstellar::RenderJobModel> mJobs;
        AnimatedRows<interstellar::RenderJobModel> mRows;
        std::map<std::string, Progress> mProgress;
        EasedScroll mScroll;
    };
}
}
