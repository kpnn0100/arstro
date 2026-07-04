#include "MixerPanel.h"
#include "../CosmoTheme.h"

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
        mEditors[0]->setMappedHue(true);  // Hue tab: colour the line by the output hue
        addChild(mTabs);

        ButtonStyle flat = theme.button;  // flat "chip" style, matching MaskPanel's add buttons
        flat.idle = {Paint::filled(palette::surface()), radius::control()};
        flat.pressed = {Paint::filled(palette::line()), radius::control()};
        flat.label.color = palette::ink();
        mReset = std::make_shared<Button>("Reset", flat);
        mReset->onClick = [this] {
            const int c = mTabs->selectedIndex();
            if (c >= 0 && c < 3) mEditors[c]->reset();
        };
        addChild(mReset);

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
        constexpr double kHeaderH = 26.0;  // room for the Reset button, above the sub-tabs
        mReset->x.set(w - 48.0); mReset->y.set(2.0);
        mReset->width.set(40.0); mReset->height.set(20.0);
        mTabs->x.set(0.0);
        mTabs->y.set(kHeaderH);
        mTabs->width.set(w);
        mTabs->height.set(h - kHeaderH);
        const double cw = w;
        const double ch = (h - kHeaderH) - mTabs->tabHeight - 6.0;
        for (int c = 0; c < 3; ++c)
        {
            mEditors[c]->width.set(cw);
            mEditors[c]->height.set(ch > 40 ? ch : 40);
        }
    }
}
}
