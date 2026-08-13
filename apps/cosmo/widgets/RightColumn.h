/*
 *  cosmo_v2 by arstro — RightColumn: histogram + 7-tab strip + scrollable
 *  panel body + pinned action bar (App.tsx's right column, fixed 292px).
 *  Owns an EditSession reference directly so it can wire every control's
 *  callback straight to real develop-param mutations, the same way
 *  CosmoApp's constructor wires its panels inline.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include "../core/EditSession.h"
#include "HistogramWidget.h"
#include "EditStackTabs.h"
#include "ParamPanel.h"
#include "MaskPanel.h"
#include "MixerPanel.h"
#include "CurvePanel.h"
#include "GradePanel.h"
#include "XformPanel.h"
#include "StackPanel.h"
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
        // Wider than the Figma mock's 292px so the edit-stack tab labels sit
        // comfortably centred (task point 5: "make the edit stack wider").
        static constexpr double kWidth = 324.0;
        // Merged edit-stack tabs (Basic+Detail, Mask, Mixer+Curve, Grade, Xform).
        static constexpr int kTabBasicDetail = 0, kTabMask = 1, kTabColor = 2, kTabGrade = 3, kTabXform = 4;

        explicit RightColumn(cosmo::EditSession &session);

        std::shared_ptr<HistogramWidget> histogram() { return mHistogram; }
        std::shared_ptr<ActionBar> actionBar() { return mActionBar; }

        /** Push the current slot's params into whichever tab is showing (and
         *  the ones cached for a quick tab switch). Call after any selection
         *  or param change. */
        void syncToSlot();
        void scrollActivePanel(double delta);
        void layout();  // call after width/height changes

        /** R-BYPASS-4: while the edit target's filter is disabled the edit stack is
         *  covered by an eased dark scrim + a "FILTER DISABLED" pill. Pushed by
         *  App::syncControlsToSlot; the scrim opacity animates (R-G-1). */
        void setBypassed(bool on);
        bool bypassed() const { return mBypassed; }

        // ── on-photo mask overlay bridge (R-MASK) ──
        int activeTab() const;                          // edit-stack tab index
        bool maskTabActive() const;                     // true while the Mask tab is selected
        const MaskParams *selectedMaskParams() const;   // the mask being edited, or nullptr
        void writeSelectedMask(const MaskParams &m);    // overlay drag -> write geometry back + submit

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;  // card body (blends with active tab)
        /** The bypass scrim. Drawn in the overlay pass so it lands ON TOP of the tab
         *  strip + panel children (onPaint runs BEFORE children), clipped by hand to
         *  the edit-stack band so the histogram and the pinned action bar stay clear
         *  (R-BYPASS-4). A child's own overlay (an open ComboBox dropdown) still draws
         *  after ours and so stays readable. */
        void onOverlay(artboard::IRenderTarget &t) const override;
        void advance(double nowMs) override;

    private:
        /** The dimmed band: the tab strip + panel body, i.e. everything between the
         *  histogram and the action bar. */
        artboard::Rect editStackRect() const;

        // Push the effective (group-stacked) curves to the mixer/curve editors as their
        // faint green "final" reference — call after every curve/mixer edit so it tracks live.
        void refreshCurveReferences();

        cosmo::EditSession &mSession;
        std::shared_ptr<HistogramWidget> mHistogram;
        std::shared_ptr<EditStackTabs> mTabs;
        std::shared_ptr<ParamPanel> mBasicDetail;  // merged Basic + Detail (one scrollable list)
        std::shared_ptr<MaskPanel> mMask;
        std::shared_ptr<StackPanel> mColorTab;     // merged Mixer + Curve (scrollable stack)
        std::shared_ptr<MixerPanel> mMixer;
        std::shared_ptr<CurvePanel> mCurve;
        std::shared_ptr<GradePanel> mGrade;
        std::shared_ptr<XformPanel> mXform;
        std::shared_ptr<ActionBar> mActionBar;
        int mSelectedMask = -1;
        bool mBypassed = false;                     // R-BYPASS-4 target state
        artboard::AnimatedProperty mDim{0.0};       // eased scrim opacity 0..1 (R-G-1)
        double mLastMs = 0.0;                       // frame clock, so setBypassed() can start a tween
    };
}
}
