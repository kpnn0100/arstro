/*
 *  cosmo_v2 by arstro — RightColumn: histogram + 7-tab strip + scrollable
 *  panel body + pinned action bar (App.tsx's right column, fixed 292px).
 *  Owns an EditSession reference directly so it can wire every control's
 *  callback straight to real develop-param mutations, the same way
 *  CosmoApp's constructor wires its panels inline.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "../../cosmo_core/EditSession.h"
#include "HistogramWidget.h"
#include "ParamPanel.h"
#include "MaskPanel.h"
#include "MixerPanel.h"
#include "CurvePanel.h"
#include "GradePanel.h"
#include "XformPanel.h"
#include "ActionBar.h"
#include <functional>
#include <memory>

namespace arstro
{
namespace cosmo_v2
{
    class RightColumn : public artboard::Segment
    {
    public:
        static constexpr double kWidth = 292.0;

        explicit RightColumn(cosmo::EditSession &session);

        std::shared_ptr<HistogramWidget> histogram() { return mHistogram; }
        std::shared_ptr<ActionBar> actionBar() { return mActionBar; }

        /** Push the current slot's params into whichever tab is showing (and
         *  the ones cached for a quick tab switch). Call after any selection
         *  or param change. */
        void syncToSlot();
        void scrollActivePanel(double delta);
        void layout();  // call after width/height changes

    private:
        cosmo::EditSession &mSession;
        std::shared_ptr<HistogramWidget> mHistogram;
        std::shared_ptr<artboard::TabView> mTabs;
        std::shared_ptr<ParamPanel> mBasic;
        std::shared_ptr<ParamPanel> mDetail;
        std::shared_ptr<MaskPanel> mMask;
        std::shared_ptr<MixerPanel> mMixer;
        std::shared_ptr<CurvePanel> mCurve;
        std::shared_ptr<GradePanel> mGrade;
        std::shared_ptr<XformPanel> mXform;
        std::shared_ptr<ActionBar> mActionBar;
        int mSelectedMask = -1;
    };
}
}
