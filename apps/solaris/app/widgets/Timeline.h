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
 *  MARKERS (R-ARR-1): the ruler's top band holds the song's markers — a flag with its name at its beat, a section
 *  (a marker with a length) a band to its end. A click on one → `transport seek <mk>`, a double-click → `transport
 *  loop <mk>` (its section), a drag moves it (the pointer's while held; ONE `marker move` on release), a right-click
 *  offers looping it, renaming it and deleting it; a right-click elsewhere on the ruler adds one there — or opens a
 *  bar of time there, takes that bar out, or takes the loop's span out of the whole song (`time insert|delete`,
 *  R-ARR-2). Keyed by id:
 *  each fades in and out, and a move made elsewhere eases there.
 *
 *  LANE GROUPS (R-ARR-3): an open group's lanes carry a bar down their headers' edge and, on the first, its name with
 *  a chevron; folded, the group is ONE row holding all its lanes' clips — its lanes fade out, the clips travel into it,
 *  the rows below ease up. The chevron (turning, eased) is `set <lg>.folded=on|off`; a clip dragged on a folded row
 *  takes the group's clips with it — ONE `clips move "<ac> …" --by <beats>`. A lane header's menu groups it (a new
 *  group, into one, out of its own, ungroup); a folded row's opens, renames or ungroups it.
 *
 *  TAKES (R-REC-2): a clip with takes says how many; its menu shows them — a row per take under its lane, keyed (rows
 *  below slide down, eased) — each lit where the comp plays it (the model's `plays`, cross-faded as the comp changes). A
 *  drag across a take is ONE `clip comp <ac> --take <k> --from --to` (the grid you see); a double-click, that take for
 *  the whole clip. Which clips show their takes is the view's.
 *
 *  Editing is command lines (`onCommand`): a click on the ruler → `transport seek <beat>` (on the grid
 *  you see); a clip dragged → `clip move <id> --at <beat> [--lane <ln>]` on release, on the same grid.
 *  Beats are printed to the tick (`beatText`). While dragging the clip follows the pointer exactly —
 *  direct manipulation, the one exemption — and the model the command returns puts it exactly there,
 *  so nothing jumps.
 */
#pragma once
#include "../Theme.h"
#include "../Cursor.h"
#include "AppModel.h"
#include "../../../interstellar/app/widgets/AnimatedRows.h"
#include "../../../interstellar/app/widgets/EasedScroll.h"
#include "../../../cosmo/widgets/ContextMenu.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
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
        static constexpr double kMarkH = 13.0;      // space::u(4): the markers' band, the ruler's top (R-ARR-1)
        static constexpr double kRulerH = 35.75;    // space::u(11): the markers' band, then the bars and the loop's brace
        static constexpr double kRowH = 48.75;      // space::u(15)
        // the grid (R-UI-10): level 0 a bar, 1 a beat, 2…6 a half … a thirty-second of a beat
        static constexpr int kGridLevels = 7;
        static constexpr double kGridHidePx = 6.0;  // a level's lines closer than this: not drawn
        static constexpr double kGridFullPx = 16.0; // … this far apart or more: fully drawn (smoothstep between)
        static constexpr double kSnapPx = 11.0;     // the snap step: the finest level at least this far apart (half drawn)
        static constexpr double kBraceGrip = 6.5;   // space::u(2): the loop brace's end is taken within this (R-TIME-6)
        static constexpr double kEdgeGrip = 6.5;    // space::u(2): a clip's right edge is taken within this (R-CLIP-7)
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
        std::string rowTrack(int i) const { return i >= 0 && i < (int)mRows.size() ? mRows[(size_t)i].strip : std::string(); } // whose track (R-LANE-3)
        std::string rowLabel(int i) const { return i >= 0 && i < (int)mRows.size() ? mRows[(size_t)i].label : std::string(); }
        /** R-LANE-3: how much of a lane's "instrument track" line is drawn (LIVE, 0 … 1), and what it says. */
        double trackAmount(const std::string &lane) const;
        /** R-MIDI-2: an instrument track's arm button (local; a click is `strip arm`), and how lit it is (LIVE). */
        artboard::Rect armRect(int row) const;
        double armAmount(const std::string &strip) const;
        std::string trackLabel(const std::string &lane) const;
        std::string rowLane(int i) const { return i >= 0 && i < (int)mRows.size() ? mRows[(size_t)i].lane : std::string(); }
        artboard::Rect rowRect(int i) const;
        artboard::Rect clipRect(const std::string &id) const;
        artboard::Rect rulerRect() const;
        double pxPerBeat() const { return mPpb.value(); }          // LIVE (eased)
        /** A clip's LIVE opacity (0 = not there): a test tells a fade from a cut. */
        double clipAlpha(const std::string &id) const;
        /** How far a clip's colour has come since its strip's last changed (1 = there). */
        double clipHueAmount(const std::string &id) const;
        /** A clip's LIVE drawn length, beats (eased; the pointer's while its end is dragged — R-CLIP-7). */
        double clipLengthLive(const std::string &id) const;
        bool resizing() const { return mResizing; }
        double playheadBeat() const { return mPlayhead.value(); }  // LIVE (eased on a seek)
        double cueBeat() const { return mCue; }                    // R-TIME-7: where Stop takes the time bar back
        double cueAmount() const { return mCueAmt.value(); }       // its mark on the ruler, LIVE (shown while playing)

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
        /** R-CLIP-9/10: the next clip the model brings is a copy just made — select it; `instant`: the pointer already
         *  showed it there (a Shift-drop), so it is placed at full strength, not faded in. Lapses at the next bind. */
        void expectCopy(bool instant);
        /** LIVE: how much the drag in hand is a COPY (Shift held) — the original at full strength, a "+" on the copy. */
        double copyAmount() const { return mCopyAmt.value(); }
        /** R-UI-14: the cursor a resize in hand keeps (Default when none), and the one a grip under `local` wants —
         *  the same zones a press takes (a clip's end, the loop brace's ends). */
        Cursor dragCursor() const;
        Cursor cursorAt(const artboard::Point &local) const;

        std::function<void(const std::string &line)> onCommand;
        std::function<void(const std::string &clipId)> onSelect;
        std::function<void(const std::string &patternId, const std::string &clipId)> onOpenPattern; // a note clip double-clicked: its piano roll, for that clip (R-ROLL-1, R-ROLL-6)
        std::function<void(const std::string &au)> onOpenAutomation;     // an automation row double-clicked: its window (R-AUTO-11)
        std::function<void(std::vector<cosmo_v2::ContextMenu::Item> items, artboard::Point world)> onMenu;
        std::function<void(const std::string &text)> onCopy;      // the host's clipboard: Copy ID (R-UI-11)
        std::function<double()> idsAmount;                        // View › Show IDs, eased by the screen (R-UI-11)

        // the loop region (R-EDM-7), as DRAWN
        artboard::Rect loopRect() const;                       // the brace on the ruler; empty when there is none
        double loopAmount() const { return mLoopAmt.value(); } // LIVE presence, 0 … 1
        double loopFromLive() const { return mLoopA.value(); } // LIVE, beats
        double loopToLive() const { return mLoopB.value(); }   // LIVE, beats
        // R-REC-1: the punch — a red line atop the bars where a recording keeps; eased in, out and between spans
        artboard::Rect punchRect() const;
        double punchAmount() const { return mPunchAmt.value(); } // LIVE presence
        // R-ARR-1: the markers on the ruler's band — local geometry as DRAWN, LIVE values
        // R-ARR-3: lane groups — the chevron and name (on a folded row, or an open group's first lane), how folded the
        // chevron shows (LIVE, 0 open … 1 folded), the row a folded group is (−1 none)
        artboard::Rect groupToggleRect(const std::string &lg) const;
        // R-REC-2: a clip's takes under it — shown or not (the view's), a take's row (local; empty when hidden), how lit a
        // take is at a beat (LIVE, 0 … 1: where the comp plays it), the range a drag on it has (the pointer's)
        void showTakes(const std::string &ac, bool on);
        bool takesShown(const std::string &ac) const { return mTakesOpen.count(ac) > 0; }
        artboard::Rect takeRowRect(const std::string &ac, int take) const;
        double takeLit(const std::string &ac, int take, double beat) const;
        // R-EDIT-1: an audio clip's fade handles (local, as DRAWN — empty when it is too narrow to take them), its gain
        // line's height (local; NaN when none), and the LIVE values: its own fade (beats), the fade heard, its gain (dB),
        // how reversed it shows (0 … 1). While a handle or the line is dragged, the pointer's.
        artboard::Rect fadeHandleRect(const std::string &ac, bool out) const;
        double gainLineY(const std::string &ac) const;
        double clipFadeLive(const std::string &ac, bool out) const;
        double clipHeardFadeLive(const std::string &ac, bool out) const;
        double clipGainLive(const std::string &ac) const;
        double clipReversedLive(const std::string &ac) const;
        /** R-WARP-1: how much of `text` the clip's tag shows (LIVE, 0 … 1) — its tempo when warped, its pitch when moved. */
        double clipTagAlpha(const std::string &ac, const std::string &text) const;
        static std::string clipTag(const solaris::ClipModel &c);
        /** R-EDIT-1: the clip's audio-editing lines for its menu — split at the time bar, reverse, consolidate, slice. */
        void clipEditItems(const solaris::ClipModel &c, std::vector<cosmo_v2::ContextMenu::Item> &items, const artboard::Point &world);
        double groupTurn(const std::string &lg) const;
        int groupRow(const std::string &lg) const;
        artboard::Rect markerRect(const std::string &mk) const;  // its flag and name; empty when none
        // R-TEMPO-1: a tempo change's flag on the ruler's band (local, as DRAWN), how present it is (LIVE), its beat
        // (LIVE — the pointer's while dragged), the tempo its flag shows (LIVE, eased)
        artboard::Rect tempoRect(const std::string &tp) const;
        double tempoAlpha(const std::string &tp) const;
        double tempoBeat(const std::string &tp) const;
        double tempoShown(const std::string &tp) const;
        double markerAlpha(const std::string &mk) const;         // LIVE presence, 0 … 1
        double markerBeat(const std::string &mk) const;          // LIVE beat — the pointer's while it is dragged
        double markerEnd(const std::string &mk) const;           // LIVE end of its section
        std::function<void(const std::string &current, artboard::Point world, std::function<void(const std::string &)> done)> onRename;
        // the ruler dragged (R-TIME-6): the playhead is the pointer's while held
        bool scrubbing() const { return mRuler == RulerDrag::Scrub; }
        bool braceDragging() const { return mRuler == RulerDrag::LoopBody || mRuler == RulerDrag::LoopFrom || mRuler == RulerDrag::LoopTo; }

        // automation rows (R-AUTO-6), local geometry as DRAWN
        int autoCount() const { return (int)mAutoIds.size(); }
        artboard::Rect autoRowRect(const std::string &au) const;
        artboard::Point autoPointAt(const std::string &au, int i) const;
        double autoValueAt(const std::string &au, double y) const;  // the value a y in its row means
        double autoEase(const std::string &au) const;               // 1 = its curve at rest (LIVE)
        /** R-AUTO-12: a looping automation's span — the brace along its row's top (local), its ends the grips; LIVE. */
        artboard::Rect autoSpanRect(const std::string &au) const;
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
            std::string strip, sub;       // R-LANE-3: an instrument's track — whose, and the instrument named under it
            std::string group, groupName; // R-ARR-3: the lane group it is in, or (folded) the group it IS
            std::string takeClip;         // R-REC-2: a take's row — whose clip, and which take (1-based)
            int take = 0;
            bool groupHead = false, folded = false; // the open group's first lane; a folded group's one row
            int groupLanes = 0;
        };
        /** A track's instrument line, fading in and out as the lane becomes a track and stops being one. */
        struct TrackLive
        {
            artboard::AnimatedProperty amt{0.0};
            bool want = false, placed = false;
            std::string sub;              // the last shown, so it fades out as it was
        };
        /** An automation's curve as drawn: eased from what was shown to what the model says. */
        struct AutoLive
        {
            solaris::AutomationModel model;
            std::vector<solaris::AutoPointModel> from, to, shownBefore;
            artboard::AnimatedProperty t{1.0};
            bool placed = false, changed = false;
            // R-AUTO-12: where its loop repeats, eased (beats): its start, its end (the song's end when it has no length)
            artboard::AnimatedProperty spanA{0.0}, spanB{0.0};
            double spanALast = -1.0, spanBLast = -1.0;
            bool spanPlaced = false;
        };
        struct ClipView
        {
            solaris::ClipModel c;
            int row = 0;
            int colour = 0;
            double patternLength = 0;
            std::vector<solaris::NoteModel> notes;
            double bandOff = 0.0, bandFrac = 1.0; // R-ARR-3: in a folded group's row, its lane's band of it
        };
        /** A clip's picture: eased to where the model puts it, in when it arrives, out when it goes. */
        struct ClipLive
        {
            ClipView v;
            double atTarget = 0, rowTarget = 0, atLast = 0, rowLast = 0, aLast = 0, lenTarget = 0, lenLast = 0;
            artboard::AnimatedProperty at{0.0}, row{0.0}, alpha{0.0}, hueT{1.0}, len{0.0}; // len: beats, eased (R-CLIP-7)
            artboard::AnimatedProperty off{0.0}, frac{1.0}; // R-ARR-3: its band of the row, eased as a group folds
            double offLast = 0.0, fracLast = 1.0;
            // R-EDIT-1: its own fades, the fades heard (an overlap's crossfade), its gain as an amplitude, reversed — eased
            artboard::AnimatedProperty fin{0.0}, fout{0.0}, hin{0.0}, hout{0.0}, amp{1.0}, rev{0.0};
            double finLast = 0.0, foutLast = 0.0, hinLast = 0.0, houtLast = 0.0, ampLast = 1.0, revLast = 0.0;
            std::string tagFrom, tagTo;              // R-WARP-1: "100 bpm · +7 st", cross-faded as it changes
            artboard::AnimatedProperty tagT{1.0};
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
        // R-AUTO-12: a looping automation's first cycle is drawn at its span's start — every beat↔x of its points goes
        // through this; the span shown (the pointer's end while a grip is held); a grip under a point (0 start, 1 end)
        double autoOffset(const AutoLive &l) const;
        void spanOf(const AutoLive &l, double &a, double &b) const;
        int spanGripAt(const std::string &au, const artboard::Point &local) const;
        void paintLoop(artboard::IRenderTarget &t, const AutoLive &l, const std::vector<solaris::AutoPointModel> &pts, const artboard::Rect &row,
                       double a) const;
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
        artboard::Rect clipBox(const ClipView &v, double at, double y, double len, double off = 0.0, double frac = 1.0) const;
        void paintClip(artboard::IRenderTarget &t, const ClipView &v, const artboard::Rect &r, double alpha, double ring, const artboard::Color &hue, double len) const;
        double liveLength(const ClipLive &l) const;   // what is drawn: the pointer's while its end is held, else eased
        bool newClipOn(const artboard::Point &local);   // R-CLIP-6: a MIDI clip on the track under the pointer
        void paintGrid(artboard::IRenderTarget &t, double b0, double b1) const; // the lanes' lines, every level faded by its room
        void paintRuler(artboard::IRenderTarget &t, double b0, double b1) const; // ticks, bar and beat labels, the step's name
        static std::string stepName(double step, int beatsPerBar);

        std::vector<Row> mRows;                       // the model's LANES, in order: hit-testing
        std::vector<std::string> mAutoIds;            // automation rows, after the lanes
        std::map<std::string, AutoLive> mAutos;
        std::string mSpanAu;                          // R-AUTO-12: a span's grip pressed …
        int mSpanEnd = -1;                            // … 0 its start, 1 its end
        double mSpanLive = 0.0;                       // … where the pointer has it (beats)
        bool mSpanDragging = false;
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
        std::map<std::string, TrackLive> mTracks;     // by lane id (R-LANE-3)
        std::string mArmed;                           // R-MIDI-2: the model's armed strip
        std::map<std::string, artboard::AnimatedProperty> mArm; // its button's light, by strip
        std::map<std::string, double> mArmLast;
        std::string mSong;                            // another song: everything placed afresh
        bool mBound = false, mEver = false, mEmptyInit = false;
        artboard::AnimatedProperty mEmptyAmt{0.0};
        double mEmptyLast = 0.0;
        int mBeatsPerBar = 4;
        double mLength = 0;                 // the song's end, beats
        double mPosition = 0, mCue = 0;
        artboard::AnimatedProperty mCueAmt{0.0};
        bool mCueShown = false;
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
        // a clip's right edge held (R-CLIP-7): its length is the pointer's, one `set <ac>.length` on release
        std::string mEdgeClip;
        bool mResizing = false;
        double mResizeLen = 0.0;
        // a clip drag in flight
        std::string mPressClip;
        double mGrab = 0.0;
        bool mDragging = false;
        bool mDropCopy = false;                        // R-CLIP-9: Shift held — the drop duplicates
        artboard::AnimatedProperty mCopyAmt{0.0};
        double mCopyLast = 0.0;
        std::set<std::string> mKnownClips;             // expectCopy: the clips there before the copy
        bool mAdoptNew = false, mAdoptInstant = false;
        std::string mInstantClip;                      // placed at full strength at its first frame
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
        // R-ARR-1: the markers — keyed by id, eased; a flag pressed, and dragged (the pointer's)
        struct MarkerLive
        {
            solaris::MarkerModel m;
            artboard::AnimatedProperty at{0.0}, end{0.0}, alpha{0.0};
            double atLast = -1.0, endLast = -1.0;
            bool placed = false, gone = false;
        };
        std::vector<MarkerLive> mMarkers;
        // R-TEMPO-1 (TimelineTempo.cpp): the tempo changes on the ruler's band, keyed — a flag at its beat, its tempo
        struct TempoLive
        {
            solaris::TempoModel t;
            artboard::AnimatedProperty at{0.0}, alpha{0.0}, bpm{120.0};
            double atLast = -1.0, bpmLast = -1.0;
            bool placed = false, gone = false;
        };
        std::vector<TempoLive> mTempos;
        double mSongBpm = 120.0;                       // the start's tempo (a glide into the first change starts there)
        std::string mTempoPress;
        bool mTempoDragging = false;
        double mTempoGrab = 0.0, mTempoLive = 0.0;
        mutable std::map<std::string, double> mTempoW; // a flag's width as last MEASURED (R5)
        const TempoLive *tempoLive(const std::string &tp) const;
        std::string tempoUnder(const artboard::Point &local) const;
        void bindTempos(const solaris::AppModel &m);
        void advanceTempos(double nowMs);
        void paintTempos(artboard::IRenderTarget &t) const;
        bool tempoGesture(const artboard::Gesture &g, const artboard::Point &local);
        double mPunchFrom = 0, mPunchTo = 0, mPunchALast = -1, mPunchBLast = -1; // R-REC-1: the model's punch
        artboard::AnimatedProperty mPunchA{0.0}, mPunchB{0.0}, mPunchAmt{0.0};
        bool mPunchOnLast = false, mPunchInit = false;
        // R-ARR-3: each group's chevron, eased; a folded group's clips dragged together
        // R-REC-2: the clips whose takes are shown; each take row's lit runs, cross-faded from the last; a drag on one
        std::set<std::string> mTakesOpen;
        const solaris::AppModel *mModelRef = nullptr; // the model last bound — the view's own changes re-bind from it
        struct TakeLit
        {
            std::vector<std::pair<double, double>> from, to;
            artboard::AnimatedProperty t{1.0};
            bool placed = false;
        };
        std::map<std::string, TakeLit> mTakeLit;  // by "<ac>#<k>"
        std::string mTakeClip;
        int mTakeIdx = 0;
        double mTakeA = 0, mTakeB = 0;
        bool mTakeDragging = false;
        void bindTakes(const solaris::AppModel &m);
        void advanceTakes(double nowMs);
        void paintTakes(artboard::IRenderTarget &t) const;
        bool takeGesture(const artboard::Gesture &g, const artboard::Point &local);
        // R-EDIT-1 (TimelineClipEdit.cpp): a fade handle or the gain line pressed (1 fade in, 2 fade out, 3 gain), the
        // clip, the pointer's value while dragged
        int mEditZone = 0;
        std::string mEditClip;
        double mEditValue = 0.0;
        bool mEditMoved = false;
        int clipEditZoneAt(const artboard::Point &p, std::string &id) const;
        bool clipEditable(const ClipLive &l, const artboard::Rect &r) const;
        bool clipEditGesture(const artboard::Gesture &g, const artboard::Point &local);
        static void placeClipEdit(ClipLive &l);
        static void advanceClipEdit(ClipLive &l, double nowMs);
        void paintClipEdit(artboard::IRenderTarget &t, const ClipView &v, const artboard::Rect &r, double a, const artboard::Color &hue) const;
        std::map<std::string, artboard::AnimatedProperty> mGroupTurn;
        std::map<std::string, double> mGroupTurnLast;
        std::string mDragGroup;
        std::vector<std::string> mDragGroupClips;
        double groupDelta() const;                     // beats the group's clips are carried while dragged
        mutable std::map<std::string, double> mMarkW;  // a flag's width as last MEASURED (R5) — what a press hits
        std::string mMarkPress;
        bool mMarkDragging = false;
        double mMarkGrab = 0.0, mMarkLive = 0.0;
        const MarkerLive *markerLive(const std::string &mk) const;
        std::string markerUnder(const artboard::Point &local) const;
        void bindMarkers(const solaris::AppModel &m);
        void advanceMarkers(double nowMs);
        void paintMarkers(artboard::IRenderTarget &t) const;
        bool markerGesture(const artboard::Gesture &g, const artboard::Point &local);
    };
}
}
