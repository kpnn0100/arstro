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
 *  Its play (R-ROLL-6): the ▶ at the toolbar's start loops the roll's clip — the one it was opened from,
 *  else the pattern's first in time — with the song sounding there (`transport play --clip`, ONE line);
 *  ■ (the same button, while that clip loops) stops it. The playhead is drawn where the clip is heard
 *  and kept in view, the grid following it a page at a time, eased. A pattern on no track has nothing to
 *  sound through: the ▶ is dimmed.
 *
 *  It sounds (R-ROLL-7): a note added, a note pressed, each pitch row a dragged note enters, a Steps cell
 *  turned on and a key pressed (or swept) are heard through the clip's track — `note preview <ch> <pitch>`,
 *  a line beside the edit's own, never in undo. The pressed key is lit while held.
 *
 *  A selection (R-ROLL-8, the view's): a click on a note selects it (Shift+click adds or removes it),
 *  Ctrl+drag on the grid draws a box, Ctrl+A takes every note, Escape lets go; the selection is lit. Done
 *  to it, each ONE line: dragged, the whole selection moves with the pointer and lands as `notes move`;
 *  ↑/↓ transpose it (Shift: an octave), ←/→ move it a snap step; Delete removes it (`notes delete`);
 *  Ctrl+D lays a copy right after it (its span, rounded up to the snap) and Ctrl+C / Ctrl+V copy it and
 *  paste it, here or in another roll, at the same beats (`notes add`) — the copy becomes the selection.
 *  The keys reach the roll the pointer last pressed in (`App`).
 *
 *  Nothing snaps on screen (§1): notes are keyed by (pitch, beat) and fade in and out; zoom and
 *  scroll ease; the mode switch cross-fades; the dragged note is the pointer's; the playhead fades in
 *  and out with the play.
 */
#pragma once
#include "../Cursor.h"
#include "../Theme.h"
#include "AppModel.h"
#include "../../../interstellar/app/widgets/EasedScroll.h"
#include "../../../cosmo/widgets/ContextMenu.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <functional>
#include <map>
#include <memory>
#include <set>
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
        /** The clip the roll was opened from (R-ROLL-6) — its ▶ loops it; '' = the pattern's first in time. */
        void setClip(const std::string &ac) { mClipWanted = ac; }
        const std::string &clip() const { return mClip; }  // as resolved ('' = the pattern is on no track)
        /** ▶ / ■ — the roll's play (also Space with the roll in front). False: nothing to play. */
        bool togglePlay();
        bool looping() const { return mLooping; }           // its clip is the one the transport loops
        artboard::Rect playRect() const;
        double playheadBeat() const { return mHeadBeat; }   // in the pattern (−1: not heard here)
        double playheadAlpha() const { return mHeadAlpha.value(); } // LIVE
        artboard::Rect keyRect(int pitch) const;            // a key on the left (R-ROLL-7: pressed, it sounds)
        // R-ROLL-8: the selection (the view's) and what is done to it
        bool selected(int pitch, double at) const;
        int selectionCount() const { return (int)mSel.size(); }
        double selectAmount(int pitch, double at) const;   // a note's selection light, LIVE
        artboard::Rect boxRect() const;                     // the box being drawn (empty when none)
        /** A keyboard action (R-SET-4: `roll.selectAll`, `roll.up` …) while this roll has the keys — the App looks the
            combo up in the model's keymap; true when the roll took it (a selection's actions need a selection). */
        bool action(const std::string &id);
        /** R-UI-14: the cursor a drag in hand keeps, and the one the grip under `local` wants (a note's end, the
         *  pattern's end handle: ↔; a velocity bar: ↕) — the zones a press takes. */
        Cursor dragCursor() const;
        Cursor cursorAt(const artboard::Point &local) const;
        /** The note clipboard every roll shares (Ctrl+C in one, Ctrl+V in any). */
        void setClipboard(std::shared_ptr<std::vector<solaris::NoteModel>> c) { mClipboard = std::move(c); }
        double keyLit(int pitch) const;                     // a pressed key's light, LIVE
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
        artboard::Rect lengthRect() const;               // R-ROLL-10: the length a click makes, and its menu
        double clickLength() const { return mLengthChoice > 0 ? mLengthChoice : mLastLength; }
        void setLengthChoice(double beats) { mLengthChoice = beats; } // 0 = the last length (the default)
        static std::string lengthName(double beats);     // 1/16, 1/4, 1/1 … or "1.75 beats"
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
            artboard::AnimatedProperty sel{0.0};    // R-ROLL-8: its selection light
            bool selLast = false;
            bool placed = false, gone = false, instant = false;
        };
        enum class Drag { None, Move, Resize, Velocity, End, Keys, MoveSel, Box };
        static std::string key(int pitch, double at);
        const NoteLive *noteAt(const artboard::Point &p, bool &edge) const;
        std::string noteName(int pitch) const;
        double snapped(double beat) const;
        void send(const std::string &line) { if (onCommand) onCommand(line); }
        void preview(int pitch, int vel, double length);    // R-ROLL-7: heard through the clip's track
        void track(const artboard::Point &local);           // a gesture in flight, where the pointer has it (D-6: also at release)
        bool sendOk(const std::string &line) { return onCommand ? onCommand(line) : false; }
        std::string tokens(const std::vector<solaris::NoteModel> &ns, double by = 0.0, int semi = 0, bool full = false) const;
        std::vector<solaris::NoteModel> selectedNotes() const;
        void moveSelection(double by, int semi);             // ONE `notes move`; the selection follows on success
        void addSelected(const std::vector<solaris::NoteModel> &ns); // ONE `notes add`; what landed is the selection
        void paintNotes(artboard::IRenderTarget &t, double a) const;
        void paintSteps(artboard::IRenderTarget &t, double a) const;

        std::string mPattern;
        std::string mClipWanted, mClip;             // R-ROLL-6
        std::string mClipTrack;                     // R-ROLL-7: the strip it sounds through ('' = none)
        std::set<std::string> mSel;                 // R-ROLL-8: selected notes, by key
        std::shared_ptr<std::vector<solaris::NoteModel>> mClipboard;
        artboard::Point mBoxA{0, 0}, mBoxB{0, 0};
        bool mBoxAdd = false;
        double mSelDBeat = 0;                       // a selection dragged: where the pointer has it
        artboard::Point mPressPt{0, 0};             // where the press was (D-6: a release elsewhere is a drag, however short)
        bool mSwallowClick = false;                 // … and the Click the recognizer still sends after it is not a click
        int mSelDSemi = 0;
        int mHeardPitch = -1, mKeyPitch = -1;       // the last pitch a drag sounded; the key held
        artboard::AnimatedProperty mKeyLight{0.0};
        int mKeyLitPitch = -1;
        bool mKeyLastHeld = false;
        double mClipAt = 0, mClipLen = 0;
        bool mPlaying = false, mLooping = false, mHeadLast = false;
        double mHeadBeat = -1;
        artboard::AnimatedProperty mHeadAlpha{0.0};
        bool mPresent = false;
        solaris::PatternModel mModel;
        std::vector<solaris::NoteNameModel> mNames; // the instrument's named keys
        int mColour = 0;
        std::string mStripName;
        std::vector<NoteLive> mNotes;               // keyed by (pitch, beat); ghosts fading out
        std::vector<int> mStepPitches;
        Mode mMode = Notes;
        double mSnap = 0.25, mLastLength = 0.25;
        double mLengthChoice = 0.0;                 // R-ROLL-10: 0 = the last length; else this, in beats
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
