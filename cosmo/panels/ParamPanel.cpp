#include "ParamPanel.h"
#include "../Chrome.h"
#include "../CosmoTheme.h"

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    namespace
    {
        constexpr double kHeaderH = 34.0;   // panel title bar
        constexpr double kPad = 12.0;
        constexpr double kLabelW = 84.0;
        constexpr double kSliderH = 14.0;
        constexpr double kSectionH = 20.0;  // section sub-header row
    }

    ParamPanel::ParamPanel(const std::string &title, const Theme &theme,
                           const Color &accent, const std::vector<Section> &sections)
        : mTitle(title), mAccent(accent)
    {
        for (const auto &sec : sections)
        {
            mItems.push_back({true, sec.title, -1, 0.0});
            for (const auto &s : sec.specs)
            {
                auto sl = std::make_shared<Slider>(theme.slider);
                sl->setRange(s.min, s.max);
                sl->setValue(s.def);
                sl->setDefault(s.def);  // click jumps to the cursor (spring-smoothed); double-click resets
                sl->onChange = s.onChange;
                mItems.push_back({false, s.label, (int)mSliders.size(), 0.0});
                mSliders.push_back(sl);
                addChild(sl);
            }
        }
        width.set(300.0);
        height.set(360.0);
    }

    void ParamPanel::layout(double w, double h)
    {
        width.set(w);
        height.set(h);
        int nHeaders = 0, nSliders = 0;
        for (const auto &it : mItems) (it.header ? nHeaders : nSliders) += 1;
        const double usable = h - kHeaderH - kPad;
        const double sliderRowH = nSliders > 0 ? (usable - nHeaders * kSectionH) / nSliders : usable;
        double y = kHeaderH;
        for (auto &it : mItems)
        {
            if (it.header)
            {
                it.baseY = y + kSectionH * 0.5 + 3.0;
                y += kSectionH;
            }
            else
            {
                it.baseY = y + sliderRowH * 0.5 + 3.0;
                auto &sl = mSliders[it.sliderIndex];
                sl->x.set(kLabelW);
                sl->y.set(y + (sliderRowH - kSliderH) * 0.5);
                sl->width.set(w - kLabelW - kPad > 20 ? w - kLabelW - kPad : 20);
                sl->height.set(kSliderH);
                y += sliderRowH;
            }
        }
    }

    void ParamPanel::setValues(const std::vector<double> &values)
    {
        for (size_t i = 0; i < values.size() && i < mSliders.size(); ++i)
            mSliders[i]->setValue(values[i]);
    }

    void ParamPanel::onPaint(IRenderTarget &t) const
    {
        drawPanelChrome(t, width.value(), height.value(), mTitle);
        for (const auto &it : mItems)
        {
            if (it.header)
            {
                t.setFill(palette::ink());  // hierarchy by colour: header (ink) > label (muted)
                for (double ox : {0.0, 0.4})  // faux-bold section title
                    t.drawText(it.label, 10.0 + ox, it.baseY, 10.0);
            }
            else
            {
                t.setFill(palette::muted());
                t.drawText(it.label, 12.0, it.baseY, 10.0);
            }
        }
    }
}
}
