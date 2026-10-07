/*
 *  interstellar_v1 — KeyLane: where animation is authored (R-ANIM-3/4: in the timeline only, never in
 *  Grade; amended 2026-10-07: UNDER THE CLIP'S TRACK).
 *
 *  The selected clip's track opens (▸ on its header, or ◇ on the ruler) to show the clip's properties as
 *  rows directly under it, in three sections —
 *    CLIP     the clip's own opacity, geometry and speed (the clip's footage clock);
 *    GRADE    its source's colour keys (law 1: the curve is the source's, so every clip of that
 *             source shows it — and keys sit under the frames they key);
 *    EFFECTS  its source's effects' mix and parameters.
 *  Each row is a lane of the timeline: on the left its name and a diamond (empty: no curve · outline:
 *  animated · filled: a key at the playhead) that keys or un-keys at the playhead; on the right its
 *  keyframes as diamonds on the clip's span, under the frames they key — drag one to move it in time,
 *  double-click to add one, right-click for its menu. Choosing a row opens its curve INLINE, a
 *  KeyGraph band under the row (eased); choosing it again closes it; Ctrl/Shift-click draws more
 *  curves in the same band (R-ANIM-7). A clip's footage time and a source's time are the same seconds
 *  inside one clip (in … out), so one mapping serves all three sections.
 *
 *  A section header folds its section (presentation, never a command). The lane is a box the
 *  Timeline sizes and places between tracks: its rows scroll inside it, eased, measured from the same
 *  rectangle they are drawn in; the Timeline shows a window of the box (`setWindow`) when the tracks
 *  scroll it partly out of view.
 */
#pragma once
#include "../Theme.h"
#include "../AppHooks.h"
#include "EasedScroll.h"
#include "KeyGraph.h"
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
        static constexpr double kRowH = 19.5;        // u(6)
        static constexpr double kSectionH = 18.0;
        static constexpr double kTopPad = 4.875;
        static constexpr double kGraphH = 104.0;     // u(32): a curve opened under its row
        static constexpr double kKeyR = 3.5;         // a keyframe diamond on a row

        struct Row
        {
            std::string section;    // CLIP | GRADE | EFFECTS
            std::string label;
            std::string node;       // the clip, the source's #rackobj, or the effect
            std::string key;        // "opacity", "basic.exposure", "radius"
            std::string address;    // what `key add|remove|set` take
            int state = 0;          // 0 no curve · 1 animated · 2 a key at the playhead
            std::vector<double> keys;   // its keyframes' times on its clock (sorted)
            bool header = false;    // a section header row
            bool folded = false;    // (a header) its section is folded
        };

        KeyLane();
        void bind(const interstellar::AppModel &m);
        /** The column's width (the Timeline's track-header width) and where the selected clip spans,
         *  in this lane's x. Set every frame — the timeline zooms and scrolls. */
        void setColumnWidth(double w) { mColW = w; }
        void setClipSpan(double x0, double x1) { mSpanX0 = x0; mSpanX1 = x1; }
        void setPlayheadX(double x) { mPlayheadX = x; }
        /** The box the Timeline gives the lane (its height between the tracks) and how much of its top
         *  is scrolled out of view above the ruler — this segment is only the visible part. */
        void setWindow(double boxH, double cut) { mBoxH = boxH; mCut = cut; }
        /** Every row and open curve, unscrolled: the most the box would ever need. */
        double contentHeight() const { return contentH(); }
        void layout();
        void advance(double nowMs) override;

        bool hasClip() const { return mHasClip; }
        double now() const { return mNow; }
        int rowCount() const { return (int)mRows.size(); }
        const Row &row(int i) const { return mRows[(size_t)i]; }
        int rowOf(const std::string &address) const;
        int sectionRow(const std::string &section) const;
        artboard::Rect rowRect(int i) const;       // local, scrolled — where it is DRAWN (the row's own line)
        artboard::Rect diamondRect(int i) const;
        /** Row i's curve band (local, scrolled; zero-high when closed) and key k's diamond on the row. */
        artboard::Rect bandRect(int i) const;
        artboard::Point keyPoint(int i, int k) const;
        /** The LIVE eased openness of row i's curve band (0 … 1) — for a test. */
        double bandAmount(int i) const;
        const std::string &selected() const { return mSelected; }
        /** Choose `address` — its curve opens under it (add = keep the others shown too: the graph draws
         *  them together, R-ANIM-7). Choosing the front one again (not add) closes it. */
        void select(const std::string &address, bool add = false);
        void closeCurve();
        const std::vector<std::string> &shown() const { return mShownRows; }
        std::shared_ptr<KeyGraph> graph() { return mGraph; }
        double scrollTarget() const { return mScroll.target(); }
        double scrollValue() const { return mScroll.value(); }
        /** The LIVE eased fill of row i's diamond (0 empty … 1 filled) — for a test. */
        double diamondFill(int i) const;

        std::function<bool(const std::string &)> onCommand;
        std::function<void(const std::string &address, double t, artboard::Point world)> onKeyContext;
        std::function<void(double t, artboard::Point world)> onPlotContext;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;

    private:
        void rebuild(const interstellar::AppModel &m);
        void bindGraph(const interstellar::AppModel &m);
        double contentH() const;

        std::shared_ptr<KeyGraph> mGraph;
        std::vector<Row> mRows;
        std::set<std::string> mFolded;          // section names folded (intent: the user's, or the default)
        std::set<std::string> mFoldChosen;      // sections the user has folded or opened — the default leaves them be
        std::string mFoldClip;                  // the clip the defaults were taken for
        std::map<std::string, std::unique_ptr<artboard::AnimatedProperty>> mOpen;   // each section's eased open amount
        std::map<std::string, bool> mOpenApplied;
        double openOf(const std::string &section) const;
        struct DiamondAnim
        {
            artboard::AnimatedProperty fill{0.0}, line{0.0};
            double fillL = -1.0, lineL = -1.0;   // targets applied (-1: not placed yet)
        };
        std::map<std::string, DiamondAnim> mDia;   // by address: a key's diamond fills, eased
        double rowH(int i) const;
        double bandH(int i) const;
        double rowTop(int i) const;              // content y (unscrolled) of row i's line
        double xOfTime(double t) const;
        double timeOfX(double x) const;
        int keyAt(const artboard::Point &p, int &row) const;
        std::map<std::string, std::unique_ptr<artboard::AnimatedProperty>> mBand;   // each row's curve band, eased
        std::map<std::string, bool> mBandApplied;
        struct KeyDrag { std::string address; int row = -1, key = -1; double from = 0.0, t = 0.0; bool moved = false; };
        KeyDrag mKeyDrag;                        // a keyframe dragged along its row (direct manipulation)
        bool mRevealFront = false;               // keep the chosen row and its curve in view while the bands move
        double mPlayheadX = -1.0, mBoxH = 0.0, mCut = 0.0;
        std::string mSelected = "";             // the chosen (front) property's address — "" = no curve open
        std::vector<std::string> mShownRows;    // every property chosen, in click order (the graph draws them)
        bool mHasClip = false;
        interstellar::ClipModel mClip;
        double mNow = 0.0;
        double mColW = 117.0, mSpanX0 = 0.0, mSpanX1 = 0.0;
        const interstellar::AppModel *mModel = nullptr;
        EasedScroll mScroll;
        cosmo_v2::HoverFade mHover;
    };
}
}
