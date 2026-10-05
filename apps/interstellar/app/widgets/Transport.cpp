#include "Transport.h"
#include "anim/Motion.h"
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
        constexpr int kButtons = 4;           // prev-cut, play/pause, next-cut, capture (R-UI-11)
        constexpr double kPad = 9.75;
        constexpr double kTcW = 88.0;        // "00:00:04:07" at 11 px mono, with air
        constexpr double kDurW = 76.0;
        constexpr double kTcPx = 11.0;
        constexpr double kPlayFadeMs = 160.0;
        /** Below this, a playing playhead's step is followed directly (it IS the motion). */
        constexpr double kFollowStepS = 0.5;
        constexpr double kMeterW = 64.0;      // the stereo meter (R-AUD-8)
        constexpr double kBadgeW = 28.0;      // the shuttle badge's room (R-EDT-2), taken from the scrubber
        constexpr double kFloorDb = -48.0;
        inline double toDb(double v) { return v > 1e-6 ? 20.0 * std::log10(v) : -120.0; }
        Color fade(Color c, double a) { c.a *= a; return c; }
    }

    Transport::Transport() { height.set(shell::transportH()); }

    void Transport::bind(const interstellar::AppModel &m)
    {
        mFps = m.fps > 0 ? m.fps : 24.0;
        mMarkers.clear();
        if (mSourceMode)
        {
            // R-EDT-1: the source viewer's own clock
            mDuration = std::max(0.0, m.sourceDuration);
            mModelTime = m.sourcePlayhead;
            mPlaying = false;
            mMarkA = m.sourceIn;
            mMarkB = m.sourceOut;
        }
        else
        {
            mDuration = std::max(0.0, m.duration);
            mModelTime = m.playhead;
            mPlaying = m.playing;
            mMarkA = m.markIn;
            mMarkB = m.markOut;
            for (const auto &mk : m.markers) mMarkers.push_back(mk.at);
        }
        if (!mScrubbing) mTargetTime = mModelTime;   // a gesture in flight outranks the model
        // R-EDT-2: the rate, said while it is not plain play
        mBadgeOn = !mSourceMode && m.playing && std::fabs(m.shuttle) > 0 && std::fabs(m.shuttle - 1.0) > 1e-9;
        if (mBadgeOn)
        {
            char b[16];
            std::snprintf(b, sizeof b, "%s%g\xC3\x97", m.shuttle < 0 ? "\xE2\x88\x92" : "", std::fabs(m.shuttle));
            mBadgeWanted = b;
        }
        mPeakIn[0] = m.meterPeakL; mPeakIn[1] = m.meterPeakR;
        mRmsIn[0] = m.meterRmsL; mRmsIn[1] = m.meterRmsR;
        mClipIn = m.meterClip;
        mSoundIn = false;
        for (const auto &tl : m.timelines) if (tl.id == m.currentTimeline) mSoundIn = tl.hasSound;
    }

    Rect Transport::meterRect() const
    {
        const double a = mMeterAmt.value();
        const double x1 = width.value() - kPad - kDurW - 6.0;
        return Rect{x1 - kMeterW * a, height.value() * 0.5 - 6.0, kMeterW * a, 12.0};
    }

    Rect Transport::buttonRect(int i) const
    {
        const double y = (height.value() - kBtn) * 0.5;
        return Rect{kPad + i * (kBtn + 2.0), y, kBtn, kBtn};
    }

    Rect Transport::scrubRect() const
    {
        const double x0 = kPad + kButtons * (kBtn + 2.0) + 6.0 + kTcW + 6.0 + (kBadgeW + 4.0) * mBadgeAmt.value();
        // the meter, when shown, takes its width from the scrubber — through its eased amount
        const double x1 = width.value() - kPad - kDurW - 6.0 - (kMeterW + 10.0) * mMeterAmt.value();
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
            emit((mSourceMode ? "source playhead " : "playhead ") + cmd::seconds(t, mFps));
        }
    }

    bool Transport::handleGesture(const Gesture &g, const Point &local)
    {
        switch (g.type)
        {
        case Gesture::Type::Move:
        {
            int id = -1;
            for (int i = 0; i < kButtons; ++i) if (buttonRect(i).contains(local)) id = i;
            if (id < 0 && scrubRect().contains(local)) id = kButtons;
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
            if (buttonRect(3).contains(local))
            {
                const Rect r = buttonRect(3);
                const Point o = worldTransform().apply(Point{r.x, r.y});
                if (onCapture) onCapture(Rect{o.x, o.y, r.w, r.h});
                return true;
            }
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
        // R-AUD-8: meter ballistics, continuous; reduced motion shows the level as it is
        const double dt = mMeterInit ? std::clamp(nowMs - mLastMs, 0.0, 200.0) : 0.0;
        mLastMs = nowMs;
        if (!mMeterInit)
        {
            mMeterAmt.set(mSoundIn ? 1.0 : 0.0);
            mSoundApplied = mSoundIn;
            mMeterInit = true;
        }
        for (int c = 0; c < 2; ++c)
        {
            auto ballistic = [&](double &live, double target) {
                if (reducedMotion()) { live = target; return; }
                if (target > live) live += (target - live) * std::min(1.0, dt / 30.0);   // attack: ~30 ms
                else live = std::max(target, live - 24.0 * dt / 1000.0);                // release: 24 dB/s
            };
            ballistic(mPeakDb[c], std::max(-120.0, toDb(mPeakIn[c])));
            ballistic(mRmsDb[c], std::max(-120.0, toDb(mRmsIn[c])));
            if (mPeakDb[c] >= mHoldDb[c]) { mHoldDb[c] = mPeakDb[c]; mHoldAt[c] = nowMs; }
            else if (nowMs - mHoldAt[c] > 1500.0) mHoldDb[c] = reducedMotion() ? mPeakDb[c] : std::max(mPeakDb[c], mHoldDb[c] - 20.0 * dt / 1000.0);
        }
        if (mClipIn != mClipApplied)
        {
            mClipAmt.animateTo(mClipIn ? 1.0 : 0.0, mClipIn ? motion::kHoverMs : 300.0, Easing::EaseOutCubic, nowMs);
            mClipApplied = mClipIn;
        }
        mClipAmt.update(nowMs);
        if (mSoundIn != mSoundApplied)
        {
            mMeterAmt.animateTo(mSoundIn ? 1.0 : 0.0, motion::kScrollMs, Easing::EaseOutCubic, nowMs);
            mSoundApplied = mSoundIn;
        }
        mMeterAmt.update(nowMs);
        if (mBadgeOn != mBadgeApplied)
        {
            mBadgeAmt.animateTo(mBadgeOn ? 1.0 : 0.0, motion::kHoverMs, Easing::EaseOutCubic, nowMs);
            mBadgeApplied = mBadgeOn;
        }
        if (mBadgeOn) mBadgeText = mBadgeWanted;   // the text holds while it fades out
        mBadgeAmt.update(nowMs);
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

        for (int i = 0; i < kButtons; ++i)
        {
            const Rect r = buttonRect(i);
            const double hv = mHover.amount(i);
            if (hv > 0.001) drawRoundedRect(t, r, radius::control(), Paint::filled(palette::hoverWash(hv)));
            const Color c = lerpColor(i == 1 ? palette::foreground() : palette::mutedForeground(), palette::white(), 0.5 * hv);
            const Rect gb{r.x + 7.0, r.y + 7.0, r.w - 14.0, r.h - 14.0};
            if (i == 0) glyph::skip(t, gb, c, -1);
            else if (i == 2) glyph::skip(t, gb, c, +1);
            else if (i == 3) glyph::camera(t, gb, c);
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
        const double tcX = kPad + kButtons * (kBtn + 2.0) + 6.0;
        t.setFill(mSourceMode ? palette::primary() : palette::foreground());   // the source's clock reads in the accent
        t.drawText(tc, tcX, textfit::baseline(cy, kTcPx), kTcPx, font::mono());
        if (const double ba = mBadgeAmt.value(); ba > 0.001 && !mBadgeText.empty())
        {
            // R-EDT-2: the shuttle rate, over the timecode's right end
            const double bw = std::min(kBadgeW, t.measureText(mBadgeText, 9.0, font::monoMedium()) + 8.0);
            const Rect br{tcX + kTcW + 4.0, cy - 7.0, bw, 14.0};   // in the room the scrubber eased aside
            Color bg = palette::primary();
            bg.a *= ba;
            drawRoundedRect(t, br, radius::control(), Paint::filled(bg));
            Color fg = palette::primaryForeground();
            fg.a *= ba;
            t.setFill(fg);
            t.drawText(mBadgeText, br.x + 4.0, textfit::baseline(cy, 9.0), 9.0, font::monoMedium());
        }

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
            // R-EDT-1: the In and Out as brackets, the span between them washed
            if (mMarkA >= 0 && mMarkB > mMarkA)
                drawRoundedRect(t, Rect{timeToX(mMarkA), cy - 5.0, std::max(0.0, timeToX(mMarkB) - timeToX(mMarkA)), 10.0}, radius::hairline(), Paint::filled(palette::whiteAlpha(0.06)));
            auto bracket = [&](double at, int dir) {
                const double bx = timeToX(at);
                glyph::line(t, bx, cy - 6.0, bx, cy + 6.0, palette::foreground(), 1.0);
                glyph::line(t, bx, cy - 6.0, bx + dir * 3.0, cy - 6.0, palette::foreground(), 1.0);
                glyph::line(t, bx, cy + 6.0, bx + dir * 3.0, cy + 6.0, palette::foreground(), 1.0);
            };
            if (mMarkA >= 0) bracket(mMarkA, +1);
            if (mMarkB >= 0) bracket(mMarkB, -1);
            const double r = 4.5 + 1.0 * hv;
            drawCircle(t, x, cy, r, Paint::filled(palette::white()));
        }
        if (const double ma = mMeterAmt.value(); ma > 0.001)
        {
            // R-AUD-8: two bars, left over right; RMS quiet, peak bright, the top 3 dB destructive
            const Rect mr = meterRect();
            auto xOf = [&](double db) { return mr.x + std::clamp((db - kFloorDb) / -kFloorDb, 0.0, 1.0) * mr.w; };
            const double hotX = xOf(-3.0);
            for (int c = 0; c < 2; ++c)
            {
                const double y = mr.y + 2.0 + c * 5.0, bh = 3.0;
                drawRoundedRect(t, Rect{mr.x, y, mr.w, bh}, radius::hairline(), Paint::filled(fade(palette::secondary(), ma)));
                const double rx = xOf(mRmsDb[c]), px = xOf(mPeakDb[c]);
                if (rx > mr.x) drawRoundedRect(t, Rect{mr.x, y, std::min(rx, hotX) - mr.x, bh}, radius::hairline(), Paint::filled(fade(palette::success(), 0.45 * ma)));
                if (px > mr.x) drawRoundedRect(t, Rect{mr.x, y, std::min(px, hotX) - mr.x, bh}, radius::hairline(), Paint::filled(fade(palette::success(), 0.9 * ma)));
                if (px > hotX) drawRoundedRect(t, Rect{hotX, y, px - hotX, bh}, radius::hairline(), Paint::filled(fade(palette::destructive(), ma)));
                const double hx = xOf(mHoldDb[c]);
                if (mHoldDb[c] > kFloorDb) glyph::line(t, hx, y - 0.5, hx, y + bh + 0.5, fade(palette::white(), 0.8 * ma), 1.0);
            }
            // the clip lamp
            drawCircle(t, mr.right() + 5.0, mr.y + 6.0, 2.5, Paint::filled(fade(palette::secondary(), ma)));
            if (const double ca = mClipAmt.value(); ca > 0.001) drawCircle(t, mr.right() + 5.0, mr.y + 6.0, 2.5, Paint::filled(fade(palette::destructive(), ca * ma)));
        }
        const std::string dur = cmd::timecode(mDuration, mFps);
        t.setFill(palette::mutedForeground());
        t.drawText(dur, w - kPad - t.measureText(dur, 10.0, font::mono()), textfit::baseline(cy, 10.0), 10.0, font::mono());
    }
}
}
