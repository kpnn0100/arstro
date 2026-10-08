/*
 *  solaris_ui — SongBar: the song screen's top bar (R-UI-3, R-TIME-4, R-PLAY-3).
 *
 *  Left → right: the `solaris.` wordmark (13 px, the dot in the accent), Home, the song's name with
 *  an unsaved dot; in the middle the transport — Play/Stop, the position as bar.beat.tick in mono
 *  (it changes every frame while playing, so it must not jitter: mono), the tempo — and on the
 *  right the master meter, Save and the Settings gear. Cosmo's TopBar height (29.25 = 9 units).
 *
 *  The play glyph and the stop glyph CROSS-FADE (200 ms) when the transport changes, whoever
 *  changed it; the unsaved dot fades in and out; the meter rises fast and falls slowly (a meter's
 *  ballistics, eased per frame from the model's peaks — §1 holds: nothing jumps a frame).
 */
#pragma once
#include "../Theme.h"
#include "AppModel.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <functional>
#include <string>

namespace arstro
{
namespace solaris_ui
{
    class SongBar : public artboard::Segment
    {
    public:
        static constexpr double kHeight = 29.25;
        enum Hit { kNone = -1, kHome = 0, kPlay, kSave, kSettings };

        SongBar();
        void bind(const solaris::AppModel &m);
        artboard::Rect hitRect(int which) const;
        double playAmount() const { return mPlaying.value(); }   // 0 = the play glyph, 1 = the stop glyph (LIVE)
        double meterLevel(int ch) const { return mMeter[ch].value(); }

        std::function<void()> onHome, onPlayToggle, onSave, onSettings;

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        int hitAt(const artboard::Point &p) const;
        std::string mName;
        bool mDirty = false, mPlayingWanted = false;
        double mPosition = 0, mBpm = 120;
        int mBeatsPerBar = 4;
        float mPeak[2] = {0, 0};
        artboard::AnimatedProperty mPlaying{0.0}, mDirtyDot{0.0}, mMeter[2]{artboard::AnimatedProperty{0.0}, artboard::AnimatedProperty{0.0}};
        bool mInit = false;
        cosmo_v2::HoverFade mHover;
    };
}
}
