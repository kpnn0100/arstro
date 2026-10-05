/*
 *  interstellar_v1 — KeyLane: where animation is authored (R-ANIM-3/4, amended 2026-10-05: in the
 *  timeline only, never in Grade).
 *
 *  Under the Cut tab's tracks, for the SELECTED clip: on the left, every property a curve can drive,
 *  in three sections —
 *    CLIP     the clip's own opacity and geometry (the clip's footage clock);
 *    GRADE    its source's colour keys (law 1: the curve is the source's, so every clip of that
 *             source shows it — and keys sit under the frames they key);
 *    EFFECTS  its source's effects' mix and parameters;
 *  each row with a diamond (empty: no curve · outline: animated · filled: a key at the playhead) that
 *  keys or un-keys at the playhead. On the right, the chosen property's KeyGraph, its plot exactly
 *  the clip's span on the timeline: a clip's footage time and a source's time are the same seconds
 *  inside one clip (in … out), so one mapping serves all three sections.
 *
 *  A section header folds its section (presentation, never a command); the list scrolls, eased,
 *  measured from the same rectangle it is drawn in. The lane's geometry — its height and the clip's
 *  x span — comes from the Timeline every frame.
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

        struct Row
        {
            std::string section;    // CLIP | GRADE | EFFECTS
            std::string label;
            std::string node;       // the clip, the source's #rackobj, or the effect
            std::string key;        // "opacity", "basic.exposure", "radius"
            std::string address;    // what `key add|remove|set` take
            int state = 0;          // 0 no curve · 1 animated · 2 a key at the playhead
            bool header = false;    // a section header row
            bool folded = false;    // (a header) its section is folded
        };

        KeyLane();
        void bind(const interstellar::AppModel &m);
        /** The column's width (the Timeline's track-header width) and where the selected clip spans,
         *  in this lane's x. Set every frame — the timeline zooms and scrolls. */
        void setColumnWidth(double w) { mColW = w; }
        void setClipSpan(double x0, double x1) { mSpanX0 = x0; mSpanX1 = x1; }
        void layout();
        void advance(double nowMs) override;

        bool hasClip() const { return mHasClip; }
        double now() const { return mNow; }
        int rowCount() const { return (int)mRows.size(); }
        const Row &row(int i) const { return mRows[(size_t)i]; }
        int rowOf(const std::string &address) const;
        int sectionRow(const std::string &section) const;
        artboard::Rect rowRect(int i) const;       // local, scrolled — where it is DRAWN
        artboard::Rect diamondRect(int i) const;
        const std::string &selected() const { return mSelected; }
        void select(const std::string &address);
        std::shared_ptr<KeyGraph> graph() { return mGraph; }
        double scrollTarget() const { return mScroll.target(); }
        double scrollValue() const { return mScroll.value(); }
        /** The LIVE eased fill of row i's diamond (0 empty … 1 filled) — for a test. */
        double diamondFill(int i) const;

        std::function<bool(const std::string &)> onCommand;
        std::function<void(const std::string &address, double t, artboard::Point world)> onKeyContext;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;

    private:
        void rebuild(const interstellar::AppModel &m);
        void bindGraph(const interstellar::AppModel &m);
        double contentH() const;

        std::shared_ptr<KeyGraph> mGraph;
        std::vector<Row> mRows;
        std::set<std::string> mFolded;          // section names folded by the user (intent)
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
        std::string mSelected = "";             // the chosen property's address
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
