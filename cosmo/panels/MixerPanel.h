/*
 *  Cosmo by arstro — MixerPanel: the HSL colour mixer as three cyclic hue-curve
 *  mappers on their own sub-tabs (Hue / Sat / Lum). Each maps the pixel's input hue
 *  (X) to an adjustment (Y), continuous across the wheel. Forwards each curve to the
 *  engine via setMixerCurve.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "../widgets/HueCurveEditor.h"
#include <array>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace arstro
{
namespace cosmo
{
    class MixerPanel : public artboard::Segment
    {
    public:
        MixerPanel(const artboard::Theme &theme, const artboard::Color &accent);

        // channel: 0 = hue, 1 = sat, 2 = lum.
        std::function<void(int, const std::vector<std::pair<float, float>> &)> onChange;

        void setCurves(const std::array<std::vector<std::pair<float, float>>, 3> &curves);
        void setHueHistogram(std::vector<float> bins)
        { for (auto &e : mEditors) if (e) e->setHueHistogram(bins); }
        void layout(double w, double h);

    private:
        std::shared_ptr<artboard::TabView> mTabs;
        std::shared_ptr<HueCurveEditor> mEditors[3];
        std::shared_ptr<artboard::Button> mReset;  // resets whichever channel sub-tab is active
    };
}
}
