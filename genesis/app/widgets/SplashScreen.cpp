#include "SplashScreen.h"
#include <cmath>

namespace genesis
{
namespace ui
{
    namespace
    {
        constexpr double kWordPx = 40.0;
        constexpr double kTagPx = 11.5;
        constexpr double kTagTrack = 1.6;
        const char *kTagline = "ANIMATION DESIGNER FOR ARTBOARD";
        constexpr double kDotR = 3.0;
        constexpr double kDotGap = 14.0;
        constexpr double kDotPulseMs = 1400.0;
        constexpr double kDotStaggerMs = 160.0;
    }

    void SplashScreen::begin(double nowMs)
    {
        mStarted = true;
        mT0 = nowMs;
        mNowMs = nowMs;
        // Staggered so the eye reads them in order: mark, then tagline, then activity.
        mRise.animateTo(1.0, 420.0, artboard::Easing::EmphasizedDecel, nowMs);
        mTag.animate(artboard::Tween::range(0.0, 1.0, 320.0).after(180.0)
                         .withEasing(artboard::Easing::EaseOutCubic), nowMs);
        mDots.animate(artboard::Tween::range(0.0, 1.0, 260.0).after(340.0)
                          .withEasing(artboard::Easing::EaseOutCubic), nowMs);
    }

    void SplashScreen::setProgress(double p)
    {
        const double v = p < 0.0 ? 0.0 : (p > 1.0 ? 1.0 : p);
        mProgress.animateTo(v, 260.0, artboard::Easing::EaseOutCubic, mNowMs);
    }

    void SplashScreen::setStatus(const std::string &text)
    {
        const bool first = mStatus.empty();
        mStatus = text;
        if (first && !text.empty())
            mStatusMix.animateTo(1.0, 280.0, artboard::Easing::EaseInOutCubic, mNowMs);
    }

    void SplashScreen::beginExit()
    {
        if (mExiting) return;
        mExiting = true;
        mExit.animateTo(0.0, 240.0, artboard::Easing::EaseOutCubic, mNowMs);
    }

    void SplashScreen::advance(double nowMs)
    {
        mNowMs = nowMs;
        if (!mStarted) begin(nowMs);
        mRise.update(nowMs);
        mTag.update(nowMs);
        mDots.update(nowMs);
        mStatusMix.update(nowMs);
        mProgress.update(nowMs);
        mExit.update(nowMs);
        Segment::advance(nowMs);
    }

    void SplashScreen::onPaint(artboard::IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        const double a = mExit.value();
        if (a <= 0.001) return;

        artboard::drawRoundedRect(t, {0, 0, w, h}, 0.0, artboard::Paint::filled(palette::background()));

        // ── wordmark: rises 8px and scales 0.96 -> 1 as it fades in ──
        const double rise = mRise.value();
        const double sz = kWordPx * (0.96 + 0.04 * rise);
        const double sp = -0.03 * sz;
        // Measure ONCE and reuse for both the centring and the dot's offset: mixing a
        // measured width with an estimated one is what pushes a wordmark off-centre.
        const double stemW = t.measureText("genesis", sz, font::sansSemiBold(), sp);
        const double wordW = stemW + t.measureText(".", sz, font::sansSemiBold(), sp);
        const double wx = (w - wordW) * 0.5;
        const double wy = h * 0.44 + (1.0 - rise) * 8.0;
        artboard::Color fg = palette::foreground();
        fg.a *= rise * a;
        artboard::Color dot = palette::primary();
        dot.a *= rise * a;
        t.setFill(fg);
        t.drawText("genesis", wx, wy, sz, font::sansSemiBold(), sp);
        t.setFill(dot);
        t.drawText(".", wx + stemW, wy, sz, font::sansSemiBold(), sp);

        // ── tagline ──
        const double tagA = mTag.value() * a;
        if (tagA > 0.004)
        {
            const double tw = t.measureText(kTagline, kTagPx, font::sans(), kTagTrack);
            artboard::Color tc = palette::mutedForeground();
            tc.a *= tagA;
            t.setFill(tc);
            t.drawText(kTagline, (w - tw) * 0.5, wy + 22.0, kTagPx, font::sans(), kTagTrack);
        }

        // ── the activity slot: dots until there is something to name, then the status
        //    line, cross-faded so the swap never pops ──
        const double mix = mStatusMix.value();
        const double cx = w * 0.5, cy = h - 48.0;

        const double dotsA = mDots.value() * (1.0 - mix) * a;
        if (dotsA > 0.004)
            for (int i = 0; i < 3; ++i)
            {
                const double phase =
                    std::fmod(mNowMs - mT0 + kDotStaggerMs * i, kDotPulseMs) / kDotPulseMs;
                const double k = 0.5 - 0.5 * std::cos(phase * 6.28318530718);   // 0..1..0
                const double r = kDotR * (1.0 + 0.4 * k);
                const double x = cx + (i - 1) * kDotGap;
                artboard::drawRoundedRect(t, {x - r, cy - r, r * 2.0, r * 2.0}, radius::pill(),
                                          artboard::Paint::filled(
                                              palette::primaryAlpha((0.3 + 0.7 * k) * dotsA)));
            }

        const double statusA = mix * a;
        if (statusA > 0.004 && !mStatus.empty())
        {
            const double sw = t.measureText(mStatus, type::small(), font::sans());
            artboard::Color sc = palette::mutedForeground();
            sc.a *= statusA;
            t.setFill(sc);
            t.drawText(mStatus, (w - sw) * 0.5, cy + 4.0, type::small(), font::sans());
        }

        // ── the progress bar, pinned to the bottom edge ──
        artboard::drawRoundedRect(t, {0, h - 2.0, w, 2.0}, 0.0,
                                  artboard::Paint::filled(palette::whiteAlpha(0.06 * a)));
        artboard::drawRoundedRect(t, {0, h - 2.0, w * mProgress.value(), 2.0}, 0.0,
                                  artboard::Paint::filled(palette::primaryAlpha(a)));
    }
}
}
