/*
 *  interstellar_v1 — ChecksPanel: the Deliver tab's left column — what would stop a delivery.
 *
 *  Read straight off the model, sentence by sentence, nothing inferred that the model does not
 *  say: a version with DANGLING deltas (R-VER-4 — "rebase to reconcile"), an OFFLINE source
 *  (R-RACK-7) and how many clips lean on it, a source still decoding, a render that failed and
 *  why; and, as information rather than warning, which versions are pinned or frozen — the state
 *  people forget they are in. Each line carries a severity glyph (destructive / muted / success);
 *  with nothing wrong it says so in one sentence rather than showing an empty column.
 *
 *  A real lint report is the service's (`lint` in §8); the model has no field for it yet, so this
 *  column is limited to facts the model already carries — a contract request in NOTES.md.
 *  Scrolls; lines travel on insert/remove (AnimatedRows).
 */
#pragma once
#include "../Theme.h"
#include "../AppHooks.h"
#include "AnimatedRows.h"
#include "EasedScroll.h"
#include <string>

namespace arstro
{
namespace interstellar_v1
{
    class ChecksPanel : public artboard::Segment
    {
    public:
        static constexpr double kRowH = 39.0;      // u(12): a sentence and its detail
        static constexpr double kHeaderH = 29.25;

        struct Check
        {
            int severity = 0;    // 0 info, 1 caution, 2 problem, 3 all clear
            std::string text, detail;
        };

        ChecksPanel();
        void bind(const interstellar::AppModel &m);
        int problemCount() const { return mProblems; }
        const EasedScroll &scroll() const { return mScroll; }
        int count() const { return (int)mChecks.size(); }

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        std::vector<Check> mChecks;
        AnimatedRows<Check> mRows;
        EasedScroll mScroll;
        int mProblems = 0;
    };
}
}
