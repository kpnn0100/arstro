#include "SplashScreen.h"
#include "../Theme.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace
    {
        constexpr double kWordPx = 40.0;      // the reference's 60px, scaled to the small window
        constexpr double kTagPx = 9.0;
        constexpr double kTagTrack = 2.1;     // the reference's 0.24em, in px at kTagPx
        constexpr double kBarH = 2.0;
        constexpr double kDotR = 2.0;
        constexpr double kDotGap = 12.0;
        constexpr double kDotPulseMs = 1400.0;
        constexpr double kDotStaggerMs = 180.0;
        // The status line that takes the dots' slot once there is something to name.
        constexpr double kStatusPx = 10.0;
        // The status line never spans the whole window: a splash reads as calm only if
        // the line stays visually centred rather than running to both edges.
        constexpr double kStatusMaxFrac = 0.72;
        constexpr double kSpinR = 5.5;
        constexpr double kSpinGap = 8.0;
        constexpr double kSpinMs = 900.0;   // one turn — the same rate as the rack's cells
        const char *kTagline = "PROFESSIONAL PHOTO EDITOR";
        const char *kVersion = "v1.0.0";

        double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
    }

    void SplashScreen::begin(double nowMs)
    {
        mT0 = nowMs;
        mNowMs = nowMs;
        mStarted = true;
        mExiting = false;
        mExit.set(1.0);
        // Staggered exactly like the reference: the wordmark leads, the tagline follows,
        // the dots come in last (R-SPLASH-2). AnimatedProperty collapses each of these
        // under reducedMotion(), so the splash still reads, just without the motion.
        mRise.set(0.0); mRise.animateTo(1.0, 700.0, Easing::EaseOutCubic, nowMs);
        mTag.set(0.0);  mTag.animateTo(1.0, 700.0, Easing::EaseOutCubic, nowMs + 300.0);
        mDots.set(0.0); mDots.animateTo(1.0, 500.0, Easing::EaseOutCubic, nowMs + 600.0);
    }

    void SplashScreen::setProgress(double p)
    {
        mProgress.animateTo(clamp01(p), 220.0, Easing::EaseOutCubic, mNowMs);
    }

    void SplashScreen::setStatus(const std::string &text)
    {
        const bool first = mStatus.empty();
        mStatus = text;
        // The dots and the status line share one slot and one affordance: the first real
        // item swaps them over, and after that only the string changes (R-SPLASH-2a).
        if (first && !text.empty()) mStatusMix.animateTo(1.0, 280.0, Easing::EaseInOutCubic, mNowMs);
    }

    void SplashScreen::beginExit()
    {
        if (mExiting) return;
        mExiting = true;
        mExit.animateTo(0.0, 260.0, Easing::EaseOutCubic, mNowMs);
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

    void SplashScreen::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        const double a = mExit.value();
        if (a <= 0.001) return;

        drawRoundedRect(t, Rect{0, 0, w, h}, 0.0, Paint::filled(palette::background()));

        // ── wordmark: rises 8px and scales 0.96 -> 1 as it fades in ──
        const double rise = mRise.value();
        const double sz = kWordPx * (0.96 + 0.04 * rise);
        const double sp = -0.03 * sz;   // R-G-2a: the one wordmark spacing formula
        // Measure ONCE and reuse for both the centring and the dot's offset: mixing a
        // measured width with an estimated one is what pushed the wordmark off-centre.
        const double stemW = t.measureText("cosmo", sz, font::sansSemiBold(), sp);
        const double wordW = stemW + t.measureText(".", sz, font::sansSemiBold(), sp);
        const double wx = (w - wordW) * 0.5;
        const double wy = h * 0.42 + (1.0 - rise) * 8.0;
        Color fg = palette::foreground(); fg.a *= rise * a;
        Color dot = palette::primary(); dot.a *= rise * a;
        t.setFill(fg);
        t.drawText("cosmo", wx, wy, sz, font::sansSemiBold(), sp);
        t.setFill(dot);
        t.drawText(".", wx + stemW, wy, sz, font::sansSemiBold(), sp);

        // ── tagline ──
        const double tagA = mTag.value() * a;
        if (tagA > 0.004)
        {
            // measureText is the read side of drawText and accounts for the tracking, so
            // the tagline centres exactly rather than by an estimate (R5).
            const double tw = t.measureText(kTagline, kTagPx, font::sans(), kTagTrack);
            Color tc = palette::mutedForeground(); tc.a *= tagA;
            t.setFill(tc);
            t.drawText(kTagline, (w - tw) * 0.5, wy + 22.0, kTagPx, font::sans(), kTagTrack);
        }

        // ── the activity slot: dots until there is something to name, then the
        //    spinner + status line, cross-faded (R-SPLASH-2a) ──
        const double mix = mStatusMix.value();
        const double cx = w * 0.5, cy = h - 46.0;

        const double dotsA = mDots.value() * (1.0 - mix) * a;
        if (dotsA > 0.004)
        {
            for (int i = 0; i < 3; ++i)
            {
                // Each dot runs the same 1.4s pulse, staggered — opacity .3 -> 1 and a
                // slight swell, exactly like the reference's cosmo-pulse keyframes.
                const double phase = std::fmod(mNowMs - mT0 + kDotStaggerMs * i, kDotPulseMs) / kDotPulseMs;
                const double k = 0.5 - 0.5 * std::cos(phase * 6.28318530718);   // 0..1..0
                const double r = kDotR * (1.0 + 0.4 * k);
                const double x = cx + (i - 1) * kDotGap;
                drawRoundedRect(t, Rect{x - r, cy - r, r * 2.0, r * 2.0}, radius::pill(),
                                Paint::filled(palette::primaryAlpha((0.3 + 0.7 * k) * dotsA)));
            }
        }

        const double statusA = mix * a;
        if (statusA > 0.004 && !mStatus.empty())
        {
            // Spinner + text, centred as one unit so the line stays balanced however
            // long the name is. Ellipsize ONLY when the text actually exceeds the room
            // available — comparing against its own measured width (as a first cut did)
            // truncates every string, because adding "…" always makes it wider (R5).
            const double avail = w * kStatusMaxFrac - kSpinR * 2.0 - kSpinGap;
            std::string shown = mStatus;
            if (t.measureText(shown, kStatusPx, font::sans()) > avail)
            {
                while (!shown.empty() && t.measureText(shown + "…", kStatusPx, font::sans()) > avail)
                    shown.pop_back();
                shown += "…";
            }
            const double tw = t.measureText(shown, kStatusPx, font::sans());
            const double total = kSpinR * 2.0 + kSpinGap + tw;
            const double sx = cx - total * 0.5 + kSpinR;

            // The same rotating quarter-arc the filmstrip's loading cells use, so "work
            // is happening" reads identically everywhere in the app (R-LOADUX-2).
            const double a0 = std::fmod(mNowMs, kSpinMs) / kSpinMs * 6.28318530718;
            t.setStroke(palette::whiteAlpha(0.12 * statusA), 1.6);
            t.beginPath();
            t.moveTo(sx + kSpinR, cy);
            t.quadTo(sx + kSpinR, cy + kSpinR, sx, cy + kSpinR);
            t.quadTo(sx - kSpinR, cy + kSpinR, sx - kSpinR, cy);
            t.quadTo(sx - kSpinR, cy - kSpinR, sx, cy - kSpinR);
            t.quadTo(sx + kSpinR, cy - kSpinR, sx + kSpinR, cy);
            t.closePath();
            t.strokePath();
            t.setStroke(palette::primaryAlpha(0.9 * statusA), 1.6);
            t.beginPath();
            for (int i = 0; i <= 8; ++i)
            {
                const double ang = a0 + (double)i / 8.0 * 1.5707963268;   // a quarter turn
                const double px = sx + std::cos(ang) * kSpinR, py = cy + std::sin(ang) * kSpinR;
                if (i == 0) t.moveTo(px, py); else t.lineTo(px, py);
            }
            t.strokePath();

            t.setFill(palette::whiteAlpha(0.55 * statusA));
            t.drawText(shown, sx + kSpinR + kSpinGap, cy + kStatusPx * 0.36, kStatusPx, font::sans());
        }

        // ── version, bottom-right ──
        Color vc = palette::whiteAlpha(0.22 * a);
        t.setFill(vc);
        t.drawText(kVersion, w - 12.0 - t.measureText(kVersion, 9.0, font::mono()), h - 10.0, 9.0, font::mono());

        // ── progress bar pinned to the bottom edge ──
        drawRoundedRect(t, Rect{0, h - kBarH, w, kBarH}, 0.0,
                        Paint::filled(Color{0x1E / 255.0, 0x1E / 255.0, 0x1E / 255.0, a}));
        const double p = clamp01(mProgress.value());
        if (p > 0.001)
        {
            Color pc = palette::primary(); pc.a *= a;
            drawRoundedRect(t, Rect{0, h - kBarH, w * p, kBarH}, 0.0, Paint::filled(pc));
        }
    }
}
}
