#include "TotalCard.h"
#include "../Theme.h"
#include <cstdio>

namespace arstro
{
namespace arstrobench
{
    using namespace artboard;

    namespace
    {
        constexpr double kPad = 24.0;
        constexpr double kTitleBaseline = 36.0;
        constexpr double kSubBaseline = 60.0;
        constexpr double kScoreSize = 40.0;
        constexpr double kScoreBaseline = 58.0;
        constexpr double kCountUpMs = 700.0;
        constexpr double kEnterRise = 14.0;
        constexpr double kEnterMs = motion::kDurationMedium4;
        constexpr double kFadeOutMs = 90.0;
        constexpr double kFadeInMs = motion::kDurationShort3;

        std::string formatTotal(double v, bool has)
        {
            if (!has) return "\xE2\x80\x94";  // em-dash until a run has finished
            char buf[32];
            std::snprintf(buf, sizeof(buf), v >= 100.0 ? "%.0f" : "%.2f", v);
            return buf;
        }
    }

    TotalCard::TotalCard() { opacity.set(0.0); }

    void TotalCard::enter(double finalY, double delayMs, double nowMs)
    {
        y.set(finalY + kEnterRise);
        y.animate(Tween::range(finalY + kEnterRise, finalY, kEnterMs)
                      .withEasing(Easing::EmphasizedDecel).after(delayMs), nowMs);
        opacity.animate(Tween::range(0.0, 1.0, kEnterMs).withEasing(Easing::EaseOutCubic).after(delayMs), nowMs);
    }

    void TotalCard::showScore(double total, bool has, double nowMs)
    {
        mInk.animate(Tween::range(mInk.value(), has ? 1.0 : 0.0, motion::kDurationMedium1)
                         .withEasing(Easing::EaseOutCubic), nowMs);
        // Swap em-dash <-> numeral at the trough of a fade, never in a single frame (R-G-1);
        // the count-up starts there too, so the total is never seen resting at zero.
        const auto atTrough = [this, total, has, nowMs] {
            mHas = has;
            mShown.animate(Tween::range(has ? 0.0 : mShown.value(), total, kCountUpMs)
                               .withEasing(Easing::EaseOutCubic), nowMs);
        };
        if (has == mHas) { atTrough(); return; }
        mNumberFade.animate(Tween::range(mNumberFade.value(), 0.0, kFadeOutMs).withEasing(Easing::EaseInQuad),
                            nowMs, [this, atTrough, nowMs] {
                                atTrough();
                                mNumberFade.animate(Tween::range(0.0, 1.0, kFadeInMs)
                                                        .withEasing(Easing::EaseOutCubic),
                                                    nowMs + kFadeOutMs);
                            });
    }

    void TotalCard::advance(double nowMs)
    {
        Segment::advance(nowMs);
        mShown.update(nowMs);
        mInk.update(nowMs);
        mNumberFade.update(nowMs);
    }

    void TotalCard::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, radius::control(),
                        Paint::filledStroked(palette::card(), palette::border(), 1.0));

        t.setFill(palette::foreground());
        t.drawText("ARSTROBENCH SCORE", kPad, kTitleBaseline, 11.0, font::sansSemiBold(), 0.8);
        t.setFill(palette::mutedForeground());
        t.drawText("image score + signal score", kPad, kSubBaseline, 10.0, font::sans());

        // Right-aligned so the numeral grows leftwards and can never collide with the
        // label column on its left (layout snaps, it does not stack).
        const std::string text = formatTotal(mShown.value(), mHas);
        const double tw = t.measureText(text, kScoreSize, font::monoMedium(), -1.0);
        Color ink = lerpColor(palette::mutedForeground(), palette::primary(), mInk.value());
        ink.a *= mNumberFade.value();
        t.setFill(ink);
        t.drawText(text, w - kPad - tw, kScoreBaseline, kScoreSize, font::monoMedium(), -1.0);
    }
}
}
