/*
 *  Cosmo by arstro — ColorGradingPanel: 3-way colour grading + hue-range remap,
 *  built from sliders in a column. A ComboBox picks the region (shadows/mid/high);
 *  hue/sat/lum sliders edit it (saved per region). A balance slider, plus a remap
 *  section (animated text toggle + source/range/target/strength sliders) for
 *  shifting an input hue window toward a target hue. Responsive via layout(w,h).
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "../widgets/TextToggle.h"
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

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
            std::array<std::array<double, 3>, 3> grade{};
            double balance = 0;
            bool remapOn = false;
            double remapSrc = 0, remapRange = 30, remapDst = 0, remapStrength = 0;  // strength 0..100
        };
        void setState(const State &s);
        void layout(double w, double h);

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        void loadRegion();
        void emitGrade();
        void emitRemap();

        // ordered rows for responsive layout
        struct Row { std::shared_ptr<artboard::Segment> ctrl; std::string label; bool labeled; double baseY = 0; };

        artboard::Color mAccent;
        std::shared_ptr<artboard::ComboBox> mRegionSel;
        std::shared_ptr<artboard::Slider> mHue, mSat, mLum, mBalance, mSrc, mRange, mDst, mStrength;
        std::shared_ptr<TextToggle> mRemap;
        std::vector<Row> mRows;
        State mState;
        int mRegion = 0;
    };
}
}
