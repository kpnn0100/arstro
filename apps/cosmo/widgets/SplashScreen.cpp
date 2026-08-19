#include "SplashScreen.h"
#include <cstdio>
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
        // ── the progress bar (R-SPLASH-5) ──
        // It used to be a 2 px hairline pinned to the very bottom edge, full-bleed and square,
        // which is invisible at a glance and unreadable at a fraction. It is a real element now:
        // inset, rounded, with its own track, sitting under the status line where the eye
        // already is — and it fades IN with the first real work rather than sitting at zero.
        constexpr double kBarH = 4.0;
        constexpr double kBarInset = 44.0;      // matches the tagline's optical margin
        constexpr double kBarBottom = 34.0;     // clear of the version line
        constexpr double kCountPx = 9.5;
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
        // The bar's TRACK comes in with the dots, so by the time there is anything to report
        // the bar is already there and simply fills.
        mBarFade.set(0.0); mBarFade.animateTo(1.0, 500.0, Easing::EaseOutCubic, nowMs + 600.0);
    }

    void SplashScreen::setProgress(double p, int done, int total)
    {
        mDone = done; mTotal = total;
        // The bar's fade-in used to start HERE, on the first real work, so that an empty
        // track would not sit through the intro looking stuck. Measurement reversed that:
        // `ui dump --root splash` at 80 ms showed a cover decoding in ~7 ms (R-SPLASH-5), so
        // the first setProgress() and the last one land in the same frame the splash begins
        // leaving — the 260 ms fade-in got 80 ms and 23% opacity before the exit swallowed
        // it, and the bar was, in practice, never visible. The track now arrives with the
        // dots in begin(); an empty 4 px track at 10% white reads as chrome, not as a stall.
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
        if (mExiting || mExitRequested) return;
        mExitRequested = true;   // advance() starts the fade once the bar has caught up
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
        mBarFade.update(nowMs);
        // Hold the exit until the eased fill has actually reached the end of the track. The
        // ease is 220 ms and the fade 260 ms, so starting them together drew the bar at 96%
        // and 0.1% alpha: the one frame that says "finished" was the one frame nobody saw.
        // Costs ~200 ms of launch to make the completion legible, which is the trade R-SPLASH
        // already makes for the intro itself.
        if (mExitRequested && !mExiting && mProgress.value() >= 0.995)
        {
            mExiting = true;
            mExit.animateTo(0.0, 260.0, Easing::EaseOutCubic, nowMs);
        }
        mExit.update(nowMs);
        Segment::advance(nowMs);
    }

    // P0.6 — what the last frame actually put on screen, in one line. Deliberately built
    // from the SAME fields and constants onPaint() reads, so it cannot drift into describing
    // a splash that is not the one being drawn. The one thing it will not claim is the
    // track's final width: onPaint shortens it by the measured width of the "n of N" count,
    // and measuring needs a render target this call does not have — so it reports the
    // untrimmed track and the count separately and lets the caller see both.
    std::string SplashScreen::uiDetail() const
    {
        char buf[320];
        const double a = mExit.value();
        const double barA = a * mBarFade.value();
        std::string count = (mDone >= 0 && mTotal > 0)
                                ? std::to_string(mDone) + " of " + std::to_string(mTotal)
                                : std::string("-");
        std::snprintf(buf, sizeof(buf),
                      "progress=%.3f barAlpha=%.3f bar=%.0f,%.0f %.0fx%.0f fill=%.0f count=%s "
                      "alpha=%.3f intro=%s status=\"%s\" statusMix=%.2f exiting=%d exitReq=%d",
                      clamp01(mProgress.value()), barA,
                      kBarInset, height.value() - kBarBottom,
                      width.value() - kBarInset * 2.0, kBarH,
                      std::max((width.value() - kBarInset * 2.0) * clamp01(mProgress.value()), kBarH),
                      count.c_str(), a, introDone() ? "done" : "playing",
                      mStatus.c_str(), mStatusMix.value(), mExiting ? 1 : 0,
                      mExitRequested ? 1 : 0);
        return buf;
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

        // ── progress bar ──
        const double barA = a * mBarFade.value();
        if (barA > 0.001)
        {
            const double by = h - kBarBottom;
            double bw = w - kBarInset * 2.0;
            // The count sits to the right of the bar rather than above it, so the two read as
            // one control; the bar gives up exactly the width the text needs.
            std::string count;
            if (mDone >= 0 && mTotal > 0) count = std::to_string(mDone) + " of " + std::to_string(mTotal);
            double cw = count.empty() ? 0.0 : t.measureText(count, kCountPx, font::mono()) + 10.0;
            bw -= cw;
            if (bw < 40.0) { bw = w - kBarInset * 2.0; cw = 0.0; count.clear(); }   // too narrow: drop the count

            drawRoundedRect(t, Rect{kBarInset, by, bw, kBarH}, kBarH * 0.5,
                            Paint::filled(palette::whiteAlpha(0.10 * barA)));
            const double p = clamp01(mProgress.value());
            if (p > 0.001)
            {
                Color pc = palette::primary(); pc.a *= barA;
                // Never narrower than the cap radius, or a small fraction draws as a smudge
                // instead of as a rounded end.
                drawRoundedRect(t, Rect{kBarInset, by, std::max(bw * p, kBarH), kBarH}, kBarH * 0.5,
                                Paint::filled(pc));
            }
            if (!count.empty())
            {
                t.setFill(palette::whiteAlpha(0.34 * barA));
                t.drawText(count, kBarInset + bw + 10.0, by + kBarH * 0.5 + kCountPx * 0.36,
                           kCountPx, font::mono());
            }
        }
    }
}
}
