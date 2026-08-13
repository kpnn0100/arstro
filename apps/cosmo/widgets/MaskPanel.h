/*
 *  cosmo_v2 by arstro — MaskPanel: three "add mask" chips, a ComboBox to pick
 *  WHICH mask to edit, its invert/delete controls, a feather slider, then
 *  Basic-style tone/colour/presence sliders scoped to that mask's LocalAdjust.
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
#include "IconButton.h"
#include "SliderRow.h"
#include <functional>
#include <memory>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
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

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        void advance(double nowMs) override;  // eases the scroll toward its target (R-G-1)

    private:
        static constexpr double kPadX = 9.75;

        std::vector<std::shared_ptr<PillButton>> mAddChips;
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
        double mContentHeight = 0.0;

        // Cached header y's for onPaint: [0]="Add Mask", [1]="Tone", [2]="Colour",
        // [3]="Presence". The mask identity is now the ComboBox, not a header.
        double mHeaderY[4] = {0, 0, 0, 0};
        double mSelectRowY = 0.0;
    };
}
}
