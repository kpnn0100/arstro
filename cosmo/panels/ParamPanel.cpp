#include "ParamPanel.h"
#include "../Chrome.h"

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    namespace
    {
        constexpr double kHeaderH = 34.0;
        constexpr double kPad = 14.0;
        constexpr double kCellW = 70.0;
        constexpr double kCellH = 66.0;
        constexpr double kKnob = 44.0;
    }

    ParamPanel::ParamPanel(const std::string &title, const Theme &theme,
                           const Color &accent, const std::vector<Spec> &specs, int columns)
        : mTitle(title), mAccent(accent)
    {
        if (columns < 1) columns = 1;
        const int rows = ((int)specs.size() + columns - 1) / columns;

        for (size_t i = 0; i < specs.size(); ++i)
        {
            const Spec &s = specs[i];
            auto k = std::make_shared<Knob>(theme.knob);
            k->label = s.label;
            k->setRange(s.min, s.max);
            k->setValue(s.def);
            k->setDefault(s.def);
            k->onChange = s.onChange;
            k->width.set(kKnob);
            k->height.set(kKnob);
            const int col = (int)i % columns;
            const int row = (int)i / columns;
            k->x.set(kPad + col * kCellW + (kCellW - kKnob) * 0.5);
            k->y.set(kHeaderH + 6.0 + row * kCellH);
            mKnobs.push_back(k);
            addChild(k);
        }

        width.set(kPad * 2 + columns * kCellW);
        height.set(kHeaderH + 6.0 + rows * kCellH + 6.0);
    }

    void ParamPanel::setValues(const std::vector<double> &values)
    {
        for (size_t i = 0; i < values.size() && i < mKnobs.size(); ++i)
            mKnobs[i]->setValue(values[i]);
    }

    double ParamPanel::valueOf(int i) const
    {
        return (i >= 0 && i < (int)mKnobs.size()) ? mKnobs[i]->value() : 0.0;
    }

    void ParamPanel::onPaint(IRenderTarget &t) const
    {
        drawPanelChrome(t, width.value(), height.value(), mAccent, mTitle);
    }
}
}
