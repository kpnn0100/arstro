/*
 *  Cosmo by arstro — ColorGradingPanel: 3-way colour grading + hue-range remap,
 *  built from existing controls. A ComboBox picks the region (shadows/mid/high);
 *  hue/sat/lum knobs edit it (saved per region). A balance knob, plus a remap
 *  section (enable toggle + source/range/target/strength knobs) for shifting an
 *  input hue window toward a target hue (e.g. red->orange, blue->teal).
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <array>
#include <functional>
#include <memory>

namespace arstro
{
namespace cosmo
{
    class ColorGradingPanel : public artboard::Segment
    {
    public:
        ColorGradingPanel(const artboard::Theme &theme, const artboard::Color &accent);

        std::function<void(int, double, double, double)> onGrade;     // region, hue,sat,lum
        std::function<void(double)> onBalance;                        // -100..+100
        std::function<void(bool, double, double, double, double)> onRemap;  // on, src,range,dst,strength01

        struct State
        {
            std::array<std::array<double, 3>, 3> grade{};  // [region][h,s,l]
            double balance = 0;
            bool remapOn = false;
            double remapSrc = 0, remapRange = 30, remapDst = 0, remapStrength = 0;  // strength 0..100
        };
        void setState(const State &s);

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        void loadRegion();
        void emitGrade();
        void emitRemap();

        artboard::Color mAccent;
        std::shared_ptr<artboard::ComboBox> mRegionSel;
        std::shared_ptr<artboard::Knob> mHue, mSat, mLum, mBalance;
        std::shared_ptr<artboard::ToggleSwitch> mRemap;
        std::shared_ptr<artboard::Knob> mSrc, mRange, mDst, mStrength;
        State mState;
        int mRegion = 0;
    };
}
}
