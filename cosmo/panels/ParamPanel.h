/*
 *  Cosmo by arstro — ParamPanel: a titled panel of labeled horizontal sliders in a
 *  column, grouped into labeled SECTIONS (e.g. Tone / Color / Effects) rather than
 *  one flat list. Each slider binds to a callback. Rows snap to a fixed height (they
 *  never stretch or overlap); when the section list is taller than the allotted
 *  h, the panel clips to it and scrollBy() (mouse wheel) reveals the rest.
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
        /** Scroll the section list by a mouse-wheel delta (positive = up). */
        void scrollBy(double wheelDelta);

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        struct Item { bool header; std::string label; int sliderIndex; double baseY; };
        void clampScroll();
        void reflow();  // reposition rows/sliders from width/mScrollY (layout() and scrollBy() both need this)

        std::string mTitle;
        artboard::Color mAccent;
        std::shared_ptr<artboard::Segment> mBody;  // clipped viewport below the title bar; owns the sliders
        std::vector<std::shared_ptr<artboard::Slider>> mSliders;
        std::vector<Item> mItems;  // section headers + slider rows, in display order
        bool mDrawChrome = true;
        double mScrollY = 0.0;    // content scroll offset (px)
        double mNaturalH = 0.0;   // full (unclipped) content height, from the last layout()
        double mTop = 0.0;        // last layout()'s row-start y (title bar height, or 6 embedded)
    };
}
}
