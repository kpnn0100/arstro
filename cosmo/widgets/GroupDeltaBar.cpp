#include "GroupDeltaBar.h"
#include "../CosmoTheme.h"

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    GroupDeltaBar::GroupDeltaBar(const Theme &theme, const Color &accent) : mAccent(accent)
    {
        mExp = std::make_shared<Slider>(theme.slider);
        mExp->setRange(-5, 5); mExp->setValue(0); mExp->setDefault(0);
        mExp->onChange = [this](double) { if (onChange) onChange(mExp->value(), mTemp->value()); };
        addChild(mExp);

        mTemp = std::make_shared<Slider>(theme.slider);
        mTemp->setRange(-100, 100); mTemp->setValue(0); mTemp->setDefault(0);
        mTemp->onChange = [this](double) { if (onChange) onChange(mExp->value(), mTemp->value()); };
        addChild(mTemp);
    }

    void GroupDeltaBar::setValues(double exposure, double temp)
    {
        mExp->setValue(exposure);
        mTemp->setValue(temp);
    }

    void GroupDeltaBar::layout(double w, double h)
    {
        width.set(w); height.set(h);
        const double labelW = 60.0, gap = 18.0;
        const double half = (w - gap) / 2;
        const double sh = 14.0, sy = (h - sh) * 0.5;
        mExpLabelX = 4; mTempLabelX = half + gap + 4; mLabelY = h * 0.5 + 4;
        mExp->x.set(labelW); mExp->y.set(sy); mExp->width.set(half - labelW); mExp->height.set(sh);
        mTemp->x.set(half + gap + labelW); mTemp->y.set(sy); mTemp->width.set(half - labelW); mTemp->height.set(sh);
    }

    void GroupDeltaBar::onPaint(IRenderTarget &t) const
    {
        drawRoundedRect(t, Rect{0, 0, width.value(), height.value()}, radius::control(),
                        Paint::filled(palette::surface()));
        t.setFill(mAccent);
        t.drawText("per-image", 4, mLabelY - 14, 9.0);
        t.setFill(palette::muted());
        t.drawText("expo", mExpLabelX, mLabelY, 10.0);
        t.drawText("temp", mTempLabelX, mLabelY, 10.0);
    }
}
}
