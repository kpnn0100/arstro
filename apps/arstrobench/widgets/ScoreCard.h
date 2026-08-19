/*
 *  Arstrobench by arstro — ScoreCard: one workload's result.
 *
 *  It IS a progress readout, so it derives from artboard::ProgressIndicator and inherits
 *  the indeterminate sweep and the spring-smoothed fill from the framework rather than
 *  re-implementing either (R-UI-6). A workload cannot report fractional progress, so the
 *  running state sweeps (indeterminate) and only the finished state is determinate.
 *
 *  Everything visible here animates (R-G-1): the score counts up, the elapsed time counts
 *  with it, the status chip cross-fades its colour AND its caption, and the whole card
 *  fades and rises on entrance.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include <functional>
#include <string>

namespace arstro
{
namespace arstrobench
{
    class ScoreCard : public artboard::ProgressIndicator
    {
    public:
        enum class State { Idle, Running, Done };

        ScoreCard(std::string title, const artboard::Color &accent);

        /** Entrance: fade in and rise into place, delayed by `delayMs` so a row of cards
         *  staggers (R-UI-4). `finalY` is where the card comes to rest. */
        void enter(double finalY, double delayMs, double nowMs);

        void setState(State s, double nowMs);
        State state() const { return mState; }

        /** A result landed: count the score and the time up to their new values (R-UI-5)
         *  and spring the meter to full. */
        void showScore(double score, double seconds, double nowMs);
        /** One line describing the work measured, shown under the meter. */
        void setDetail(std::string detail) { mDetail = std::move(detail); }

        /** The number currently DRAWN (mid-count-up), for the motion assertions. */
        double shownScore() const { return mShownScore.value(); }

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        void fadeChipTo(const std::string &caption, double nowMs);
        /** Swap what the numeral shows through a fade, so the em-dash never pops into a
         *  number in one frame (R-G-1). The count-up starts at the fade trough. */
        void fadeNumberTo(bool hasScore, double nowMs, std::function<void()> atTrough = {});
        artboard::Color statusColor() const;

        std::string mTitle, mDetail, mChip = "Idle", mChipPending;
        artboard::Color mAccent;
        State mState = State::Idle;
        bool mHasScore = false;

        artboard::Property mShownScore{0.0};
        artboard::Property mShownSeconds{0.0};
        artboard::Property mRunAmount{0.0};   ///< 0 -> 1 as the card enters the running state
        artboard::Property mDoneAmount{0.0};  ///< 0 -> 1 as the result lands
        artboard::Property mChipFade{1.0};    ///< status caption cross-fade
        artboard::Property mNumberFade{1.0};  ///< numeral cross-fade (em-dash <-> the count-up)
    };
}
}
