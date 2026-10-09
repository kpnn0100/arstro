/*
 *  solaris_ui — Timeline: the lanes (R-LANE-1, R-CLIP-1…4, R-TIME-4, R-UI-3).
 *
 *  Lanes are rows for ORGANISATION — a clip is drawn on its lane and coloured by the STRIP it sounds
 *  through, so a lane holding clips of three strips shows three colours (the FL-style freedom the
 *  song was designed around, `docs/discussion.md` §2). A clip with no lane (a file written by
 *  Interstellar) gets a row of its strip's own. A note clip draws its pattern's notes, repeated
 *  where the clip loops it, with the pattern's seams marked; a clip sharing its pattern with others
 *  says "linked ×N" (R-CLIP-3).
 *
 *  Interstellar's time idioms: the header column is the time origin (117 px); the ruler counts bars
 *  from 1; the playhead is `destructive` (a position). Zoom (Ctrl+wheel, anchored at the pointer)
 *  and scroll EASE; while playing the playhead follows the transport continuously, and a seek eases
 *  it (§1).
 *
 *  THE GRID FOLLOWS THE ZOOM (R-UI-10, R-TIME-5). Its levels are a bar, a beat, then 1/2, 1/4, 1/8,
 *  1/16 and 1/32 of a beat. How much of a level is drawn is a smooth function of its line spacing at
 *  the EASED zoom, recomputed every frame — none below kGridHidePx, all of it from kGridFullPx — so a
 *  Ctrl+wheel zoom fades levels in and out and never pops one. Bars are drawn strongest, beats next,
 *  the divisions faintest; every fourth bar is always drawn, so a far-out song keeps its phrases.
 *  THE SNAP STEP is the finest level whose lines are at least kSnapPx apart (half drawn: you snap to
 *  lines you can see), never coarser than a bar; at the deepest zoom nothing snaps — the exact tick.
 *  `snap()` is the ONE rounding every gesture on the lanes uses: a ruler click, a clip drag, a loop
 *  Shift-drag, an automation point, the browser's drop (`ProjectScreen::overLanes`). The ruler names
 *  the step in its corner, in the piano roll's note values (a beat = 1/4), cross-faded as it changes.
 *  The zoom steps ×1.25 on a lattice about 28 px a beat — 8 steps out (4.7) and 14 in (637, where
 *  1/32 of a beat is 20 px apart: fully drawn and THE step one notch before the deepest zoom).
 *
 *  The picture TRAVELS when the song changes shape (§1, "list insert/remove"): lanes are Interstellar's
 *  `AnimatedRows` keyed by lane id; a clip keeps its own eased beat, row and opacity keyed by clip
 *  id — it fades in when it arrives, fades out when it goes, and a `clip move` from a shell eases
 *  it there. A song opened places everything where it is: there is nowhere to travel from.
 *
 *  AUTOMATION (R-AUTO-6): under the lanes, a row per automation — its name, its curve over the beat
 *  grid with the points as handles (a log scale for Hz and ms). A click adds a point, a drag moves one
 *  (following the pointer exactly; one `auto point move` on release), a double-click deletes it, a
 *  right-click offers the shapes or deleting the automation. The rows are keyed in the same eased
 *  list as the lanes; a curve the model changes eases point by point (a point added or removed, or a
 *  shape changed, cross-fades the two curves). The curve is drawn by the ENGINE's mapping onto
 *  Interstellar's keyframes (`engine::curveKeys`, `anim::segment`), so what is drawn is what plays.
 *  BEZIER (R-AUTO-10, cosmo's CurvePanel gesture): Alt-drag a point pulls out symmetric handles; a
 *  handle dragged moves the opposite one as its mirror; Alt-drag a handle breaks the symmetry. The
 *  handle follows the pointer exactly while held; the release is ONE `auto point shape … --shape
 *  bezier --speed-in … --influence-in … --speed-out … --influence-out …`, and stays where it was let go.
 *  A double-click on the row's header or on its curve away from a point opens the automation's
 *  window (R-AUTO-11) — so a click on the curve waits out the double-click before it adds a point,
 *  drawn at once as a ghost that fades in.
 *
 *  Editing is command lines (`onCommand`): a click on the ruler → `transport seek <beat>` (on the grid
 *  you see); a clip dragged → `clip move <id> --at <beat> [--lane <ln>]` on release, on the same grid.
 *  Beats are printed to the tick (`beatText`). While dragging the clip follows the pointer exactly —
 *  direct manipulation, the one exemption — and the model the command returns puts it exactly there,
 *  so nothing jumps.
 */
#pragma once
#include "../Theme.h"
#include "AppModel.h"
#include "../../../interstellar/app/widgets/AnimatedRows.h"
#include "../../../interstellar/app/widgets/EasedScroll.h"
#include "../../../cosmo/widgets/ContextMenu.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris_ui
{
    class Timeline : public artboard::Segment
    {
    public:
        static constexpr double kHeaderW = 117.0;   // space::u(36): Interstellar's time origin
        static constexpr double kRulerH = 22.75;    // space::u(7)
        static constexpr double kRowH = 48.75;      // space::u(15)
        // the grid (R-UI-10): level 0 a bar, 1 a beat, 2…6 a half … a thirty-second of a beat
        static constexpr int kGridLevels = 7;
        static constexpr double kGridHidePx = 6.0;  // a level's lines closer than this: not drawn
        static constexpr double kGridFullPx = 16.0; // … this far apart or more: fully drawn (smoothstep between)
        static constexpr double kSnapPx = 11.0;     // the snap step: the finest level at least this far apart (half drawn)
        static constexpr double kBraceGrip = 6.5;   // space::u(2): the loop brace's end is taken within this (R-TIME-6)
        static constexpr double kZoomPpb = 28.0;    // the zoom a song opens at, px a beat
        static constexpr int kZoomOutSteps = 8, kZoomInSteps = 14; // Ctrl+wheel: ×1.25 a step — 4.7 … 637 px a beat

        Timeline();
        void bind(const solaris::AppModel &m);
        void layout();

        double beatToX(double beat) const;
        double xToBeat(double x) const;
        /** The row under `y` (local); −1 above the rows; rowCount() below the last. */
        int rowAt(double y) const;
        int rowCount() const { return (int)mRows.size(); }
        std::string rowLane(int i) const { return i >= 0 && i < (int)mRows.size() ? mRows[(size_t)i].lane : std::string(); }
        artboard::Rect rowRect(int i) const;
        artboard::Rect clipRect(const std::string &id) const;
        artboard::Rect rulerRect() const;
        double pxPerBeat() const { return mPpb.value(); }          // LIVE (eased)
        /** A clip's LIVE opacity (0 = not there): a test tells a fade from a cut. */
        double clipAlpha(const std::string &id) const;
        /** How far a clip's colour has come since its strip's last changed (1 = there). */
        double clipHueAmount(const std::string &id) const;
        double playheadBeat() const { return mPlayhead.value(); }  // LIVE (eased on a seek)

        // the grid follows the zoom (R-UI-10, R-TIME-5) — every value LIVE, from the EASED zoom
        double gridSpan(int level) const;    // beats between a level's lines (a bar is the song's meter)
        double gridAlpha(int level) const;   // how much of a level is drawn, 0 … 1
        double snapStep() const;             // THE step on the lanes, beats; 0 at the deepest zoom: nothing snaps
        double snap(double beat) const;      // the nearest line of it (≥ 0) — at the deepest zoom the exact tick
        bool atDeepestZoom() const;
        std::string snapLabel() const;       // the step as the ruler's corner names it ("1/8", "Bar", "Off")
        double snapLabelAmount() const { return mStepNames.empty() ? 0.0 : mStepNames.back().a.value(); } // LIVE: 1 = its name fully drawn
        /** A beat as a command spells it: rounded to the tick (960 PPQ), the fewest decimals that keep it. */
        static std::string beatText(double beat);

        /** The browser's drag: where it would land (a row; beat), and what it is. */
        void setDropHint(bool on, double beat = 0.0, int row = -1, const std::string &label = std::string());
        const std::string &selectedClip() const { return mSelected; }
        void selectClip(const std::string &id);

        std::function<void(const std::string &line)> onCommand;
        std::function<void(const std::string &clipId)> onSelect;
        std::function<void(const std::string &patternId)> onOpenPattern; // a note clip double-clicked: its piano roll (R-ROLL-1)
        std::function<void(const std::string &au)> onOpenAutomation;     // an automation row double-clicked: its window (R-AUTO-11)
        std::function<void(std::vector<cosmo_v2::ContextMenu::Item> items, artboard::Point world)> onMenu;
        std::function<void(const std::string &text)> onCopy;      // the host's clipboard: Copy ID (R-UI-11)
        std::function<double()> idsAmount;                        // View › Show IDs, eased by the screen (R-UI-11)

        // the loop region (R-EDM-7), as DRAWN
        artboard::Rect loopRect() const;                       // the brace on the ruler; empty when there is none
        double loopAmount() const { return mLoopAmt.value(); } // LIVE presence, 0 … 1
        double loopFromLive() const { return mLoopA.value(); } // LIVE, beats
        double loopToLive() const { return mLoopB.value(); }   // LIVE, beats
        // the ruler dragged (R-TIME-6): the playhead is the pointer's while held
        bool scrubbing() const { return mRuler == RulerDrag::Scrub; }
        bool braceDragging() const { return mRuler == RulerDrag::LoopBody || mRuler == RulerDrag::LoopFrom || mRuler == RulerDrag::LoopTo; }

        // automation rows (R-AUTO-6), local geometry as DRAWN
        int autoCount() const { return (int)mAutoIds.size(); }
        artboard::Rect autoRowRect(const std::string &au) const;
        artboard::Point autoPointAt(const std::string &au, int i) const;
        double autoValueAt(const std::string &au, double y) const;  // the value a y in its row means
        double autoEase(const std::string &au) const;               // 1 = its curve at rest (LIVE)
        /** The point being dragged, as DRAWN (local); (−1, −1) when none. */
        artboard::Point autoDragPoint() const;
        /** A bezier point's handle end as DRAWN (local; side 1 = in, 2 = out) — the pointer's while held; (−1, −1) when none. */
        artboard::Point autoHandleAt(const std::string &au, int i, int side) const;
        /** A click's point waiting out the double-click: its ghost's LIVE opacity (0 = none). */
        double autoPendingAmount() const { return mPendAmt.value(); }

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        struct Row
        {
            std::string key, lane, label; // key: the lane's id, "strip:<id>" for a strip's own row, "auto:<au>"
            int colour = -1;
            std::string automation;       // an automation row: its id
        };
        /** An automation's curve as drawn: eased from what was shown to what the model says. */
        struct AutoLive
        {
            solaris::AutomationModel model;
            std::vector<solaris::AutoPointModel> from, to, shownBefore;
            artboard::AnimatedProperty t{1.0};
            bool placed = false, changed = false;
        };
        struct ClipView
        {
            solaris::ClipModel c;
            int row = 0;
            int colour = 0;
            double patternLength = 0;
            std::vector<solaris::NoteModel> notes;
        };
        /** A clip's picture: eased to where the model puts it, in when it arrives, out when it goes. */
        struct ClipLive
        {
            ClipView v;
            double atTarget = 0, rowTarget = 0, atLast = 0, rowLast = 0, aLast = 0;
            artboard::AnimatedProperty at{0.0}, row{0.0}, alpha{0.0}, hueT{1.0};
            artboard::Color hueFrom, hueTo; // its strip's colour, cross-faded when the strip's changes
            int hueLast = 0;
            bool placed = false, gone = false;
        };
        /** A lane stripe's colour, cross-faded when it changes (an empty lane takes its first clip's). */
        struct Stripe
        {
            int want = -1, last = -1;
            artboard::Color from, to;     // from what was SHOWN, so a change mid-fade never jumps
            artboard::AnimatedProperty t{1.0};
            bool placed = false;
        };
        std::string clipAt(const artboard::Point &p) const;
        std::vector<solaris::AutoPointModel> shownPoints(const AutoLive &l) const;
        double valueToY(const AutoLive &l, double v, const artboard::Rect &row) const;
        double yToValue(const AutoLive &l, double y, const artboard::Rect &row) const;
        void paintAutomation(artboard::IRenderTarget &t) const;
        void paintCurve(artboard::IRenderTarget &t, const AutoLive &l, const std::vector<solaris::AutoPointModel> &pts, const artboard::Rect &row,
                        double alpha, bool handles) const;
        bool autoGesture(const artboard::Gesture &g, const artboard::Point &local);
        void advanceAuto(double nowMs);
        std::vector<solaris::AutoPointModel> drawnPoints(const AutoLive &l) const; // shown, with a held point or handle the pointer's
        int handleNear(const std::string &au, const artboard::Point &local, int &side) const;
        void dragHandle(const artboard::Point &local, bool alt);
        bool loopGesture(const artboard::Gesture &g, const artboard::Point &local);
        void loopSpan(double &a, double &b) const; // the brace's beats as drawn (the pointer's while dragged)
        std::string autoAt(double y) const;
        int pointNear(const std::string &au, const artboard::Point &local) const;
        const ClipLive *live(const std::string &id) const;
        double rowY(int i) const; // row i's LIVE top, before scrolling
        artboard::Rect clipBox(const ClipView &v, double at, double rowY) const;
        void paintClip(artboard::IRenderTarget &t, const ClipView &v, const artboard::Rect &r, double alpha, double ring, const artboard::Color &hue) const;
        void paintGrid(artboard::IRenderTarget &t, double b0, double b1) const; // the lanes' lines, every level faded by its room
        void paintRuler(artboard::IRenderTarget &t, double b0, double b1) const; // ticks, bar and beat labels, the step's name
        static std::string stepName(double step, int beatsPerBar);

        std::vector<Row> mRows;                       // the model's LANES, in order: hit-testing
        std::vector<std::string> mAutoIds;            // automation rows, after the lanes
        std::map<std::string, AutoLive> mAutos;
        std::string mAutoPress;                       // a point pressed / dragged
        int mAutoPoint = -1;
        bool mAutoDragging = false;
        double mAutoAt = 0, mAutoValue = 0;
        int mAutoHandle = 0;                          // 0 the point; 1 its in-handle, 2 its out-handle; 3 an Alt-pull (R-AUTO-10)
        solaris::AutoPointModel mAutoHeld;            // the pressed point's handles while one is held
        // a click on a row's curve waits out the double-click (which opens the window) before adding
        std::string mPendAu;
        double mPendAt = 0, mPendValue = 0, mPendMs = 0;
        bool mPendWaiting = false;
        artboard::AnimatedProperty mPendAmt{0.0};
        interstellar_v1::AnimatedRows<Row> mRowMotion; // what is drawn: eased, ghosts fading
        std::vector<ClipView> mClips;
        struct StripRef { std::string id, name, kind; };
        std::vector<StripRef> mStrips;                // what a clip may play through (its menu, R-MIX-14)
        std::vector<ClipLive> mLive;                  // by clip id, ghosts included
        std::map<std::string, Stripe> mStripes;       // by row key
        std::string mSong;                            // another song: everything placed afresh
        bool mBound = false, mEver = false, mEmptyInit = false;
        artboard::AnimatedProperty mEmptyAmt{0.0};
        double mEmptyLast = 0.0;
        int mBeatsPerBar = 4;
        double mLength = 0;                 // the song's end, beats
        double mPosition = 0;
        bool mPlaying = false, mInit = false;
        double mNowMs = 0.0;
        artboard::AnimatedProperty mPpb{kZoomPpb}, mPlayhead{0.0};
        double mPpbTarget = kZoomPpb;
        int mZoomStep = 0;                  // the lattice: mPpbTarget = kZoomPpb · 1.25^step
        // the step's name on the ruler: when the step changes (the zoom eased past a level) the new name
        // fades in and the ones before fade out from where they are — however fast the wheel turns
        struct StepName
        {
            std::string text;
            artboard::AnimatedProperty a{0.0};
            bool out = false;
        };
        std::vector<StepName> mStepNames;
        interstellar_v1::EasedScroll mScrollX, mScrollY;
        cosmo_v2::HoverFade mHover;
        // selection ring: fades out from the old clip while it fades in on the new one
        std::string mSelected, mPrevSelected;
        artboard::AnimatedProperty mSelIn{0.0}, mSelOut{0.0};
        // a clip drag in flight
        std::string mPressClip;
        double mGrab = 0.0;
        bool mDragging = false;
        double mDragBeat = 0.0;
        int mDragRow = 0;
        // the browser's drop hint
        bool mDropOn = false;
        double mDropBeat = 0.0;
        int mDropRow = -1;
        std::string mDropLabel;
        artboard::AnimatedProperty mDropAmt{0.0};
        // the loop: the model's, eased; a Shift-drag on the ruler is the pointer's
        double mLoopFrom = 0, mLoopTo = 0, mLoopALast = 0, mLoopBLast = 0;
        artboard::AnimatedProperty mLoopA{0.0}, mLoopB{0.0}, mLoopAmt{0.0};
        bool mLoopInit = false, mLoopOnLast = false, mLoopDragging = false;
        double mLoopGrab = 0, mLoopLive = 0;
        // the ruler dragged (R-TIME-6): a press waits to see whether it is a click (a seek — R-TIME-5 — or
        // clearing the loop) or a drag, and a drag decides once, by where it began, what it moves
        enum class RulerDrag { None, Pending, Scrub, LoopBody, LoopFrom, LoopTo };
        RulerDrag mRuler = RulerDrag::None, mRulerZone = RulerDrag::None;
        double mRulerDownBeat = 0.0;
        double mScrubBeat = 0.0, mScrubSent = -1.0;   // the pointer's beat on the grid; the last one sent
        double mBraceA0 = 0.0, mBraceB0 = 0.0;        // the brace's span when it was taken
        double mBraceA = 0.0, mBraceB = 0.0;          // … and where the pointer has it
        bool rulerGesture(const artboard::Gesture &g, const artboard::Point &local);
    };
}
}
