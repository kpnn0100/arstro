#include "HistogramPanel.h"
#include "../Chrome.h"
#include "../CosmoTheme.h"

namespace arstro
{
namespace cosmo
{
    using namespace artboard;
    using HD = arstro::HistogramData;

    HistogramPanel::HistogramPanel(const Theme &theme, const Color &accent) : mAccent(accent)
    {
        (void)theme;
        width.set(352.0);
        height.set(190.0);
        mLogToggle = std::make_shared<TextToggle>("log", accent);
        mLogToggle->x.set(width.value() - 44.0);
        mLogToggle->y.set(10.0);
        mLogToggle->onChange = [this](bool on) { mLog = on; };
        addChild(mLogToggle);
    }

    void HistogramPanel::layout(double w, double h)
    {
        width.set(w);
        height.set(h);
        mLogToggle->x.set(w - 44.0);
        mLogToggle->y.set(10.0);
    }

    void HistogramPanel::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawPanelChrome(t, w, h, "HISTOGRAM");

        const double px = 12.0, py = 40.0, pw = w - 24.0, ph = h - 52.0;
        drawRoundedRect(t, Rect{px, py, pw, ph}, radius::control(),
                        Paint::filledStroked(palette::bg(), palette::line(), 1.0));

        float norm[4][HD::kBins];
        if (mLog) arstro::Histogram::toLog(mData, norm);
        else arstro::Histogram::toLinear(mData, norm);

        const Color cols[4] = {
            Color{0.95, 0.30, 0.34, 0.45},
            Color{0.35, 0.90, 0.45, 0.45},
            Color{0.40, 0.55, 1.00, 0.45},
            Color{1.00, 1.00, 1.00, 0.22}};
        for (int ch = 0; ch < 4; ++ch)
        {
            t.beginPath();
            t.moveTo(px, py + ph);
            for (int i = 0; i < HD::kBins; ++i)
            {
                const double x = px + (double)i / (HD::kBins - 1) * pw;
                const double y = py + ph - (double)norm[ch][i] * ph;
                t.lineTo(x, y);
            }
            t.lineTo(px + pw, py + ph);
            t.closePath();
            t.setFill(cols[ch]);
            t.fillPath();
        }
    }
}
}
