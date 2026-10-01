/*
 *  interstellar_v1 — Transport: ◀◀ ▶ ▶▶, the timecode, and a scrubber, under the monitor.
 *
 *  Lives with the monitor, outside the tab host, so it is the same control in every tab.
 *
 *  The scrubber is DIRECT MANIPULATION (the design rule's one exemption): while dragged, the
 *  thumb is exactly under the pointer and every move dispatches `playhead <t>` (frame-quantised,
 *  only when the frame changed). Released, the thumb follows the MODEL — and a model value that
 *  jumps (a click on the timeline, `playhead next-cut`, an agent) is a value catching up to a
 *  source it does not control, so it EASES there over 220 ms. During playback the model advances
 *  every frame by a frame's worth; easing that would only add lag, so small steps while playing
 *  are followed directly and only a jump eases. `displayedTime()` is that live value.
 *
 *  The play/pause glyph CROSS-FADES (160 ms) rather than swapping. Prev/next dispatch the grammar's
 *  own `playhead prev-cut` / `playhead next-cut`.
 */
#pragma once
#include "../Theme.h"
#include "../AppHooks.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar_v1
{
    class Transport : public artboard::Segment
    {
    public:
        Transport();

        void bind(const interstellar::AppModel &m);

        double displayedTime() const { return mShown.value(); }
        double playAmount() const { return mPlayAmt.value(); }   // 0 = play glyph, 1 = pause glyph
        bool scrubbing() const { return mScrubbing; }
        artboard::Rect buttonRect(int i) const;   // 0 prev-cut, 1 play/pause, 2 next-cut
        artboard::Rect scrubRect() const;         // the track's hit band
        double timeToX(double t) const;

        std::function<void(const std::string &line)> onCommand;

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        double xToTime(double x) const;
        void scrubTo(double x);
        void emit(const std::string &line) { if (onCommand) onCommand(line); }

        double mDuration = 0.0, mFps = 24.0, mModelTime = 0.0;
        bool mPlaying = false, mPlayApplied = false, mInit = false;
        std::vector<double> mMarkers;
        double mTargetTime = 0.0, mLastTarget = -1.0;
        artboard::AnimatedProperty mShown{0.0};
        artboard::AnimatedProperty mPlayAmt{0.0};
        bool mScrubbing = false;
        long long mLastSentFrame = -1;
        cosmo_v2::HoverFade mHover;
    };
}
}
