#include "ScoreCard.h"
#include "../Theme.h"
#include <algorithm>
#include <cstdio>

namespace arstro
{
namespace arstrobench
{
    using namespace artboard;

    namespace
    {
        // ── card geometry (all in local space) ──
        constexpr double kPad = 20.0;
        constexpr double kTitleBaseline = 30.0;
        constexpr double kScoreBaseline = 108.0;
        constexpr double kScoreSize = 46.0;
        constexpr double kTimeBaseline = 132.0;
        constexpr double kMeterY = 148.0;
        constexpr double kMeterH = 4.0;
        constexpr double kDetailBaseline = 182.0;
        constexpr double kChipTop = 16.0;
        constexpr double kChipH = 18.0;
        constexpr double kChipPadX = 9.0;
        constexpr double kChipTextSize = 9.0;

        constexpr double kCountUpMs = 700.0;      // R-UI-5
        constexpr double kStateFadeMs = motion::kDurationMedium1;
        constexpr double kChipOutMs = 90.0;
        constexpr double kChipInMs = motion::kDurationShort3;
        constexpr double kEnterRise = 14.0;       // px the card travels on entrance
        constexpr double kEnterMs = motion::kDurationMedium4;

        std::string formatScore(double score, bool has)
        {
            if (!has) return "\xE2\x80\x94";  // em-dash: absence, not a zero score (R-UI-3)
            char buf[32];
            std::snprintf(buf, sizeof(buf), score >= 100.0 ? "%.0f" : "%.2f", score);
            return buf;
        }

        std::string formatSeconds(double seconds, bool has)
        {
            if (!has) return "not measured yet";
            char buf[48];
            if (seconds >= 1.0) std::snprintf(buf, sizeof(buf), "%.3f s per pass", seconds);
            else std::snprintf(buf, sizeof(buf), "%.2f ms per pass", seconds * 1000.0);
            return buf;
        }
    }

    ScoreCard::ScoreCard(std::string title, const Color &accent)
        : mTitle(std::move(title)), mAccent(accent)
    {
        opacity.set(0.0);  // hidden until enter() brings it in -- an empty state, not a pop-in
    }

    void ScoreCard::enter(double finalY, double delayMs, double nowMs)
    {
        y.set(finalY + kEnterRise);
        y.animate(Tween::range(finalY + kEnterRise, finalY, kEnterMs)
                      .withEasing(Easing::EmphasizedDecel).after(delayMs), nowMs);
        opacity.animate(Tween::range(0.0, 1.0, kEnterMs).withEasing(Easing::EaseOutCubic).after(delayMs), nowMs);
    }

    void ScoreCard::fadeChipTo(const std::string &caption, double nowMs)
    {
        if (caption == mChip || caption == mChipPending) return;
        mChipPending = caption;
        mChipFade.animate(Tween::range(mChipFade.value(), 0.0, kChipOutMs).withEasing(Easing::EaseInQuad),
                          nowMs, [this, nowMs] {
                              mChip = mChipPending;
                              mChipPending.clear();
                              mChipFade.animate(Tween::range(0.0, 1.0, kChipInMs).withEasing(Easing::EaseOutCubic),
                                                nowMs + kChipOutMs);
                          });
    }

    void ScoreCard::fadeNumberTo(bool hasScore, double nowMs, std::function<void()> atTrough)
    {
        if (hasScore == mHasScore)  // nothing to swap; just make sure the numeral is visible
        {
            if (atTrough) atTrough();
            mNumberFade.animate(Tween::range(mNumberFade.value(), 1.0, kChipInMs)
                                    .withEasing(Easing::EaseOutCubic), nowMs);
            return;
        }
        mNumberFade.animate(Tween::range(mNumberFade.value(), 0.0, kChipOutMs).withEasing(Easing::EaseInQuad),
                            nowMs, [this, hasScore, nowMs, atTrough] {
                                mHasScore = hasScore;
                                if (atTrough) atTrough();
                                mNumberFade.animate(Tween::range(0.0, 1.0, kChipInMs)
                                                        .withEasing(Easing::EaseOutCubic),
                                                    nowMs + kChipOutMs);
                            });
    }

    void ScoreCard::setState(State s, double nowMs)
    {
        if (s == mState) return;
        mState = s;

        // The two state amounts cross-fade the chip colour and the score's ink; nothing
        // switches colour in one frame (R-UI-8).
        const double run = (s == State::Running || s == State::Done) ? 1.0 : 0.0;
        const double done = (s == State::Done) ? 1.0 : 0.0;
        mRunAmount.animate(Tween::range(mRunAmount.value(), run, kStateFadeMs).withEasing(Easing::EaseOutCubic), nowMs);
        mDoneAmount.animate(Tween::range(mDoneAmount.value(), done, kStateFadeMs).withEasing(Easing::EaseOutCubic), nowMs);

        switch (s)
        {
        case State::Idle:
            fadeChipTo("Idle", nowMs);
            setIndeterminate(false);
            setValue(0.0);
            fadeNumberTo(false, nowMs);
            mShownScore.animate(Tween::range(mShownScore.value(), 0.0, kStateFadeMs), nowMs);
            mShownSeconds.animate(Tween::range(mShownSeconds.value(), 0.0, kStateFadeMs), nowMs);
            break;
        case State::Running:
            fadeChipTo("Running", nowMs);
            fadeNumberTo(false, nowMs);
            setValue(0.0);
            setIndeterminate(true);  // the workload cannot report a fraction (R-UI-6)
            break;
        case State::Done:
            fadeChipTo("Done", nowMs);
            setIndeterminate(false);
            setValue(1.0);           // springs to full via displayValue()
            break;
        }
    }

    void ScoreCard::showScore(double score, double seconds, double nowMs)
    {
        setState(State::Done, nowMs);
        // The count-up begins where the numeral becomes visible, so the number is never
        // seen sitting at zero before it starts moving.
        fadeNumberTo(true, nowMs, [this, score, seconds, nowMs] {
            mShownScore.animate(Tween::range(0.0, score, kCountUpMs).withEasing(Easing::EaseOutCubic), nowMs);
            mShownSeconds.animate(Tween::range(0.0, seconds, kCountUpMs).withEasing(Easing::EaseOutCubic), nowMs);
        });
    }

    void ScoreCard::advance(double nowMs)
    {
        ProgressIndicator::advance(nowMs);
        mShownScore.update(nowMs);
        mShownSeconds.update(nowMs);
        mRunAmount.update(nowMs);
        mDoneAmount.update(nowMs);
        mChipFade.update(nowMs);
        mNumberFade.update(nowMs);
    }

    Color ScoreCard::statusColor() const
    {
        // muted -> accent (running) -> success (done), both legs eased by their own amount.
        const Color running = lerpColor(palette::mutedForeground(), mAccent, mRunAmount.value());
        return lerpColor(running, palette::success(), mDoneAmount.value());
    }

    void ScoreCard::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();

        // ── card surface: cosmo's card colour + hairline border, cosmo's control radius ──
        drawRoundedRect(t, Rect{0, 0, w, h}, radius::control(),
                        Paint::filledStroked(palette::card(), palette::border(), 1.0));

        // ── title ──
        t.setFill(palette::mutedForeground());
        t.drawText(mTitle, kPad, kTitleBaseline, 10.0, font::sansMedium(), 0.8);

        // ── status chip (top-right) ──
        const Color status = statusColor();
        const double chipTextW = t.measureText(mChip, kChipTextSize, font::sansMedium(), 0.5);
        const double chipW = chipTextW + kChipPadX * 2.0;
        const double chipX = w - kPad - chipW;
        Color chipBg = status; chipBg.a = 0.14;
        drawRoundedRect(t, Rect{chipX, kChipTop, chipW, kChipH}, radius::pill(), Paint::filled(chipBg));
        Color chipInk = status; chipInk.a = mChipFade.value();
        t.setFill(chipInk);
        t.drawText(mChip, chipX + kChipPadX, kChipTop + kChipH * 0.5 + kChipTextSize * 0.36,
                   kChipTextSize, font::sansMedium(), 0.5);

        // ── the score: mono numerals, accent-tinted once a result has landed ──
        Color scoreInk = lerpColor(palette::mutedForeground(), palette::foreground(),
                                   mDoneAmount.value());
        scoreInk.a *= mNumberFade.value();
        t.setFill(scoreInk);
        t.drawText(formatScore(mShownScore.value(), mHasScore), kPad, kScoreBaseline,
                   kScoreSize, font::monoMedium(), -1.0);

        // "score" caption, snapped to the right of the numeral so the two never overlap.
        const double numW = t.measureText(formatScore(mShownScore.value(), mHasScore), kScoreSize,
                                          font::monoMedium(), -1.0);
        t.setFill(palette::mutedForeground());
        t.drawText("score", kPad + numW + 10.0, kScoreBaseline, 10.0, font::sans());

        // ── measured time: a primary readout, so it takes the primary ink rather than
        // the muted label grey (the score and the time are the two things being reported).
        Color timeInk = lerpColor(palette::mutedForeground(), palette::foreground(), mDoneAmount.value());
        timeInk.a *= mNumberFade.value();
        t.setFill(timeInk);
        t.drawText(formatSeconds(mShownSeconds.value(), mHasScore), kPad, kTimeBaseline,
                   10.0, font::mono());

        // ── meter: sweeps while running, springs to full when the result lands ──
        const double meterW = w - kPad * 2.0;
        drawRoundedRect(t, Rect{kPad, kMeterY, meterW, kMeterH}, radius::pill(),
                        Paint::filled(palette::secondary()));
        if (indeterminate())
        {
            // A short bar sweeping the track: the honest picture of "working, fraction
            // unknown". phase() is the framework's free-running sweep.
            const double barW = meterW * 0.28;
            const double travel = meterW + barW;
            const double x = kPad - barW + phase() * travel;
            const double x0 = std::max(kPad, x), x1 = std::min(kPad + meterW, x + barW);
            if (x1 > x0)
                drawRoundedRect(t, Rect{x0, kMeterY, x1 - x0, kMeterH}, radius::pill(),
                                Paint::filled(mAccent));
        }
        else if (displayValue() > 0.0)
        {
            drawRoundedRect(t, Rect{kPad, kMeterY, meterW * displayValue(), kMeterH},
                            radius::pill(), Paint::filled(mAccent));
        }

        // ── what was measured ──
        t.setFill(palette::mutedForeground());
        t.drawText(mDetail, kPad, kDetailBaseline, 10.0, font::mono());
    }
}
}
