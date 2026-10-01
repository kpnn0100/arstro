#include "Transport.h"
#include "CommandLine.h"
#include "Glyphs.h"
#include "TextFit.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace interstellar_v1
{
    using namespace artboard;

    namespace
    {
        constexpr double kBtn = 26.0;
        constexpr double kPad = 9.75;
        constexpr double kTcW = 88.0;        // "00:00:04:07" at 11 px mono, with air
        constexpr double kDurW = 76.0;
        constexpr double kTcPx = 11.0;
        constexpr double kPlayFadeMs = 160.0;
        /** Below this, a playing playhead's step is followed directly (it IS the motion). */
        constexpr double kFollowStepS = 0.5;
    }

    Transport::Transport() { height.set(shell::transportH()); }

    void Transport::bind(const interstellar::AppModel &m)
    {
        mDuration = std::max(0.0, m.duration);
        mFps = m.fps > 0 ? m.fps : 24.0;
        mModelTime = m.playhead;
        mPlaying = m.playing;
        mMarkers.clear();
        for (const auto &mk : m.markers) mMarkers.push_back(mk.at);
        if (!mScrubbing) mTargetTime = m.playhead;   // a gesture in flight outranks the model
    }

    Rect Transport::buttonRect(int i) const
    {
        const double y = (height.value() - kBtn) * 0.5;
        return Rect{kPad + i * (kBtn + 2.0), y, kBtn, kBtn};
    }

    Rect Transport::scrubRect() const
    {
        const double x0 = kPad + 3 * (kBtn + 2.0) + 6.0 + kTcW + 6.0;
        const double x1 = width.value() - kPad - kDurW - 6.0;
        return Rect{x0, 0, std::max(0.0, x1 - x0), height.value()};
    }

    double Transport::timeToX(double t) const
    {
        const Rect r = scrubRect();
        if (mDuration <= 0.0) return r.x;
        return r.x + std::clamp(t / mDuration, 0.0, 1.0) * r.w;
    }

    double Transport::xToTime(double x) const
    {
        const Rect r = scrubRect();
        if (r.w <= 0.0 || mDuration <= 0.0) return 0.0;
        return std::clamp((x - r.x) / r.w, 0.0, 1.0) * mDuration;
    }

    void Transport::scrubTo(double x)
    {
        const double t = xToTime(x);
        mTargetTime = t;
        mShown.set(t);           // direct manipulation: exactly under the pointer
        mLastTarget = t;
        const long long frame = (long long)std::llround(t * mFps);
        if (frame != mLastSentFrame)
        {
            mLastSentFrame = frame;
            emit("playhead " + cmd::seconds(t, mFps));
        }
    }

    bool Transport::handleGesture(const Gesture &g, const Point &local)
    {
        switch (g.type)
        {
        case Gesture::Type::Move:
        {
            int id = -1;
            for (int i = 0; i < 3; ++i) if (buttonRect(i).contains(local)) id = i;
            if (id < 0 && scrubRect().contains(local)) id = 3;
            mHover.setHovered(id);
            return true;
        }
        case Gesture::Type::Down:
            if (scrubRect().contains(local) && mDuration > 0.0)
            {
                mScrubbing = true;
                mLastSentFrame = -1;
                scrubTo(local.x);
            }
            return true;
        case Gesture::Type::DragStart:
        case Gesture::Type::Drag:
            if (mScrubbing) scrubTo(local.x);
            return true;
        case Gesture::Type::Up:
        case Gesture::Type::Drop:
            mScrubbing = false;
            return true;
        case Gesture::Type::Click:
            if (buttonRect(0).contains(local)) { emit("playhead prev-cut"); return true; }
            if (buttonRect(1).contains(local)) { emit(mPlaying ? "pause" : "play"); return true; }
            if (buttonRect(2).contains(local)) { emit("playhead next-cut"); return true; }
            return true;
        default:
            break;
        }
        return Segment::handleGesture(g, local);
    }

    void Transport::advance(double nowMs)
    {
        if (!mInit)
        {
            mShown.set(mTargetTime);
            mLastTarget = mTargetTime;
            mPlayAmt.set(mPlaying ? 1.0 : 0.0);
            mPlayApplied = mPlaying;
            mInit = true;
        }
        if (!mScrubbing && mTargetTime != mLastTarget)
        {
            if (mPlaying && std::fabs(mTargetTime - mShown.value()) < kFollowStepS) mShown.set(mTargetTime);
            else mShown.animateTo(mTargetTime, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs);
            mLastTarget = mTargetTime;
        }
        mShown.update(nowMs);
        if (mPlaying != mPlayApplied)
        {
            mPlayAmt.animateTo(mPlaying ? 1.0 : 0.0, kPlayFadeMs, Easing::EaseOutCubic, nowMs);
            mPlayApplied = mPlaying;
        }
        mPlayAmt.update(nowMs);
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    void Transport::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, 0.0, Paint::filled(palette::card()));
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(0, 0); t.lineTo(w, 0); t.strokePath();

        for (int i = 0; i < 3; ++i)
        {
            const Rect r = buttonRect(i);
            const double hv = mHover.amount(i);
            if (hv > 0.001) drawRoundedRect(t, r, radius::control(), Paint::filled(palette::hoverWash(hv)));
            const Color c = lerpColor(i == 1 ? palette::foreground() : palette::mutedForeground(), palette::white(), 0.5 * hv);
            const Rect gb{r.x + 7.0, r.y + 7.0, r.w - 14.0, r.h - 14.0};
            if (i == 0) glyph::skip(t, gb, c, -1);
            else if (i == 2) glyph::skip(t, gb, c, +1);
            else
            {
                // the play/pause glyph cross-fades rather than swapping
                const double p = mPlayAmt.value();
                if (p < 0.999) { Color a = c; a.a *= (1.0 - p); glyph::play(t, gb, a); }
                if (p > 0.001) { Color a = c; a.a *= p; glyph::pause(t, gb, a); }
            }
        }

        const double cy = h * 0.5;
        const std::string tc = cmd::timecode(mShown.value(), mFps);
        const double tcX = kPad + 3 * (kBtn + 2.0) + 6.0;
        t.setFill(palette::foreground());
        t.drawText(tc, tcX, textfit::baseline(cy, kTcPx), kTcPx, font::mono());

        // scrubber: pill track, accent fill to the shown time, marker ticks, white thumb
        const Rect sr = scrubRect();
        if (sr.w > 4.0)
        {
            const double hv = mHover.amount(3);
            const double th = 3.0 + 1.0 * hv;   // hover is already 1 under a press, so no snap on Down
            drawRoundedRect(t, Rect{sr.x, cy - th * 0.5, sr.w, th}, radius::pill(), Paint::filled(palette::secondary()));
            const double x = timeToX(mShown.value());
            if (x - sr.x >= th)   // a pill shorter than its height reads as a dot that pops (gotcha 12)
                drawRoundedRect(t, Rect{sr.x, cy - th * 0.5, x - sr.x, th}, radius::pill(), Paint::filled(palette::primary()));
            for (double mt : mMarkers)
            {
                const double mx = timeToX(mt);
                glyph::line(t, mx, cy - 6.0, mx, cy - 3.0, surface::marker(), 1.0);
            }
            const double r = 4.5 + 1.0 * hv;
            drawCircle(t, x, cy, r, Paint::filled(palette::white()));
        }
        const std::string dur = cmd::timecode(mDuration, mFps);
        t.setFill(palette::mutedForeground());
        t.drawText(dur, w - kPad - t.measureText(dur, 10.0, font::mono()), textfit::baseline(cy, 10.0), 10.0, font::mono());
    }
}
}
