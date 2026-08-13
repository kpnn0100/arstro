#include "GainPanel.h"
#include "Chrome.h"

namespace arstro
{
namespace pulsar
{
    using namespace artboard;

    GainPanel::GainPanel(const Theme &theme, const Color &accent) : mAccent(accent)
    {
        width.set(170.0);
        height.set(357.0);

        mGain = std::make_shared<Knob>(theme.knob);
        mGain->label = "gain";
        mGain->setRange(0.0, 1.0); mGain->setValue(mGainVal); mGain->setDefault(mGainVal);
        mGain->x.set(26.0); mGain->y.set(52.0);
        mGain->width.set(64.0); mGain->height.set(54.0);
        mGain->onChange = [this](double v) { mGainVal = v; };
        addChild(mGain);

        mPan = std::make_shared<Knob>(theme.knob);
        mPan->label = "pan";
        mPan->setRange(0.0, 1.0); mPan->setValue(mPanVal); mPan->setDefault(mPanVal);
        mPan->x.set(26.0); mPan->y.set(140.0);
        mPan->width.set(64.0); mPan->height.set(54.0);
        mPan->onChange = [this](double v) { mPanVal = v; };
        addChild(mPan);
    }

    void GainPanel::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawPanelChrome(t, w, h, mAccent, "GAIN");

        // vertical meter on the right — follows the gain's modulated value (env-driven)
        const double mx = w - 36.0, my = 52.0, mw = 18.0, mh = h - my - 24.0;
        drawRoundedRect(t, Rect{mx, my, mw, mh}, 4.0,
                        Paint::filledStroked(Color{0, 0, 0, 0.35}, Color{1, 1, 1, 0.08}, 1.0));
        const double level = mGain->modulatedValue();
        const double fillH = mh * (level < 0 ? 0 : (level > 1 ? 1 : level));
        if (fillH > 1.0)
        {
            t.beginPath();
            t.moveTo(mx + 2, my + mh - 2); t.lineTo(mx + mw - 2, my + mh - 2);
            t.lineTo(mx + mw - 2, my + mh - fillH); t.lineTo(mx + 2, my + mh - fillH); t.closePath();
            t.setLinearFill(0, my + mh - fillH, 0, my + mh,
                            Color{mAccent.r, mAccent.g, mAccent.b, 0.95},
                            Color{mAccent.r, mAccent.g, mAccent.b, 0.35});
            t.fillPath();
        }
    }
}
}
