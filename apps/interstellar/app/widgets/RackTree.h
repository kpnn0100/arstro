/*
 *  interstellar_v1 — RackTree: the Grade tab's left column — the rack, Cosmo's group tree, hosted.
 *
 *  The rack is the colour authority (R-RACK-1): groups and sources in tree order, indented by
 *  `depth`, each a two-line row in cosmo's left-rail rhythm. Line one is the node (glyph, name,
 *  a version-OVERRIDE badge when this timeline has a #tlgrade on it — ui-brief §3); line two is
 *  what an address spells (the bind name, mono — R-RACK-6), the grade WEIGHT as a small bar you
 *  drag (R-RACK-4: a continuous bypass), and the USED-BY count (0 is not an error: reference
 *  stills are legitimate, so it reads muted, never red). A bypass toggle sits at the row's end.
 *
 *  States, each with its own words: a source still decoding shows cosmo's per-entry spinner; an
 *  OFFLINE one reads "offline — media missing" in destructive (R-RACK-7: missing, never a stall);
 *  a bypassed node dims; the empty rack says "no footage yet — add some" with the add affordance.
 *
 *  Every interaction is a command line: a click `rack select <bind>`, the toggle
 *  `set <bind>.bypass=0|1`, the weight bar `set <bind>.weight=<v>` (direct manipulation while
 *  dragged; afterwards the bar EASES to whatever the model says). The add button asks the host for
 *  files (the App turns the answer into `rack add <paths…>`).
 *
 *  Scrolls (EasedScroll: clamped both ends, one viewport, bar only when there is more, the wheel
 *  bubbles when there is nothing to scroll); rows travel on insert/remove (AnimatedRows).
 */
#pragma once
#include "../Theme.h"
#include "../AppHooks.h"
#include "AnimatedRows.h"
#include "EasedScroll.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <functional>
#include <map>
#include <string>

namespace arstro
{
namespace interstellar_v1
{
    class RackTree : public artboard::Segment
    {
    public:
        static constexpr double kRowH = 32.5;        // u(10): a two-line row
        static constexpr double kHeaderH = 29.25;    // u(9)

        RackTree();

        void bind(const interstellar::AppModel &m);

        // geometry a test aims at (local coords, live scroll included)
        artboard::Rect rowRect(int rackIndex) const;
        artboard::Rect bypassRect(int rackIndex) const;
        artboard::Rect weightRect(int rackIndex) const;
        artboard::Rect addRect() const;
        artboard::Rect viewport() const;
        const EasedScroll &scroll() const { return mScroll; }
        double shownWeight(int rackIndex) const;
        int selected() const { return mSelected; }

        std::function<void(const std::string &line)> onCommand;
        /** Right-click on row `rackIndex` at a WORLD point: the screen opens cosmo's context menu
         *  there (R-UI-9). */
        std::function<void(int rackIndex, artboard::Point local)> onContext;
        std::function<void()> onAddFootage;

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        int rowAt(const artboard::Point &local) const;   // rack index, -1 none
        double rowTopLocal(int rackIndex) const;
        void emit(const std::string &line) { if (onCommand) onCommand(line); }

        AnimatedRows<interstellar::RackNodeModel> mRows;
        std::vector<interstellar::RackNodeModel> mRack;
        int mSelected = -1;
        EasedScroll mScroll;
        cosmo_v2::HoverFade mHover, mSel;
        struct WeightAnim { artboard::AnimatedProperty v{1.0}; double last = -1.0, target = 1.0; };
        std::map<std::string, WeightAnim> mWeights;   // keyed by rackObj
        /** A row's STATE flags, eased: a bypass the user toggles, a source that finishes decoding,
         *  an override that appears on a version switch — each cross-fades its look (R1) instead of
         *  swapping glyphs and colours in one frame. */
        struct RowState
        {
            artboard::AnimatedProperty bypass{0.0}, pending{0.0}, failed{0.0}, ovr{0.0}, selected{0.0};
            bool want[5] = {false, false, false, false, false}, applied[5] = {false, false, false, false, false}, init = false;
        };
        std::map<std::string, RowState> mStates;   // keyed by rackObj
        const RowState *stateFor(const interstellar::RackNodeModel &n) const;
    public:
        /** The live eased bypass amount of a rack row (0..1) — what a test reads to tell a fade from a flip. */
        double bypassAmount(int rackIndex) const;
        /** Where row `rackIndex`'s OVR badge was last painted; empty when it shows none. */
        artboard::Rect overrideBadgeRect(int rackIndex) const
        {
            const auto it = mOvrRects.find(rackIndex);
            return it == mOvrRects.end() ? artboard::Rect{0, 0, 0, 0} : it->second;
        }
    private:
        int mDragRow = -1;
        /** The weight bar names itself on hover — "weight 75%" cross-fades over the clip count, so
         *  the control explains what it does (the user asked what it was). */
        cosmo_v2::HoverFade mWeightTip;
    public:
        /** The LIVE eased amount of row `rackIndex`'s weight caption (0..1). */
        double weightTipAmount(int rackIndex) const { return mWeightTip.amount(rackIndex); }
    private:
        double mPhaseMs = 0.0;
        /** Where each row's OVR badge was last painted (rack index → local rect). A click on it is
         *  "revert to base" — `revert <bind>` (ui-brief §3, one click away). Written by the const
         *  paint because only paint can measure the badge's text. */
        mutable std::map<int, artboard::Rect> mOvrRects;
        double mFps = 24.0;
    };
}
}
