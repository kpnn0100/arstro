#include "ToneCurvePanel.h"
#include "../Chrome.h"
#include "../CosmoTheme.h"

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    ToneCurvePanel::ToneCurvePanel(const Theme &theme, const Color &accent) : mAccent(accent)
    {
        width.set(300.0);
        height.set(244.0);

        mLogToggle = std::make_shared<TextToggle>("log", accent);
        mLogToggle->x.set(width.value() - 44.0);
        mLogToggle->y.set(10.0);
        mLogToggle->setOn(true);  // engine default is log/perceptual
        mLogToggle->onChange = [this](bool on) { if (onLogChange) onLogChange(on); };
        addChild(mLogToggle);

        ButtonStyle flat = theme.button;  // flat "chip" style, matching MaskPanel's add buttons
        flat.idle = {Paint::filled(palette::surface()), radius::control()};
        flat.pressed = {Paint::filled(palette::line()), radius::control()};
        flat.label.color = palette::ink();
        mReset = std::make_shared<Button>("Reset", flat);
        mReset->onClick = [this] { mCurve->reset(); };
        addChild(mReset);

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
        mReset->x.set(w - 92.0); mReset->y.set(8.0);
        mReset->width.set(40.0); mReset->height.set(20.0);
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
