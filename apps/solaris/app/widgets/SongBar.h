/*
 *  solaris_ui — SongBar: the song screen's top bar (R-UI-3, R-TIME-4, R-PLAY-3).
 *
 *  Left → right: the `solaris.` wordmark (13 px, the dot in the accent) — it IS Home, as cosmo's and Interstellar's
 *  are (R-UI-3 amended 2026-10-16) — then Settings, cosmo's
 *  `MenuStrip` (File · Edit · Song · View — R-UI-3 amended: *"settings at top left next to home"*),
 *  the song's name with an unsaved dot; then the transport — Play/Stop, the position as
 *  bar.beat.tick in mono (it changes every frame while playing, so it must not jitter: mono), the
 *  tempo — centred when there is room, pushed right of the name when there is not; on the right
 *  the master meter and Save. Cosmo's TopBar height (29.25 = 9 units). The menus' items are the
 *  App's (every one a command line or a host picker); this bar only places the strip.
 *
 *  The tempo is a control (R-UI-13): dragged up or down it is the pointer's, a bpm a step (Shift: a tenth),
 *  one `set project.bpm=<n>` per step that moved it; a double-click types one in (the App's field). A
 *  tempo changed from elsewhere is drawn easing to its new value.
 *
 *  The play glyph and the stop glyph CROSS-FADE (200 ms) when the transport changes, whoever
 *  changed it; the unsaved dot fades in and out; the meter rises fast and falls slowly (a meter's
 *  ballistics, eased per frame from the model's peaks — §1 holds: nothing jumps a frame).
 */
#pragma once
#include "../Theme.h"
#include "AppModel.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include "../../../cosmo/widgets/MenuStrip.h"
#include <memory>
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
        enum Hit { kNone = -1, kHome = 0, kPlay, kSave, kSettings, kBpm, kRecord, kLearn }; // kHome: the wordmark; kRecord: R-MIDI-2; kLearn: R-CTRL-1
        static constexpr double kBpmStepPx = 3.25; // space::u(1) of vertical drag: one step
        static constexpr double kNameMin = 120.0; // the song's name keeps this much before the transport moves right

        SongBar();
        void bind(const solaris::AppModel &m);
        void layout();
        cosmo_v2::MenuStrip &menus() { return *mMenus; }
        artboard::Rect hitRect(int which) const;
        double playAmount() const { return mPlaying.value(); }   // 0 = the play glyph, 1 = the stop glyph (LIVE)
        double recordAmount() const { return mRecLit.value(); }  // R-MIDI-2: 1 = recording, lit red (LIVE)
        /** R-CTRL-1: while a `map learn` waits, a pill after the tempo says for what (a click cancels it) — LIVE, eased. */
        double learnAmount() const { return mLearn.value(); }
        const std::string &learnShown() const { return mLearnShown; }
        double meterLevel(int ch) const { return mMeter[ch].value(); }
        double bpmShown() const { return mBpmDrag ? mBpmLive : mBpmShown.value(); } // the tempo as DRAWN (LIVE)
        /** R-TEMPO-1: the line that sets the tempo shown — the song's, or the change in effect at the time bar. */
        std::string tempoLine(const std::string &v) const { return mTempoId.empty() ? "set project.bpm=" + v : "tempo move " + mTempoId + " --bpm " + v; }

        std::function<void()> onHome, onPlayToggle, onSave, onSettings, onRecordToggle;
        std::function<bool(const std::string &line)> onCommand;
        /** A value typed in place (the App's field): the current text, where, and what to do with the result. */
        std::function<void(const std::string &current, artboard::Point world, std::function<void(const std::string &)> done)> onEditText;

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        mutable double mWordW = 52.0; // the `solaris.` wordmark, measured in the paint (Home's hit rect)
        int hitAt(const artboard::Point &p) const;
        std::string mName;
        bool mDirty = false, mPlayingWanted = false, mRecordingWanted = false;
        double mPosition = 0, mBpm = 120;
        std::string mTempoId; // R-TEMPO-1: the change in effect at the time bar ('' = the song's bpm)
        artboard::AnimatedProperty mBpmShown{120.0};
        double mBpmLast = -1, mBpmPress = 0, mBpmPressY = 0, mBpmLive = 0, mNowMs = 0;
        bool mBpmDrag = false;
        std::string mBpmSent;
        int mBeatsPerBar = 4;
        float mPeak[2] = {0, 0};
        artboard::AnimatedProperty mRecLit{0.0};
        std::string mLearning, mLearnShown; // R-CTRL-1: what a learn waits for; what the pill says (kept while it fades out)
        artboard::AnimatedProperty mLearn{0.0};
        artboard::AnimatedProperty mPlaying{0.0}, mDirtyDot{0.0}, mMeter[2]{artboard::AnimatedProperty{0.0}, artboard::AnimatedProperty{0.0}};
        bool mInit = false;
        cosmo_v2::HoverFade mHover;
        std::shared_ptr<cosmo_v2::MenuStrip> mMenus;
        double transportX() const;
    };
}
}
