/*
 *  Cosmo by arstro — ParamPanel: a titled panel of labeled horizontal sliders in a
 *  column, grouped into labeled SECTIONS (e.g. Tone / Color / Effects) rather than
 *  one flat list. Each slider binds to a callback. Responsive: layout(w,h) fills the
 *  area, distributing slider rows evenly (section headers take a fixed slim row).
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
            std::function<void(double)> onChange;
            bool hasGradient = false;            // render the track as a colour ramp
            artboard::Color gradLeft, gradRight; // ramp endpoints (when hasGradient)
        };
        struct Section
        {
            std::string title;
            std::vector<Spec> specs;
        };

        ParamPanel(const std::string &title, const artboard::Theme &theme,
                   const artboard::Color &accent, const std::vector<Section> &sections);

        void layout(double w, double h);
        /** Set all slider positions (flat order across sections), no callbacks. */
        void setValues(const std::vector<double> &values);
        /** Draw the panel chrome (title bar + body). Off when embedded in another panel. */
        void setChrome(bool on) { mDrawChrome = on; }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        struct Item { bool header; std::string label; int sliderIndex; double baseY; };

        std::string mTitle;
        artboard::Color mAccent;
        std::vector<std::shared_ptr<artboard::Slider>> mSliders;
        std::vector<Item> mItems;  // section headers + slider rows, in display order
        bool mDrawChrome = true;
    };
}
}
