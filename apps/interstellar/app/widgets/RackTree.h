/*
 *  interstellar_v1 — RackTree: the Grade tab's left column — the rack, Cosmo's group tree, hosted.
 *
 *  The rack is the colour authority (R-RACK-1): groups and sources in tree order, indented by
 *  `depth`, each a two-line row in cosmo's left-rail rhythm. Line one is the node (glyph, name,
 *  a version-OVERRIDE badge when this timeline has a #tlgrade on it — ui-brief §3); line two is
 *  what an address spells (the bind name, mono — R-RACK-6) and the USED-BY count (0 is not an
 *  error: reference stills are legitimate, so it reads muted, never red). A bypass toggle sits at
 *  the row's end. (The grade WEIGHT used to be a bar here; it is the Cosmo plugin's Mix in the
 *  IMAGE PROCESSING list now — R-RACK-4, amended 2026-10-05.)
 *
 *  States, each with its own words: a source still decoding shows cosmo's per-entry spinner; an
 *  OFFLINE one reads "offline — media missing" in destructive (R-RACK-7: missing, never a stall);
 *  a bypassed node dims; the empty rack says "no footage yet — add some" with the add affordance.
 *
 *  Every interaction is a command line: a click `rack select <bind>`, the toggle
 *  `set <bind>.bypass=0|1`. The add button asks the host for files (the App turns the answer into
 *  `rack add <paths…>`).
 *
 *  Groups COLLAPSE (R-UI-12, cosmo's "the items go inside the group"): a group's members show only
 *  while it is open. A chevron before the folder opens and closes it — the chevron turns and the
 *  rows below travel, through AnimatedRows (members fade in at their slots / out as ghosts). Groups
 *  start shut; when the Grade target moves to a node inside a shut group, its ancestors open so the
 *  selection is never hidden by the tree itself. Double-clicking a group row opens it AND drills the
 *  SOURCES strip into it (`onOpenGroup`). Open/shut is presentation state, never a command.
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
#include <set>
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
        /** A group row's open/shut chevron (local); empty for a source. */
        artboard::Rect chevronRect(int rackIndex) const;
        /** Shown in the tree: every ancestor group is open. */
        bool rowVisible(int rackIndex) const;
        bool isOpen(int rackIndex) const;
        /** The LIVE eased turn of a group's chevron (0 shut … 1 open). */
        double openAmount(int rackIndex) const;
        /** Open or shut a group by its #rackobj id — the strip's navigation keeps the tree in step. */
        void setOpen(const std::string &rackObj, bool open);
        artboard::Rect addRect() const;
        artboard::Rect viewport() const;
        const EasedScroll &scroll() const { return mScroll; }
        int selected() const { return mSelected; }

        std::function<void(const std::string &line)> onCommand;
        /** Right-click on row `rackIndex` at a WORLD point: the screen opens cosmo's context menu
         *  there (R-UI-9). */
        std::function<void(int rackIndex, artboard::Point local)> onContext;
        std::function<void()> onAddFootage;
        /** A group row was double-clicked: open it in the SOURCES strip (R-UI-12). */
        std::function<void(const std::string &rackObj)> onOpenGroup;

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        int rowAt(const artboard::Point &local) const;   // rack index, -1 none
        double rowTopLocal(int rackIndex) const;
        void emit(const std::string &line) { if (onCommand) onCommand(line); }

        struct RowItem { int rack = -1; interstellar::RackNodeModel n; };
        AnimatedRows<RowItem> mRows;                    // the VISIBLE rows, in tree order
        void syncRows();
        std::set<std::string> mOpen;                    // open groups, by #rackobj id (presentation)
        std::string mRevealedFor;                       // the Grade target whose ancestors were last opened
        bool mBound = false;
        std::vector<interstellar::RackNodeModel> mRack;
        int mSelected = -1;
        EasedScroll mScroll;
        cosmo_v2::HoverFade mHover, mSel;
        /** A row's STATE flags, eased: a bypass the user toggles, a source that finishes decoding,
         *  an override that appears on a version switch — each cross-fades its look (R1) instead of
         *  swapping glyphs and colours in one frame. */
        struct RowState
        {
            artboard::AnimatedProperty bypass{0.0}, pending{0.0}, failed{0.0}, ovr{0.0}, selected{0.0}, open{0.0}, shared{0.0};
            static constexpr int kN = 7;
            bool want[kN] = {}, applied[kN] = {}, init = false;
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
        double mPhaseMs = 0.0;
        /** Where each row's OVR badge was last painted (rack index → local rect). A click on it is
         *  "revert to base" — `revert <bind>` (ui-brief §3, one click away). Written by the const
         *  paint because only paint can measure the badge's text. */
        mutable std::map<int, artboard::Rect> mOvrRects;
        double mFps = 24.0;
    };
}
}
