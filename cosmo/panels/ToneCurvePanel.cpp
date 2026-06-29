#include "ToneCurvePanel.h"
#include "../Chrome.h"

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    ToneCurvePanel::ToneCurvePanel(const Theme &theme, const Color &accent) : mAccent(accent)
    {
        (void)theme;
        width.set(300.0);
        height.set(244.0);

        mLogToggle = std::make_shared<TextToggle>("log", accent);
        mLogToggle->x.set(width.value() - 44.0);
        mLogToggle->y.set(10.0);
        mLogToggle->setOn(true);  // engine default is log/perceptual
        mLogToggle->onChange = [this](bool on) { if (onLogChange) onLogChange(on); };
        addChild(mLogToggle);

        mCurve = std::make_shared<CurveEditor>(accent);
        mCurve->onChange = [this](const std::vector<std::pair<float, float>> &pts) {
            if (onCurveChange) onCurveChange(pts);
        };
        addChild(mCurve);
    }

    void ToneCurvePanel::layout(double w, double h)
    {
        width.set(w);
        height.set(h);
        mLogToggle->x.set(w - 44.0);
        mLogToggle->y.set(10.0);
        mCurve->x.set(10.0);
        mCurve->y.set(40.0);
        mCurve->width.set(w - 20.0);
        mCurve->height.set(h - 50.0);
    }

    void ToneCurvePanel::onPaint(IRenderTarget &t) const
    {
        drawPanelChrome(t, width.value(), height.value(), "CURVE");
    }
}
}
