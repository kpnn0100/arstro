#include "Transport.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace artboard;

namespace arstro
{
namespace interstellar_v1
{
    namespace
    {
        std::string timecodeFrames(double t, double fps)
        {
            if (t < 0) t = 0;
            const long long f = (long long)std::llround(t * fps);
            const long long fpsI = std::max(1LL, (long long)std::llround(fps));
            char b[40];
            std::snprintf(b, sizeof b, "%02lld:%02lld:%02lld:%02lld", (f / fpsI) / 3600,
                          ((f / fpsI) / 60) % 60, (f / fpsI) % 60, f % fpsI);
            return b;
        }
    }

    Transport::Transport() { height.set(time::transportHeight()); }

    void Transport::setPlayhead(double t, double duration)
    {
        mPlayhead = t;
        mDuration = duration;
    }

    Rect Transport::scrubberRect() const
    {
        const double right = 150.0;
        return Rect{mGutter, height.value() * 0.5 - 2.0,
                    std::max(10.0, width.value() - mGutter - right), 4.0};
    }

    void Transport::layout() {}

    void Transport::advance(double nowMs)
    {
        mLastMs = nowMs;
        // A boolean setter has no clock: it records intent, and `advance` — which has one —
        // starts the tween. This is the `mXWanted`/`mXApplied` idiom.
        if (mPlayingWanted != mPlayingApplied)
        {
            mPlayFade.animateTo(mPlayingWanted ? 1.0 : 0.0, 120.0, Easing::EaseOutCubic, nowMs);
            mPlayingApplied = mPlayingWanted;
        }
        mPlayFade.update(nowMs);
        Segment::advance(nowMs);
    }

    void Transport::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        t.setFill(palette::card());
        t.beginPath();
        t.moveTo(0, 0); t.lineTo(w, 0); t.lineTo(w, h); t.lineTo(0, h);
        t.closePath();
        t.fillPath();
        t.setStroke(palette::border(), 1.0);
        t.beginPath();
        t.moveTo(0, 0); t.lineTo(w, 0);
        t.closePath();
        t.strokePath();

        // ── transport buttons ──
        const double cy = h * 0.5;
        auto glyphPrev = [&](double cx) {
            t.beginPath();
            t.moveTo(cx + 4, cy - 5); t.lineTo(cx - 3, cy); t.lineTo(cx + 4, cy + 5);
            t.closePath();
            t.fillPath();
        };
        t.setFill(palette::secondaryForeground());
        glyphPrev(22.0);
        // Play and pause CROSS-FADE rather than swapping: a glyph that swaps in one frame is the
        // snap R-G-1 forbids, and it is the commonest place to forget it.
        const double f = std::max(0.0, std::min(1.0, mPlayFade.value()));
        t.pushLayer(1.0 - f);
        t.setFill(palette::foreground());
        t.beginPath();
        t.moveTo(50.0 - 4, cy - 6); t.lineTo(50.0 + 6, cy); t.lineTo(50.0 - 4, cy + 6);
        t.closePath();
        t.fillPath();
        t.popLayer();
        t.pushLayer(f);
        t.setFill(palette::foreground());
        for (int i = 0; i < 2; ++i)
        {
            const double bx = 50.0 - 4 + i * 6.0;
            t.beginPath();
            t.moveTo(bx, cy - 6); t.lineTo(bx + 3, cy - 6); t.lineTo(bx + 3, cy + 6);
            t.lineTo(bx, cy + 6);
            t.closePath();
            t.fillPath();
        }
        t.popLayer();
        t.setFill(palette::secondaryForeground());
        t.save();
        t.setTransform(Transform::identity());
        t.restore();
        // next-cut glyph, mirrored
        t.beginPath();
        t.moveTo(78.0 - 4, cy - 5); t.lineTo(78.0 + 3, cy); t.lineTo(78.0 - 4, cy + 5);
        t.closePath();
        t.fillPath();

        // ── timecode: MONO, always — and the gutter is MEASURED from it, so the scrubber
        //    starts after the text instead of under it.
        const std::string tc = timecodeFrames(mPlayhead, mFps);
        t.setFill(palette::foreground());
        t.drawText(tc, 96.0, cy + 3.5, 10.0, font::mono(), 0.0);
        mGutter = 96.0 + t.measureText(tc, 10.0, font::mono(), 0.0) + 13.0;

        // ── the scrubber: a track, a filled reach, and a thumb ──
        const Rect s = scrubberRect();
        drawRoundedRect(t, s, radius::pill(), Paint::filled(palette::switchBackground()));
        const double frac = mDuration > 0 ? std::max(0.0, std::min(1.0, mPlayhead / mDuration)) : 0.0;
        if (frac > 0)
            drawRoundedRect(t, Rect{s.x, s.y, s.w * frac, s.h}, radius::pill(),
                            Paint::filled(palette::primary()));
        const double thumbX = s.x + s.w * frac;
        drawRoundedRect(t, Rect{thumbX - 4.0, cy - 6.0, 8.0, 12.0}, radius::pill(),
                        Paint::filled(palette::foreground()));

        // ── readouts, not warnings. Degrading under load is correct behaviour, and a red badge
        //    would make it look like a fault (R-NFR-4). ──
        if (mDropped > 0 || mProxyEdge > 0)
        {
            std::string r;
            if (mProxyEdge > 0) r = "proxy " + std::to_string(mProxyEdge);
            if (mDropped > 0) r += (r.empty() ? "" : "  ") + std::string("dropped ") + std::to_string(mDropped);
            const double rw = t.measureText(r, 9.0, font::mono(), 0.0);
            t.setFill(palette::mutedForeground());
            t.drawText(r, w - rw - 9.75, cy + 3.0, 9.0, font::mono(), 0.0);
        }
    }

    bool Transport::handleGesture(const Gesture &g, const Point &local)
    {
        const Rect s = scrubberRect();
        auto scrubTo = [&](double x) {
            if (!onScrub || s.w <= 0) return;
            const double frac = std::max(0.0, std::min(1.0, (x - s.x) / s.w));
            onScrub(frac * mDuration);
        };
        switch (g.type)
        {
            case Gesture::Type::Down:
                if (local.x >= 10 && local.x <= 34 && onStep) { onStep(-1); return true; }
                if (local.x >= 38 && local.x <= 62 && onPlayPause) { onPlayPause(!mPlayingWanted); return true; }
                if (local.x >= 66 && local.x <= 90 && onStep) { onStep(1); return true; }
                if (local.x >= s.x - 6 && local.x <= s.x + s.w + 6)
                {
                    // Direct manipulation: no easing, no snapping, no threshold. The pointer is
                    // the animation.
                    mDragging = true;
                    scrubTo(local.x);
                    return true;
                }
                break;
            case Gesture::Type::Drag:
            case Gesture::Type::Move:
                if (mDragging) { scrubTo(local.x); return true; }
                break;
            case Gesture::Type::Up:
                mDragging = false;
                break;
            default: break;
        }
        return Segment::handleGesture(g, local);
    }
}
}
