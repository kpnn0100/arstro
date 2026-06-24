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
        constexpr double kHeaderH = 34.0;
        constexpr double kPad = 12.0;
        constexpr double kLabelW = 84.0;
        constexpr double kSliderH = 14.0;
    }

    ParamPanel::ParamPanel(const std::string &title, const Theme &theme,
                           const Color &accent, const std::vector<Spec> &specs)
        : mTitle(title), mAccent(accent)
    {
        for (const auto &s : specs)
        {
            auto sl = std::make_shared<Slider>(theme.slider);
            sl->setRange(s.min, s.max);
            sl->setValue(s.def);
            sl->setDefault(s.def);
            sl->onChange = s.onChange;
            mLabels.push_back(s.label);
            mSliders.push_back(sl);
            addChild(sl);
        }
        width.set(300.0);
        height.set(kHeaderH + specs.size() * 30.0 + kPad);
    }

    void ParamPanel::layout(double w, double h)
    {
        width.set(w);
        height.set(h);
        const int rows = (int)mSliders.size();
        mRowY.assign(rows, 0.0);
        if (rows == 0)
            return;
        const double usable = h - kHeaderH - kPad;
        const double rowH = usable / rows;
        const double sliderX = kLabelW;
        const double sliderW = w - kLabelW - kPad;
        for (int i = 0; i < rows; ++i)
        {
            const double rowTop = kHeaderH + i * rowH;
            mRowY[i] = rowTop + rowH * 0.5 + 3.0;  // label baseline (centered)
            auto &sl = mSliders[i];
            sl->x.set(sliderX);
            sl->y.set(rowTop + (rowH - kSliderH) * 0.5);
            sl->width.set(sliderW > 20 ? sliderW : 20);
            sl->height.set(kSliderH);
        }
    }

    void ParamPanel::setValues(const std::vector<double> &values)
    {
        for (size_t i = 0; i < values.size() && i < mSliders.size(); ++i)
            mSliders[i]->setValue(values[i]);
    }

    void ParamPanel::onPaint(IRenderTarget &t) const
    {
        drawPanelChrome(t, width.value(), height.value(), mAccent, mTitle);
        t.setFill(palette::muted());
        for (size_t i = 0; i < mLabels.size() && i < mRowY.size(); ++i)
            t.drawText(mLabels[i], 10.0, mRowY[i], 10.0);
    }
}
}
