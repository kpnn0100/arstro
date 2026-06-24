#include "MixerPanel.h"

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    MixerPanel::MixerPanel(const Theme &theme, const Color &accent)
    {
        mTabs = std::make_shared<TabView>(theme.tab);
        const char *names[3] = {"Hue", "Sat", "Lum"};
        for (int c = 0; c < 3; ++c)
        {
            mEditors[c] = std::make_shared<HueCurveEditor>(accent);
            const int ch = c;
            mEditors[c]->onChange = [this, ch](const std::vector<std::pair<float, float>> &pts) {
                if (onChange) onChange(ch, pts);
            };
            mTabs->addPage(names[c], mEditors[c]);
        }
        addChild(mTabs);
        width.set(300.0);
        height.set(220.0);
    }

    void MixerPanel::setCurves(const std::array<std::vector<std::pair<float, float>>, 3> &curves)
    {
        for (int c = 0; c < 3; ++c)
            mEditors[c]->setPoints(curves[c]);
    }

    void MixerPanel::layout(double w, double h)
    {
        width.set(w);
        height.set(h);
        mTabs->x.set(0.0);
        mTabs->y.set(0.0);
        mTabs->width.set(w);
        mTabs->height.set(h);
        const double cw = w;
        const double ch = h - mTabs->tabHeight - 6.0;
        for (int c = 0; c < 3; ++c)
        {
            mEditors[c]->width.set(cw);
            mEditors[c]->height.set(ch > 40 ? ch : 40);
        }
    }
}
}
