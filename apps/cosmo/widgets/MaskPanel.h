/*
 *  cosmo_v2 by arstro — MaskPanel: five "add mask" chips, a ComboBox to pick
 *  WHICH mask to edit, its invert/delete controls, a feather slider, then
 *  Basic-style tone/colour/presence sliders scoped to that mask's LocalAdjust.
 *
 *  The fifth chip is Detect (R-AISEG-10): a mask with no geometry, whose region
 *  is found in the pixels. Selecting one opens the DetectBlock below — a subject
 *  picker, a line saying what THAT subject is found by, and Sensitivity.
 *
 *  The per-mask sliders are wired (via onFeatherChange / onAdjustChange) so
 *  editing a mask actually writes its `adjust` — without a non-identity adjust
 *  the engine's applyMaskStack skips the mask entirely (MaskStack: identity ->
 *  skipped), which is why masks previously had no visible effect. The mask
 *  picker mirrors cosmo's MaskPanel ComboBox.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include "../../../core/ImageProcessing/src/engine/EditParams.h"
#include "PillButton.h"
#include "SegmentedControl.h"
#include "IconButton.h"
#include "SliderRow.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    /** The Detect block: subject picker, caption, Sensitivity, under its own section header.
     *
     *  A Segment of its own, and CLIPPED, because R-G-1 has no exemption for "the selected mask
     *  changed kind" (R-AISEG-12). Its height animates 0 -> kHeight while its children keep their
     *  real positions, so the block slides out from under the Feather row and everything below
     *  moves in step. The alternative — flipping the rows visible and letting Tone jump down a
     *  frame later — is the snap this project keeps re-shipping under a different name.
     *
     *  It draws its own section header rather than letting MaskPanel paint one, because a header
     *  painted by the parent would not be clipped with the block and would hang in the air at
     *  every intermediate height. */
    class DetectBlock : public artboard::Segment
    {
    public:
        static constexpr double kHeaderH = 27.95;      // == kSectionHeaderHeight
        static constexpr double kPickerH = 22.75;
        static constexpr double kPickerMB = 4.875;
        static constexpr double kCaptionH = 13.0;
        DetectBlock() { clipToBounds = true; }
        std::string caption;   // what THIS subject is found by (R-AISEG-11)
    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
    };

    class MaskPanel : public artboard::Segment
    {
    public:
        MaskPanel();

        /** Refresh from the current image's masks + which one is focused
         *  (-1 = none). Does not fire callbacks. */
        void setMasks(const std::vector<MaskParams> &masks, int selected);
        void layout();
        void scrollBy(double delta);

        std::function<void(int type)> onAddMask;   // MaskParams::Type
        std::function<void(int index)> onSelectMask;  // choose which mask to edit
        std::function<void()> onToggleInvert;
        std::function<void()> onDeleteMask;
        std::function<void(double)> onFeatherChange;              // 0..1
        std::function<void(const LocalAdjust &)> onAdjustChange;  // selected mask's local adjust
        std::function<void(int subject)> onSubjectChange;         // SemanticSubject (R-AISEG-10)
        std::function<void(double)> onSensitivityChange;          // 0..1 (R-AISEG-5)

        /** Where "add mask" chip `i` is, in widget-local coords (0 Radial, 1 Linear, 2 Brush,
         *  3 Draw, 4 Detect). Public for the same reason `XformPanel::aspectChipRect` is: a test
         *  or a shot that aims at a chip must ask the widget where it drew it, or it ends up
         *  aiming where the widget no longer does. */
        artboard::Rect addChipRect(int i) const;

        /** How far the Detect block is open, 0..1 — the LIVE eased value, not the target. Public
         *  so a test can assert the two differ mid-tween, which is the only kind of assertion
         *  that can tell an eased implementation from a snapping one (R-G-1, R-AISEG-12). */
        double detectOpenAmount() const { return mDetect ? mDetectOpen.value() : 0.0; }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        void advance(double nowMs) override;  // eases the scroll toward its target (R-G-1)

    private:
        static constexpr double kPadX = 9.75;

        std::vector<std::shared_ptr<PillButton>> mAddChips;
        std::shared_ptr<DetectBlock> mDetect;
        std::shared_ptr<SegmentedControl> mSubject;
        std::shared_ptr<SliderRow> mSensitivity;
        std::shared_ptr<artboard::ComboBox> mSelect;   // which mask to edit
        std::shared_ptr<PillButton> mInvBtn;
        std::shared_ptr<IconButton> mTrashBtn;
        std::shared_ptr<SliderRow> mFeather;
        std::shared_ptr<SliderRow> mExposure, mHighlights, mShadows;
        std::shared_ptr<SliderRow> mTemperature, mSaturation;
        std::shared_ptr<SliderRow> mDehaze;

        std::vector<MaskParams> mMasks;
        int mSelected = -1;
        LocalAdjust mEditing;   // working copy of the selected mask's adjust
        artboard::AnimatedProperty mScroll{0.0};
        double mScrollTarget = 0.0, mScrollLastTarget = 0.0;
        artboard::AnimatedProperty mDetectOpen{0.0};
        double mDetectTarget = 0.0, mDetectLastTarget = 0.0;
        double mContentHeight = 0.0;

        // Cached header y's for onPaint: [0]="Add Mask", [1]="Tone", [2]="Colour",
        // [3]="Presence". The mask identity is now the ComboBox, not a header.
        double mHeaderY[4] = {0, 0, 0, 0};
        double mSelectRowY = 0.0;
    };
}
}
