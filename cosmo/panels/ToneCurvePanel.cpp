#include "ToneCurvePanel.h"
#include "../Chrome.h"

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    ToneCurvePanel::ToneCurvePanel(const Theme &theme, const Color &accent) : mAccent(accent)
    {
        width.set(300.0);
        height.set(244.0);

        mLogToggle = std::make_shared<ToggleSwitch>(theme.toggle);
        mLogToggle->width.set(34.0);
        mLogToggle->height.set(16.0);
        mLogToggle->x.set(width.value() - 48.0);
        mLogToggle->y.set(11.0);
        mLogToggle->setOn(true);  // engine default is log/perceptual
        mLogToggle->onChange = [this](bool on) { if (onLogChange) onLogChange(on); };
        addChild(mLogToggle);

        mCurve = std::make_shared<CurveEditor>(accent);
        mCurve->x.set(10.0);
        mCurve->y.set(40.0);
        mCurve->width.set(width.value() - 20.0);
        mCurve->height.set(height.value() - 50.0);
        mCurve->onChange = [this](const std::vector<std::pair<float, float>> &pts) {
            if (onCurveChange) onCurveChange(pts);
        };
        addChild(mCurve);
    }

    void ToneCurvePanel::onPaint(IRenderTarget &t) const
    {
        drawPanelChrome(t, width.value(), height.value(), mAccent, "CURVE");
        t.setFill(Color{1, 1, 1, 0.45});
        t.drawText("log", width.value() - 78.0, 22.0, 10.0);
    }
}
}
