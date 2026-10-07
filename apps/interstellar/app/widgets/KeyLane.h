/*
 *  interstellar_v1 — KeyLane: the timeline's ANIMATION section (R-ANIM-9, amended 2026-10-07: below
 *  all tracks, every marked property of the timeline).
 *
 *  A section docked under the tracks. Its header always shows: a fold (▾ ANIMATION and how many),
 *  and "＋ Animate…", which offers what the chosen clip (or Grade's source) does not animate yet. Under
 *  it, ONE LANE PER MARKED PROPERTY. A property is marked in Grade (its diamond), from Animate…, or by
 *  `key mark`. Each lane is named on two lines, the property, then its object and group ("Exposure",
 *  "s_day01 · Tone"), with its mode (OFFSET | FIXED, R-ANIM-10) beside them. A click on the mode
 *  switches it, unless it is a shape or a clip's own property.
 *
 *  A lane runs on the timeline's own time axis. A source's curve (colour, effects) is drawn under EVERY
 *  clip of that source, each key under the frames it keys; a clip's own curve under that clip only. A
 *  key is dragged in time, double-clicked into being, or right-clicked for its menu. The lane's diamond
 *  keys at the playhead, and only while the playhead is over a clip of that object (it is the one
 *  clock the key would be on). Choosing a lane opens its curve INLINE, a KeyGraph band under the lane
 *  (eased); choosing it again closes it; Ctrl/Shift-click draws more curves in the same band (R-ANIM-7).
 *
 *  Lanes ease in when a property is marked and out when it is unmarked, so nothing jumps. The lanes
 *  scroll inside the section's body, eased, measured from the same rectangle they are drawn in.
 */
#pragma once
#include "../Theme.h"
#include "../AppHooks.h"
#include "EasedScroll.h"
#include "KeyGraph.h"
#include "KeyState.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar_v1
{
    class KeyLane : public artboard::Segment
    {
    public:
        static constexpr double kHeadH = 22.75;       // u(7): the header, the height of the ruler
        static constexpr double kRowH = 29.25;        // u(9): a lane's two lines
        static constexpr double kHintH = 22.75;       // the line under the header while nothing is marked
        static constexpr double kPad = 4.875;         // u(1.5)
        static constexpr double kGraphH = 104.0;      // u(32): a curve opened under its lane
        static constexpr double kKeyR = 3.5;          // a keyframe diamond on a lane

        /** A clip of the lane's object: where its clock shows in the timeline. */
        struct Span { std::string clip; double at = 0, dur = 0, in = 0, out = 0, speed = 1; };
        struct Row
        {
            keys::Prop prop;              // address, names, order
            std::string mode = "offset";  // R-ANIM-10
            std::vector<double> keys;     // its keyframes on its clock (sorted)
            std::vector<Span> spans;      // every clip of its object in this timeline
            bool gone = false;            // unmarked: easing out
            std::string sortKey;
        };

        KeyLane();
        void bind(const interstellar::AppModel &m);
        /** The Timeline's time axis (live — it zooms and scrolls) and its playhead (timeline seconds). */
        void setTimeAxis(std::function<double(double)> toX, std::function<double(double)> toT) { mToX = std::move(toX); mToT = std::move(toT); }
        void setColumnWidth(double w) { mColW = w; }
        void setPlayhead(double t) { mPlayhead = t; }
        /** How open the section is (the Timeline eases it) — the header's chevron turns with it. */
        void setOpenAmount(double a) { mOpen = a; }
        /** Header, hint or lanes and open curves, unscrolled: the most the body would need. */
        double contentHeight() const { return contentH(); }
        void layout();
        void advance(double nowMs) override;

        int rowCount() const { return (int)mRows.size(); }
        const Row &row(int i) const { return mRows[(size_t)i]; }
        int rowOf(const std::string &address) const;
        /** How many properties are marked (the lanes' targets, not the eases). */
        int markedCount() const;
        /** The LIVE eased presence of lane i (0: unmarked, gone · 1: shown) — for a test. */
        double rowAmount(int i) const;
        artboard::Rect headerRect() const;          // local
        artboard::Rect addRect() const;             // "＋ Animate…"
        artboard::Rect foldRect() const;            // ▾ ANIMATION — folds the section
        artboard::Rect rowRect(int i) const;        // local, scrolled — where it is DRAWN (the lane's own line)
        artboard::Rect diamondRect(int i) const;
        artboard::Rect modeRect(int i) const;       // its OFFSET | FIXED badge
        /** Lane i's curve band (local, scrolled; zero-high when closed) and key k's diamond under span s. */
        artboard::Rect bandRect(int i) const;
        artboard::Point keyPoint(int i, int k, int s = -1) const;
        /** The LIVE eased openness of lane i's curve band (0 … 1) — for a test. */
        double bandAmount(int i) const;
        /** The playhead on lane i's clock (-1: not over a clip of its object). */
        double nowOf(int i) const;
        /** The front lane's now (-1 when there is none). */
        double now() const { return nowOf(rowOf(mSelected)); }
        const std::string &selected() const { return mSelected; }
        /** Choose `address` — its curve opens under it (add = keep the others shown too: the graph draws
         *  them together, R-ANIM-7). Choosing the front one again (not add) closes it. */
        void select(const std::string &address, bool add = false);
        void closeCurve();
        const std::vector<std::string> &shown() const { return mShownRows; }
        std::shared_ptr<KeyGraph> graph() { return mGraph; }
        double scrollTarget() const { return mScroll.target(); }
        double scrollValue() const { return mScroll.value(); }
        /** The LIVE eased fill of lane i's diamond (0 empty … 1 filled) — for a test. */
        double diamondFill(int i) const;

        std::function<bool(const std::string &)> onCommand;
        std::function<void(const std::string &address, double t, artboard::Point world)> onKeyContext;
        std::function<void(double t, artboard::Point world)> onPlotContext;
        /** "Animate…" pressed (its button, WORLD) — the host offers what is not marked yet. */
        std::function<void(artboard::Rect world)> onAddMenu;
        /** A lane's own menu (right-click on its name or its lane, not on a key), WORLD point. */
        std::function<void(const std::string &address, artboard::Point world)> onRowContext;
        /** The header's fold pressed. */
        std::function<void()> onToggle;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        /** Its top 3 px are the Timeline's: the grip that resizes the section straddles the edge (R-ANIM-8). */
        bool hitTestSelf(const artboard::Point &p) const override { return p.y >= 3.0 && localBounds().contains(p); }

    private:
        void rebuild(const interstellar::AppModel &m);
        void bindGraph(const interstellar::AppModel &m);
        double contentH() const;
        double rowH(int i) const;
        double bandH(int i) const;
        double rowTop(int i) const;              // content y (unscrolled) of lane i's line
        double hintAmount() const;               // the "nothing is marked" line, eased with the lanes
        double xOf(double t) const { return mToX ? mToX(t) : t; }
        double tOf(double xx) const { return mToT ? mToT(xx) : xx; }
        double keyX(const Span &s, double k) const { return xOf(s.at + (k - s.in) / std::max(1e-6, s.speed)); }
        int graphSpan(int i) const;              // the span the front curve is plotted over
        int keyAt(const artboard::Point &p, int &row, int &span) const;
        int spanAt(int i, double xx) const;

        std::shared_ptr<KeyGraph> mGraph;
        std::vector<Row> mRows;                  // sorted by object, then Grade's order; gone lanes keep their place
        struct DiamondAnim
        {
            artboard::AnimatedProperty fill{0.0}, line{0.0};
            double fillL = -1.0, lineL = -1.0;   // targets applied (-1: not placed yet)
        };
        std::map<std::string, DiamondAnim> mDia;   // by address: a key's diamond fills, eased
        std::map<std::string, std::unique_ptr<artboard::AnimatedProperty>> mPresent;   // each lane's presence, eased
        std::map<std::string, bool> mPresentApplied;
        std::map<std::string, std::unique_ptr<artboard::AnimatedProperty>> mBand;      // each lane's curve band, eased
        std::map<std::string, bool> mBandApplied;
        bool mEverBound = false;                 // the first model places the lanes (nowhere to travel from)
        struct KeyDrag { std::string address; int row = -1, key = -1, span = -1; double from = 0.0, t = 0.0; bool moved = false; };
        KeyDrag mKeyDrag;                        // a keyframe dragged along its lane (direct manipulation)
        bool mRevealFront = false;               // keep the chosen lane and its curve in view while the lanes move
        double mPlayhead = 0.0, mOpen = 1.0;
        std::string mSelected = "";             // the chosen (front) property's address — "" = no curve open
        std::string mPendingSelect;             // chosen before its lane exists (Animate… marks, then chooses)
        std::vector<std::string> mShownRows;    // every property chosen, in click order (the graph draws them)
        double mColW = 117.0;
        std::function<double(double)> mToX, mToT;
        const interstellar::AppModel *mModel = nullptr;
        EasedScroll mScroll;
        cosmo_v2::HoverFade mHover;             // 0 = Animate…, 1 = the fold, lane i = i + 2, its mode = 1000 + i
    };
}
}
