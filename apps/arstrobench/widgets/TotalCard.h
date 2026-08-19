/*
 *  Arstrobench by arstro — TotalCard: the headline ARSTROBENCH SCORE (R-SCORE-5).
 *
 *  The unweighted sum of the two workload scores. Labelled with its composition so the
 *  number is never a black box, and counted up rather than assigned (R-G-1 / R-UI-5).
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"

namespace arstro
{
namespace arstrobench
{
    class TotalCard : public artboard::Segment
    {
    public:
        TotalCard();

        /** Entrance: fade in and rise into place after `delayMs` (R-UI-4). */
        void enter(double finalY, double delayMs, double nowMs);
        /** Count up to the new total; `has` false draws an em-dash instead of a zero. */
        void showScore(double total, bool has, double nowMs);
        double shownScore() const { return mShown.value(); }

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        bool mHas = false;
        artboard::Property mShown{0.0};
        artboard::Property mInk{0.0};        ///< 0 = muted (no result yet), 1 = accent
        artboard::Property mNumberFade{1.0}; ///< numeral cross-fade (em-dash <-> the count-up)
    };
}
}
