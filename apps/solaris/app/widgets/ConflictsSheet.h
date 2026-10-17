/*
 *  solaris_ui — ConflictsSheet: what a merge asked (R-COLLAB-1; docs/discussion.md Round 19, part 3).
 *
 *  When the song's file changed under it and two people changed one node, the service keeps this window's and asks
 *  (`sync.conflicts`). The sheet — the Relink sheet's look (the scrim, the card, the 150/120 ms fade) — shows a row per
 *  node: what it is and how it was changed, what tells it apart here and in the file side by side (on its own line the
 *  fields that differ — "gain=-2.0" against "gain=-9.0" — under it the first line that differs), and Keep Mine / Take Theirs —
 *  ONE `project sync --take mine|theirs <node>` each; at its foot Keep All Mine / Take All Theirs (ONE line) and Done. It
 *  holds nothing: it draws the model, so an answer from a shell takes its row too. Opened by the App when a sync asks,
 *  and from File › Conflicts….
 *
 *  Nothing snaps: rows are keyed by node — an answered one fades out where it is and the rest slide up into its place;
 *  one asked while it is open fades in; the card grows and shrinks with them; the list clips and scrolls, eased.
 */
#pragma once
#include "../Theme.h"
#include "AppModel.h"
#include "../A11y.h"
#include "../../../interstellar/app/widgets/EasedScroll.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris_ui
{
    class ConflictsSheet : public artboard::Segment
    {
    public:
        static constexpr double kCardW = 680.0;
        static constexpr double kPad = 24.0;
        static constexpr double kRowH = 92.0;      // what · how, then two lines of each side
        static constexpr double kButtonW = 108.0, kButtonH = 26.0;
        static constexpr int kMaxRowsShown = 4;   // the card grows to this many, then the list scrolls

        ConflictsSheet();
        /** Open it on the song as it is: its rows placed, not faded in one by one. */
        void show(const solaris::AppModel &m);
        void close();
        /** What is asked now — rows arrive and leave, eased (call on every model change). */
        void bind(const solaris::AppModel &m);
        bool isOpen() const { return mOpen && !mClosing; }
        double appearAmount() const { return mAppear.value(); } // LIVE: 0 shut … 1 open

        /** A line to send — `project sync --take mine|theirs [<node>]`. */
        std::function<bool(const std::string &line)> onCommand;

        // what a test reaches (window coordinates, from the last paint) and the LIVE eased values it catches mid-way
        int rowCount() const { return (int)mRows.size(); }       // rows drawn, a leaving one too
        double rowAmount(const std::string &node) const;         // 0 … 1: its fade (−1: no such row)
        double rowY(const std::string &node) const;              // its eased place in the list (content px)
        artboard::Rect mineRect(const std::string &node) const;   // its Keep Mine
        /** R-ACCESS-1: each node asked — Keep Mine and Take Theirs, named by what it is, the sides as their value — the
         *  "all" buttons (enabled while something is asked), Done. */
        void accessibles(std::vector<A11yNode> &out);
        artboard::Rect theirsRect(const std::string &node) const; // its Take Theirs
        artboard::Rect allMineRect() const { return mAllMine; }
        artboard::Rect allTheirsRect() const { return mAllTheirs; }
        artboard::Rect doneRect() const { return mDone; }
        double cardHeight() const { return mCardH.value(); }     // LIVE: it grows and shrinks with its rows, eased
        double allAmount() const { return mAllOn.value(); }      // LIVE: 1 the "all" buttons take a click … 0.35 dimmed
        std::vector<std::string> shown(const std::string &node, bool mine) const; // what tells it apart, as drawn

        void advance(double nowMs) override;
        bool handleKey(const artboard::KeyEvent &e) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &) const override { return mOpen && !mClosing; }

    private:
        struct Row
        {
            std::string node, what, how;
            std::vector<std::string> mine, theirs;       // what tells the two sides apart (`tellApart`)
            artboard::AnimatedProperty a{0.0}, y{0.0};
            double aTarget = -1.0, yTarget = 0.0, yLast = -1.0;
            bool leaving = false, placed = false;
        };
        artboard::Rect cardRect() const;
        artboard::Rect listRect() const;
        bool send(const std::string &line) { return onCommand && onCommand(line); }
        std::vector<Row> mRows;
        std::vector<solaris::SyncConflictModel> mWanted;
        bool mWaiting = false;
        bool mPlaceAll = false;
        bool mOpen = false, mClosing = false, mShowWanted = false, mCloseWanted = false;
        artboard::AnimatedProperty mAppear{0.0};
        artboard::AnimatedProperty mCardH{0.0};
        double mCardHTarget = -1.0;
        artboard::AnimatedProperty mAllOn{1.0};      // Keep All Mine · Take All Theirs: dimmed, eased, with nothing asked
        double mAllOnLast = -1.0;
        interstellar_v1::EasedScroll mScroll;
        mutable artboard::Rect mAllMine, mAllTheirs, mDone;
        mutable std::vector<std::pair<std::string, artboard::Rect>> mMine, mTheirs;
        cosmo_v2::HoverFade mHover;
    };
}
}
