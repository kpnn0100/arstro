/*
 *  interstellar_v1 — Timeline: the Cut tab's deck — tracks, clips, transitions, markers, playhead.
 *
 *  ONE time origin. Every time<->x mapping in this widget — the ruler's ticks, the lanes, each
 *  clip, the transitions, the markers, the playhead, a drag's snap — goes through `timeToX` /
 *  `xToTime`, which read the single token `shell::headerWidth()` (the track-header column) and the
 *  LIVE eased zoom and scroll. Two origins drift; one cannot.
 *
 *  Clips are drawn by PROVENANCE (R-VER, AppModel::Provenance), because "this clip is the base's"
 *  and "this clip is mine" ask for different things: Local plain; Inherited dimmed; Overridden
 *  with an ACCENT edge; Dangling (the base deleted what this version's delta pointed at) in
 *  destructive, hatched, with its reason. The playhead is `destructive` (a position — ui-brief §1).
 *
 *  Motion:
 *   * ZOOM eases (220 ms); `ppsLive()` is the live pixels-per-second, `ppsTarget()` where it is
 *     going. A zoom is anchored — the time under the pointer (or the playhead, for the buttons)
 *     stays put — so the scroll is RE-DERIVED every frame from the eased zoom, never set once.
 *   * Scroll (wheel) eases; a clip whose model position changes (an agent moved it, a version
 *     switched) eases to it; a new clip fades in, a deleted one fades out as a ghost.
 *   * A DRAG is direct manipulation: the clip keeps its GRAB OFFSET (it never teleports to put
 *     its start under the pointer — gotcha 8), snaps to clip edges, the playhead and markers
 *     within 8 px (an accent guide shows the edge it caught), may change to another track of the
 *     same kind, and on release dispatches the SNAPPED value:
 *        `clip move <clip> --at <t> [--track <trk>]`
 *     Edge drags trim: `clip trim <clip> --in <t>` / `--out <t>` (source time). A click selects
 *     (`clip select <clip>`); a click on the ruler or an empty lane moves the playhead
 *     (`playhead <t>`), and a ruler drag scrubs. If the service refuses a drop, the clip eases
 *     back to where the model says it is.
 *   * ALT changes what a drag is (R-UI-14), and the clip says so while it is held: Alt-drag the
 *     cut between two TOUCHING clips ROLLS it (`clip roll <left> --at <t>` — both clips move their
 *     shared edge live); Alt-drag a clip's body SLIPS its source range under a fixed position
 *     (`clip slip <clip> --by <dt>`, the new in-point shown on the clip).
 *   * A SOURCE dragged from the bin (the bin owns the gesture; EditScreen forwards it here as
 *     `dropHover` / `dropAt` / `dropCancel`) shows a ghost clip, eased in, snapped like a move, on
 *     the video lane under the pointer — or on a "new video track" when there is none; over an
 *     audio lane the ghost says it cannot land. Right-clicks report up (`onClipContext`,
 *     `onLaneContext`) for the screen's menu.
 *
 *  States: empty → "drag a source here", tracks greyed and the source bin lit (SourceBin); the
 *  tracks scroll vertically when they outgrow the deck, time scrolls horizontally, both clamped.
 */
#pragma once
#include "../Theme.h"
#include "../AppHooks.h"
#include "EasedScroll.h"
#include "KeyLane.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar_v1
{
    class Timeline : public artboard::Segment
    {
    public:
        static constexpr double kZoomStep = 1.5;
        static constexpr double kSnapPx = 8.0;
        static constexpr double kEdgePx = 6.0;      // an edge grab band for trimming
        static constexpr double kMaxPps = 480.0;

        Timeline();
        void bind(const interstellar::AppModel &m);

        // ── the one time origin ──
        double timeToX(double t) const;
        /** The LIVE eased "cached" amount of preview-cache segment `seg` (R-PLAY-1) — for a test. */
        double cacheAmount(size_t seg) const { return seg < mCacheSegs.size() ? mCacheSegs[seg].cached.value() : 0.0; }
        double xToTime(double x) const;
        double ppsLive() const { return mPps.value(); }
        double ppsTarget() const { return mPpsTarget; }
        double scrollLive() const { return mScroll.value(); }
        double displayedPlayhead() const { return mShownPlayhead.value(); }

        /** Zoom by `factor` keeping the time under local x `anchorX` where it is. Intent only. */
        void zoomBy(double factor, double anchorX);
        /** Workspace › Reset: zoom to fit with time 0 at the left edge. Intent only — it eases. */
        void resetView();

        // geometry a test aims at (local coords, live values)
        artboard::Rect rulerRect() const;
        artboard::Rect lanesRect() const;
        artboard::Rect laneRect(const std::string &trackId) const;
        artboard::Rect clipRect(const std::string &clipId) const;
        artboard::Rect zoomOutRect() const;
        artboard::Rect zoomInRect() const;
        bool dragging() const { return mDrag.kind != DragKind::None; }
        /** The snapped start (move) or edge (trim) the current drag would dispatch. */
        double dragValue() const { return mDrag.value; }
        double snapGuideAmount() const { return mGuideAmt.value(); }
        double emptyAmount() const { return mEmptyAmt.value(); }
        /** The live eased opacity of a clip's CURRENT look (0..1). */
        double clipAlpha(const std::string &clipId) const;

        /** Returns whether the line was accepted — a refused drop eases back. */
        std::function<bool(const std::string &line)> onCommand;
        /** R-AUD-7: a file's waveform envelope (the app hands the host's hook through). */
        std::function<bool(const std::string &media, std::vector<float> &peaks, double &perSecond)> peaksFor;
        /** The live eased opacity of `media`'s waveform (0 until its envelope lands, then easing to 1). */
        double waveformAmount(const std::string &media) const;
        /** Right-click on clip `id` / on an empty lane (`trackId` "" = below the tracks) at `t`, WORLD point. */
        std::function<void(const std::string &id, artboard::Point world)> onClipContext;
        std::function<void(const std::string &trackId, double t, artboard::Point world)> onLaneContext;
        /** R-ANIM-4: a right-click on a keyframe in the key lane — `address`, key time, WORLD point. */
        std::function<void(const std::string &address, double t, artboard::Point world)> onKeyContext;
        std::function<void(double t, artboard::Point world)> onKeyPlotContext;   // right-click on the lane's empty plot

        // ── the key lane (R-ANIM-3/4): where animation is authored — the selected clip's properties ──
        static constexpr double kKeyLaneMinH = 80.0;
        /** The lane's height now: the setting (R-ANIM-8), or the drag in flight, never more than leaves
         *  one track showing. */
        double keyLaneH() const;
        artboard::Rect keyLaneGrabRect() const;   // the band above the lane that resizes it
        bool resizingKeyLane() const { return mLaneDragging; }
        /** Show the key lane under the tracks while a clip is selected. Intent only; it eases. */
        void setKeysShown(bool on) { mKeysWanted = on; }
        bool keysShown() const { return mKeysWanted; }
        double keyLaneAmount() const { return mKeyLane.value(); }
        artboard::Rect keysToggleRect() const;
        artboard::Rect keyLaneRect() const;
        std::shared_ptr<KeyLane> keyLane() { return mKeyLaneW; }

        // ── a source dragged in from the bin (R-UI-14) ──
        /** The pointer, carrying source `label` (about `dur` s long), is at WORLD point `at`. */
        void dropHover(const std::string &label, double dur, artboard::Point at);
        /** Released at WORLD `at`: true with where it lands — `track` "" = a new video track. */
        bool dropAt(artboard::Point at, std::string &track, double &t);
        void dropCancel();
        double dropAmount() const { return mDropAmt.value(); }
        /** Where the ghost would land now: its time and track ("" = new; "!" = cannot, an audio lane). */
        double dropTime() const { return mDropT; }
        const std::string &dropTrack() const { return mDropTrack; }
        /** What the current modifier-drag is, in words ("roll", "slip +0.50 s"); empty otherwise. */
        std::string dragHint() const;

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        enum class DragKind { None, Move, TrimIn, TrimOut, Scrub, Roll, Slip };
        struct Drag
        {
            DragKind kind = DragKind::None;
            std::string clip;
            double grab = 0.0;          // anchor - pointer time: the offset that stops a teleport
            double value = 0.0;         // snapped start / edge
            double origAt = 0.0, origDur = 0.0, origIn = 0.0, origOut = 0.0, speed = 1.0;
            int origLane = 0, lane = 0;
            std::string other;          // roll: the clip on the right of the cut
            double otherAt = 0.0, otherDur = 0.0;
            double pressT = 0.0;        // slip: the time under the pointer at the press
            bool snapped = false;
            double snapTime = 0.0;
            long long lastFrame = -1;
        };
        struct ClipAnim
        {
            interstellar::ClipModel data;
            artboard::AnimatedProperty at{0.0}, dur{0.0}, lane{0.0}, alpha{0.0};
            double atT = 0, durT = 0, laneT = 0, alphaT = 1;
            double atL = 0, durL = 0, laneL = 0, alphaL = 1;
            bool placed = false, gone = false, fadeIn = false;
        };
        /** A track lane, eased like a clip: a version switch that adds or drops a track slides the
         *  lanes below it and fades the lane in or out, instead of re-stacking them in one frame. */
        struct TrackAnim
        {
            interstellar::TrackModel data;
            artboard::AnimatedProperty lane{0.0}, alpha{0.0};
            double laneT = 0, laneL = 0, alphaT = 1, alphaL = 1;
            bool placed = false, gone = false;
        };
        /** R-PLAY-1: one second of the preview cache bar along the ruler's top — cached (success),
         *  stale (a quiet grey: an edit changed it), building (the accent) — each amount eased, so a
         *  segment finishing fades in rather than flipping. Never shrunk: a segment past a shortened
         *  timeline eases out like any other. */
        struct CacheSegAnim
        {
            artboard::AnimatedProperty cached{0.0}, stale{0.0}, building{0.0};
            double cachedT = 0, staleT = 0, buildingT = 0, cachedL = 0, staleL = 0, buildingL = 0;
        };
        /** Clips are keyed by id AND look (provenance, offline): a restyle — a rebase turning a
         *  dangling clip back into an inherited one, a version switch — is then a cross-fade between
         *  the old look and the new, with the new one starting from the old one's live geometry. */
        static std::string styleKey(const interstellar::ClipModel &c);
        ClipAnim *liveAnim(const std::string &clipId);

        int laneOfTrack(const std::string &trackId) const;
        int laneAtY(double y) const;
        double laneTop(double laneLive) const;
        double contentSeconds() const { return mDuration + 2.0; }
        double fitPps() const;
        double maxScrollFor(double pps) const;
        double snap(double start, double dur, const std::string &self, bool twoEdges, bool &snapped, double &snapT) const;
        const ClipAnim *animFor(const std::string &id) const;
        std::string clipAt(const artboard::Point &p, int &edge) const;   // edge: -1 left, 0 body, +1 right
        bool emit(const std::string &line) { return onCommand ? onCommand(line) : false; }
        void resetToModel(const std::string &clipId);

        std::vector<interstellar::TrackModel> mTracks;          // display order (video top, then audio)
        std::vector<interstellar::TransitionModel> mTransitions;
        std::vector<interstellar::MarkerModel> mMarkers;
        std::map<std::string, interstellar::ClipModel> mModelClips;
        std::map<std::string, ClipAnim> mAnims;            // keyed by styleKey
        std::map<std::string, TrackAnim> mTrackAnims;      // keyed by track id
        bool mEmptyWanted = true, mEmptyApplied = true, mEmptyInit = false;
        artboard::AnimatedProperty mEmptyAmt{1.0};
        std::vector<CacheSegAnim> mCacheSegs;
        // the key lane (R-ANIM)
        std::shared_ptr<KeyLane> mKeyLaneW;
        bool mKeysWanted = false, mKeyLaneApplied = false;
        artboard::AnimatedProperty mLaneH{140.0};
        double mLaneHTarget = 140.0, mLaneHLast = -1.0;
        bool mLaneDragging = false;
        artboard::AnimatedProperty mKeyLane{0.0};
        bool mKeyClip = false;
        interstellar::ClipModel mKeyClipData;
        double mCacheSegSeconds = 1.0;
        std::string mSelectedClip;
        double mDuration = 0.0, mFps = 24.0, mPlayhead = 0.0;
        bool mPlaying = false;
        // R-AUD-7: the envelopes this view has, each fading in when it lands
        struct Wave { std::vector<float> peaks; double perSecond = 100.0; artboard::AnimatedProperty amount{0.0}; bool placed = false; };
        std::map<std::string, Wave> mWaves;
        unsigned mPeaksEpoch = ~0u;
        bool mEverBound = false, mPopulated = false;

        // zoom + horizontal scroll (scroll is re-derived from the eased zoom while anchored)
        artboard::AnimatedProperty mPps{0.0};
        double mPpsTarget = 0.0, mPpsLast = 0.0;
        bool mPpsInit = false, mAnchored = false;
        double mAnchorTime = 0.0, mAnchorX = 0.0;
        artboard::AnimatedProperty mScroll{0.0};
        double mScrollTarget = 0.0, mScrollLast = 0.0;
        EasedScroll mVScroll;

        artboard::AnimatedProperty mShownPlayhead{0.0};
        double mPlayheadLast = -1.0;

        Drag mDrag;
        bool mGuideWanted = false, mGuideApplied = false;
        artboard::AnimatedProperty mGuideAmt{0.0};
        double mGuideTime = 0.0;
        cosmo_v2::HoverFade mHover, mSel, mUi;   // clip hover, clip selection, zoom buttons
        // the drop ghost
        bool mDropWanted = false, mDropApplied = false;
        artboard::AnimatedProperty mDropAmt{0.0};
        std::string mDropLabel, mDropTrack;
        double mDropT = 0.0, mDropDur = 0.0, mDropLane = 0.0;
        bool mDropNewTrack = false, mDropBad = false;
        void dropLocate(artboard::Point local);
        std::vector<std::string> mClipOrder;     // stable small ids for the HoverFades
        int clipSlot(const std::string &id) const;
    };
}
}
