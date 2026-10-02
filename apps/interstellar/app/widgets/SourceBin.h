/*
 *  interstellar_v1 — SourceBin: the Cut tab's left column — the footage you can cut with.
 *
 *  The rack's SOURCES (groups are colour structure, not material, so they are not listed here),
 *  as two-line rows in the rack tree's rhythm: the source's name, then its bind name in mono and
 *  how many clips in the current version use it. Offline media reads "offline — media missing" in
 *  destructive, a decoding source carries cosmo's spinner cell.
 *
 *  When the timeline is EMPTY the bin is highlighted — an eased accent outline and wash — because
 *  the timeline's empty sentence ("drag a source here") points at it (ui-brief §4). Its own empty
 *  state is the rack's sentence, "no footage yet — add some", with the add affordance.
 *
 *  A DRAG carries a source to the timeline (R-UI-14): the bin owns the gesture and reports the
 *  pointer through `onDragSource` (move / drop / cancel); the screen forwards it to the timeline,
 *  which shows where it lands and answers with the track and time for `clip add`. The dragged row
 *  stays lit while it travels.
 *
 *  A click selects the source in the rack (`rack select <bind>`), the same act as everywhere else; a
 *  right-click opens the rack's own menu (Duplicate as Variant among it — R-RACK-5). A row whose
 *  file another source also uses says "shared" before its clip count.
 */
#pragma once
#include "../Theme.h"
#include "../AppHooks.h"
#include "AnimatedRows.h"
#include "EasedScroll.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <functional>
#include <string>

namespace arstro
{
namespace interstellar_v1
{
    class SourceBin : public artboard::Segment
    {
    public:
        static constexpr double kRowH = 32.5;
        static constexpr double kHeaderH = 29.25;

        SourceBin();
        void bind(const interstellar::AppModel &m);

        artboard::Rect rowRect(int i) const;   // i = index into the listed sources
        artboard::Rect viewport() const;
        const EasedScroll &scroll() const { return mScroll; }
        double highlightAmount() const { return mHighlight.value(); }
        int count() const { return (int)mSources.size(); }

        std::function<void(const std::string &line)> onCommand;
        std::function<void()> onAddFootage;
        /** Right-click on the row of rack node `rackIndex`, at a WORLD point (the rack's menu). */
        std::function<void(int rackIndex, artboard::Point world)> onContext;
        enum class DragPhase { Move, Drop, Cancel };
        /** A source row is being dragged out (R-UI-14), at a WORLD point. */
        std::function<void(const interstellar::RackNodeModel &source, artboard::Point world, DragPhase)> onDragSource;
        bool draggingSource() const { return mDragRow >= 0 && mDragMoved; }

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        void onOverlay(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        int rowAt(const artboard::Point &p) const;

        std::vector<interstellar::RackNodeModel> mSources;
        std::vector<int> mRackIndex;                     // listed source → index into the model's rack
        int mDragRow = -1;                               // the row a press landed on
        bool mDragMoved = false;
        AnimatedRows<interstellar::RackNodeModel> mRows;
        EasedScroll mScroll;
        cosmo_v2::HoverFade mHover, mSel;
        std::string mSelectedBind;
        bool mHighlightWanted = false, mHighlightApplied = false, mInit = false;
        artboard::AnimatedProperty mHighlight{0.0};
        double mPhaseMs = 0.0;
    };
}
}
