/*
 *  interstellar/widgets — Transport: the scrubber and the play controls (R-PLAY-2/4).
 *
 *  The scrubber is DIRECT MANIPULATION — it follows the pointer exactly, with no easing. That is
 *  the one motion exemption in `arstro.design.rule` §1: the pointer IS the animation. Every
 *  value DERIVED from the playhead still eases, which is the monitor's and the timeline's job.
 *
 *  Play/pause is one button whose glyph CROSS-FADES rather than swapping, and the timecode is
 *  mono because a proportional face jitters as the number changes.
 */
#pragma once
#include "../Theme.h"
#include <functional>
#include <string>

namespace arstro
{
namespace interstellar_v1
{
    class Transport : public artboard::Segment
    {
    public:
        Transport();

        void setPlayhead(double t, double duration);
        void setPlaying(bool on) { mPlayingWanted = on; }
        void setFps(double fps) { mFps = fps; }
        void setDropped(int frames, int proxyEdge) { mDropped = frames; mProxyEdge = proxyEdge; }

        /** The live eased play/pause glyph cross-fade, 0 = pause shown, 1 = play shown. A test
         *  needs to read it to tell a tween from a snap. */
        double playFade() const { return mPlayFade.value(); }
        artboard::Rect scrubberRect() const;

        std::function<void(double)> onScrub;
        std::function<void(bool)> onPlayPause;
        std::function<void(int)> onStep;   // -1 / +1 frame, or -2 / +2 for a cut

        void layout();
        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        double mPlayhead = 0, mDuration = 0, mFps = 24.0;
        bool mPlayingWanted = false, mPlayingApplied = false;
        artboard::Property mPlayFade{0.0};
        int mDropped = 0, mProxyEdge = 0;
        bool mDragging = false;
        double mLastMs = 0;
        /** Measured in paint and read by scrubberRect: the gutter must fit the timecode, and a
         *  magic number did not — the text ran into the scrubber track. Found by looking. */
        mutable double mGutter = 172.0;
    };
}
}
