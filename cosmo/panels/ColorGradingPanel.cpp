#include "ColorGradingPanel.h"
#include "../Chrome.h"

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    ColorGradingPanel::ColorGradingPanel(const Theme &theme, const Color &accent) : mAccent(accent)
    {
        width.set(300.0);
        height.set(290.0);

        mRegionSel = std::make_shared<ComboBox>(theme.combo);
        mRegionSel->setOptions({"Shadows", "Midtones", "Highlights"});
        mRegionSel->setSelectedIndex(0);
        mRegionSel->x.set(14.0); mRegionSel->y.set(40.0);
        mRegionSel->width.set(170.0); mRegionSel->height.set(24.0);
        mRegionSel->onChange = [this](int r) { mRegion = r; loadRegion(); };
        addChild(mRegionSel);

        auto knob = [&](double mn, double mx, double def, const char *label, double x, double y) {
            auto k = std::make_shared<Knob>(theme.knob);
            k->label = label; k->setRange(mn, mx); k->setValue(def); k->setDefault(def);
            k->width.set(40.0); k->height.set(40.0); k->x.set(x); k->y.set(y);
            addChild(k);
            return k;
        };
        mHue = knob(0, 360, 0, "hue", 20, 80);
        mSat = knob(0, 100, 0, "sat", 80, 80);
        mLum = knob(-100, 100, 0, "lum", 140, 80);
        mBalance = knob(-100, 100, 0, "balance", 210, 80);
        for (auto *k : {mHue.get(), mSat.get(), mLum.get()}) k->onChange = [this](double) { emitGrade(); };
        mBalance->onChange = [this](double v) { mState.balance = v; if (onBalance) onBalance(v); };

        mRemap = std::make_shared<ToggleSwitch>(theme.toggle);
        mRemap->width.set(34.0); mRemap->height.set(16.0);
        mRemap->x.set(70.0); mRemap->y.set(150.0);
        mRemap->onChange = [this](bool on) { mState.remapOn = on; emitRemap(); };
        addChild(mRemap);

        mSrc = knob(0, 360, 0, "src", 20, 190);
        mRange = knob(0, 180, 30, "range", 90, 190);
        mDst = knob(0, 360, 0, "dst", 160, 190);
        mStrength = knob(0, 100, 0, "amt", 230, 190);
        for (auto *k : {mSrc.get(), mRange.get(), mDst.get(), mStrength.get()})
            k->onChange = [this](double) { emitRemap(); };
    }

    void ColorGradingPanel::loadRegion()
    {
        mHue->setValue(mState.grade[mRegion][0]);
        mSat->setValue(mState.grade[mRegion][1]);
        mLum->setValue(mState.grade[mRegion][2]);
    }

    void ColorGradingPanel::emitGrade()
    {
        mState.grade[mRegion] = {mHue->value(), mSat->value(), mLum->value()};
        if (onGrade) onGrade(mRegion, mHue->value(), mSat->value(), mLum->value());
    }

    void ColorGradingPanel::emitRemap()
    {
        mState.remapSrc = mSrc->value();
        mState.remapRange = mRange->value();
        mState.remapDst = mDst->value();
        mState.remapStrength = mStrength->value();
        if (onRemap)
            onRemap(mState.remapOn, mSrc->value(), mRange->value(), mDst->value(), mStrength->value() / 100.0);
    }

    void ColorGradingPanel::setState(const State &s)
    {
        mState = s;
        loadRegion();
        mBalance->setValue(s.balance);
        mRemap->setOn(s.remapOn);
        mSrc->setValue(s.remapSrc);
        mRange->setValue(s.remapRange);
        mDst->setValue(s.remapDst);
        mStrength->setValue(s.remapStrength);
    }

    void ColorGradingPanel::onPaint(IRenderTarget &t) const
    {
        drawPanelChrome(t, width.value(), height.value(), mAccent, "GRADE");
        t.setFill(Color{1, 1, 1, 0.45});
        t.drawText("remap", 14.0, 162.0, 10.0);
    }
}
}
