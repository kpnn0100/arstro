/*
 *  cosmo_v2 by arstro — GradePanel (App.tsx GradePanel): a Shadows/Midtones/
 *  Highlights region picker driving a hue/sat/lum triplet
 *  (EditParams::grade[3]), a balance slider (EditParams::balance), and a
 *  hue-remap sub-section (EditParams::remapEnable/remapSrc/remapRange/
 *  remapDst/remapStrength).
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "../../ImageProcessing/src/engine/EditParams.h"
#include "SegmentedControl.h"
#include "SliderRow.h"
#include <array>
#include <functional>
#include <memory>

namespace arstro
{
namespace cosmo_v2
{
    class GradePanel : public artboard::Segment
    {
    public:
        struct State
        {
            std::array<GradeWheel, 3> grade{};  // 0=Shadows, 1=Midtones, 2=Highlights
            float balance = 0;
            bool remapEnable = false;
            float remapSrc = 0, remapRange = 30, remapDst = 0, remapStrength = 0;  // remapStrength is 0..1
        };

        GradePanel();

        void setState(const State &s);
        void layout();
        void scrollBy(double delta);

        std::function<void(int region, double hue, double sat, double lum)> onRegionChange;
        std::function<void(double)> onBalanceChange;
        std::function<void(bool)> onRemapEnableChange;
        /** strength in UI units (0..100); caller divides by 100 for the engine. */
        std::function<void(double src, double range, double dst, double strength)> onRemapChange;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        void advance(double nowMs) override;  // eases the scroll toward its target (R-G-1)

    private:
        void pushRegionValues();

        std::shared_ptr<SegmentedControl> mRegionPicker;
        std::shared_ptr<SliderRow> mHue, mSat, mLum;
        std::shared_ptr<SliderRow> mBalance;
        std::shared_ptr<artboard::ToggleSwitch> mRemapToggle;
        std::shared_ptr<SliderRow> mSource, mRange, mTarget, mStrength;

        State mState;
        int mRegion = 0;
        artboard::AnimatedProperty mScroll{0.0};
        double mScrollTarget = 0.0, mScrollLastTarget = 0.0, mContentHeight = 0.0;
        double mRegionHeaderY = 0.0, mBalanceHeaderY = 0.0, mRemapHeaderY = 0.0, mEnableRowY = 0.0;
    };
}
}
