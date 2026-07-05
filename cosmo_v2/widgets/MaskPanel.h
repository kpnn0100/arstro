/*
 *  cosmo_v2 by arstro — MaskPanel (App.tsx MaskPanel): three "add mask" chips,
 *  the current mask's swatch/type/invert/delete row, a feather slider, then
 *  Basic-style tone/colour/presence sliders scoped to that mask's
 *  LocalAdjust. The Figma mock hardcodes one example mask with all no-op
 *  callbacks; this wires it to the current image's real EditParams::masks.
 *  No multi-mask picker is specified in the source, so adding a mask makes
 *  it the current one (matches cosmo's own mSelectedMask convention) and
 *  there is presently one mask "in focus" at a time, exactly as drawn.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "../../ImageProcessing/src/engine/EditParams.h"
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

        std::function<void(int type)> onAddMask;  // MaskParams::Type
        std::function<void()> onToggleInvert;
        std::function<void()> onDeleteMask;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        static constexpr double kPadX = 9.75;

        std::vector<std::shared_ptr<PillButton>> mAddChips;
        std::shared_ptr<PillButton> mInvBtn;
        std::shared_ptr<IconButton> mTrashBtn;
        std::shared_ptr<SliderRow> mFeather;
        std::shared_ptr<SliderRow> mExposure, mHighlights, mShadows;
        std::shared_ptr<SliderRow> mTemperature, mSaturation;
        std::shared_ptr<SliderRow> mDehaze;

        std::vector<MaskParams> mMasks;
        int mSelected = -1;
        double mScroll = 0.0;
        double mContentHeight = 0.0;

        // Cached header y positions from the last layout(), read by onPaint --
        // [0]="Add Mask", [1]="Mask N -- Type" (only when a mask is selected),
        // [2]="Tone", [3]="Colour", [4]="Presence".
        double mHeaderY[5] = {0, 0, 0, 0, 0};
        double mInfoRowY = 0.0;
    };
}
}
