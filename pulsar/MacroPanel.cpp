#include "MacroPanel.h"
#include "Chrome.h"

namespace arstro
{
namespace pulsar
{
    using namespace artboard;

    MacroPanel::MacroPanel(const Theme &theme, const Color &accent) : mAccent(accent)
    {
        width.set(340.0);
        height.set(230.0);

        const char *labels[4] = {"M1", "M2", "M3", "M4"};
        for (int i = 0; i < 4; ++i)
        {
            int idx = i;
            auto k = std::make_shared<Knob>(theme.knob);
            k->label = labels[i];
            k->setRange(0.0, 1.0); k->setValue(0.0); k->setDefault(0.0);
            k->x.set(45.0 + i * 64.0); k->y.set(96.0); // 4 centred: (340-250)/2 = 45
            k->width.set(58.0); k->height.set(48.0);
            k->onChange = [this, idx](double v) { mMacro[idx] = v; };
            mKnobs[i] = k;
            addChild(k);
        }
    }

    void MacroPanel::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawPanelChrome(t, w, h, mAccent, "MACRO");

        // hint strip under the title
        t.setFill(Color{1, 1, 1, 0.28});
        t.drawText("assignable controls", 14.0, 44.0, 10.0);
    }
}
}
