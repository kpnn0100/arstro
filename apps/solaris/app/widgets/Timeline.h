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
 *  The picture TRAVELS when the song changes shape (§1, "list insert/remove"): lanes are Interstellar's
 *  `AnimatedRows` keyed by lane id; a clip keeps its own eased beat, row and opacity keyed by clip
 *  id — it fades in when it arrives, fades out when it goes, and a `clip move` from a shell eases
 *  it there. A song opened places everything where it is: there is nowhere to travel from.
 *
 *  Editing is command lines (`onCommand`): a click on the ruler → `transport seek <beat>` (snapped to a sixteenth, as clips are); a clip
 *  dragged → `clip move <id> --at <beat> [--lane <ln>]` on release, snapped to a sixteenth. While
 *  dragging the clip follows the pointer exactly — direct manipulation, the one exemption — and the
 *  model the command returns puts it exactly there, so nothing jumps.
 */
#pragma once
#include "../Theme.h"
#include "AppModel.h"
#include "../../../interstellar/app/widgets/AnimatedRows.h"
#include "../../../interstellar/app/widgets/EasedScroll.h"
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
        double playheadBeat() const { return mPlayhead.value(); }  // LIVE (eased on a seek)

        /** The browser's drag: where it would land (a row; beat), and what it is. */
        void setDropHint(bool on, double beat = 0.0, int row = -1, const std::string &label = std::string());
        const std::string &selectedClip() const { return mSelected; }
        void selectClip(const std::string &id);

        std::function<void(const std::string &line)> onCommand;
        std::function<void(const std::string &clipId)> onSelect;

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        struct Row
        {
            std::string key, lane, label; // key: the lane's id, or "strip:<id>" for a strip's own row
            int colour = -1;
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
            artboard::AnimatedProperty at{0.0}, row{0.0}, alpha{0.0};
            bool placed = false, gone = false;
        };
        /** A lane stripe's colour, cross-faded when it changes (an empty lane takes its first clip's). */
        struct Stripe
        {
            int want = -1, from = -1, to = -1;
            artboard::AnimatedProperty t{1.0};
            bool placed = false;
        };
        std::string clipAt(const artboard::Point &p) const;
        const ClipLive *live(const std::string &id) const;
        double rowY(int i) const; // row i's LIVE top, before scrolling
        artboard::Rect clipBox(const ClipView &v, double at, double rowY) const;
        void paintClip(artboard::IRenderTarget &t, const ClipView &v, const artboard::Rect &r, double alpha, double ring) const;
        double snap(double beat) const { return std::max(0.0, std::round(beat * 4.0) / 4.0); }

        std::vector<Row> mRows;                       // the model's, in order: hit-testing
        interstellar_v1::AnimatedRows<Row> mRowMotion; // what is drawn: eased, ghosts fading
        std::vector<ClipView> mClips;
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
        artboard::AnimatedProperty mPpb{28.0}, mPlayhead{0.0};
        double mPpbTarget = 28.0;
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
    };
}
}
