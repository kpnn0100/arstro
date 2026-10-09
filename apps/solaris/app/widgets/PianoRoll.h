/*
 *  solaris_ui — PianoRoll: a pattern's notes, edited — the content of a piano-roll window
 *  (R-ROLL-1…5, R-EDM-6).
 *
 *  A VIEW of the pattern (R-INST-3): every edit is the pattern's own grammar — `note add`,
 *  `note move` (one line per gesture: moved, resized or re-velocitied), `note delete`,
 *  `pattern quantize`, `set <pt>.length=` — so every clip playing it changes (R-CLIP-3) and a script
 *  can do all of it.
 *
 *  Notes mode: the keys on the left (C named with its octave, C4 = 60; a kit's keys named by its
 *  pads — the registry's note names), the beat grid (bars stronger), the notes in their strip's
 *  colour (brighter the louder), the velocity lane below, the pattern's end as a handle. A click
 *  adds a note of the last length at the snapped beat; a note dragged follows the pointer (its beat
 *  snapped) and lands as one `note move`; its right edge resizes it; a double-click or a right-click
 *  deletes it; a velocity stem dragged sets it. Steps mode: a row per pad (or per pitch in use),
 *  sixteen steps a bar; a click toggles a note. Snap 1/4, 1/8, 1/16 (default), 1/32 or off.
 *
 *  Nothing snaps on screen (§1): notes are keyed by (pitch, beat) and fade in and out; zoom and
 *  scroll ease; the mode switch cross-fades; the dragged note is the pointer's.
 */
#pragma once
#include "../Theme.h"
#include "AppModel.h"
#include "../../../interstellar/app/widgets/EasedScroll.h"
#include "../../../cosmo/widgets/ContextMenu.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris_ui
{
    class PianoRoll : public artboard::Segment
    {
    public:
        static constexpr double kToolH = 29.25;   // space::u(9)
        static constexpr double kKeysW = 58.5;    // space::u(18)
        static constexpr double kNoteH = 13.0;    // space::u(4): a pitch row
        static constexpr double kStepH = 22.75;   // space::u(7): a step row
        static constexpr double kVelH = 52.0;     // space::u(16): the velocity lane
        enum Mode { Notes = 0, Steps = 1 };

        explicit PianoRoll(std::string patternId);
        const std::string &pattern() const { return mPattern; }
        bool present() const { return mPresent; }
        std::string title() const;

        void bind(const solaris::AppModel &m);
        void layout();
        void advance(double nowMs) override;
        void setMode(Mode m) { mMode = m; }
        Mode mode() const { return mMode; }
        void setSnap(double beats) { mSnap = beats; }
        double snap() const { return mSnap; }

        // geometry (local), as DRAWN
        double beatToX(double b) const;
        double xToBeat(double x) const;
        double pitchToY(int p) const;   // a pitch row's top
        int yToPitch(double y) const;
        artboard::Rect gridRect() const;
        artboard::Rect velRect() const;
        artboard::Rect noteRect(int pitch, double at) const;
        double noteAlpha(int pitch, double at) const;   // LIVE
        artboard::Rect heldRect() const;                 // the note in a drag, where the POINTER has it (empty when none)
        std::string keyLabel(int pitch) const { return noteName(pitch); }
        artboard::Rect snapRect(int i) const;            // 0 1/4 · 1 1/8 · 2 1/16 · 3 1/32 · 4 Off
        artboard::Rect modeRect(int m) const;
        artboard::Rect quantizeRect() const;
        artboard::Rect endRect() const;                  // the pattern's end handle
        int stepRows() const { return (int)mStepPitches.size(); }
        int stepRowPitch(int r) const { return r >= 0 && r < stepRows() ? mStepPitches[(size_t)r] : -1; }
        artboard::Rect stepCell(int row, int step) const;
        double modeAmount() const { return mModeAmt.value(); } // 0 Notes … 1 Steps, LIVE
        /** A right-click at a window point: on a note it deletes it (true). */
        bool contextClick(artboard::Point world);

        std::function<bool(const std::string &line)> onCommand;
        std::function<void(std::vector<cosmo_v2::ContextMenu::Item> items, artboard::Point world)> onMenu;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        struct NoteLive
        {
            solaris::NoteModel n;
            artboard::AnimatedProperty alpha{0.0};
            double aLast = 0;
            bool placed = false, gone = false, instant = false;
        };
        enum class Drag { None, Move, Resize, Velocity, End };
        static std::string key(int pitch, double at);
        const NoteLive *noteAt(const artboard::Point &p, bool &edge) const;
        std::string noteName(int pitch) const;
        double snapped(double beat) const;
        void send(const std::string &line) { if (onCommand) onCommand(line); }
        void paintNotes(artboard::IRenderTarget &t, double a) const;
        void paintSteps(artboard::IRenderTarget &t, double a) const;

        std::string mPattern;
        bool mPresent = false;
        solaris::PatternModel mModel;
        std::vector<solaris::NoteNameModel> mNames; // the instrument's named keys
        int mColour = 0;
        std::string mStripName;
        std::vector<NoteLive> mNotes;               // keyed by (pitch, beat); ghosts fading out
        std::vector<int> mStepPitches;
        Mode mMode = Notes;
        double mSnap = 0.25, mLastLength = 0.25;
        artboard::AnimatedProperty mModeAmt{0.0}, mPpb{48.0}, mEnd{4.0};
        double mPpbTarget = 48.0, mNotesPpb = 48.0, mEndLast = -1, mNowMs = 0;
        bool mModeInit = false, mModeLast = false, mScrolledIn = false, mEver = false;
        interstellar_v1::EasedScroll mScrollX, mScrollY;
        cosmo_v2::HoverFade mHover;
        // a gesture in flight
        Drag mDrag = Drag::None;
        std::string mPressKey;
        solaris::NoteModel mPressNote;
        double mGrabBeat = 0, mGrabPitch = 0;
        solaris::NoteModel mLive;                   // the dragged note, as the pointer has it
        double mLiveEnd = 0;
    };
}
}
