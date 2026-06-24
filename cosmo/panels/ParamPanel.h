/*
 *  Cosmo by arstro — ParamPanel: a titled panel of labeled horizontal sliders laid
 *  out in a single column, each bound to a callback. The Basic edit section is a
 *  ParamPanel (DRY). Responsive: layout(w,h) fills the given area, distributing the
 *  slider rows evenly so there is no unused vertical space.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo
{
    class ParamPanel : public artboard::Segment
    {
    public:
        struct Spec
        {
            std::string label;
            double min, max, def;
            std::function<void(double)> onChange;  // called live as the slider drags
        };

        ParamPanel(const std::string &title, const artboard::Theme &theme,
                   const artboard::Color &accent, const std::vector<Spec> &specs);

        /** Resize to (w,h) and re-flow the slider column to fill it. */
        void layout(double w, double h);

        /** Set slider positions without firing callbacks (used when switching slots). */
        void setValues(const std::vector<double> &values);

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        std::string mTitle;
        artboard::Color mAccent;
        std::vector<std::string> mLabels;
        std::vector<std::shared_ptr<artboard::Slider>> mSliders;
        std::vector<double> mRowY;  // baseline y for each row's label (set in layout)
    };
}
}
