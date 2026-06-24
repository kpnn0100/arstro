#include "ColorGradingPanel.h"
#include "../Chrome.h"
#include "../CosmoTheme.h"

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    namespace
    {
        constexpr double kHeaderH = 34.0;
        constexpr double kPad = 12.0;
        constexpr double kLabelW = 64.0;
        constexpr double kCtrlH = 16.0;
    }

    ColorGradingPanel::ColorGradingPanel(const Theme &theme, const Color &accent) : mAccent(accent)
    {
        width.set(300.0);
        height.set(300.0);

        mRegionSel = std::make_shared<ComboBox>(theme.combo);
        mRegionSel->setOptions({"Shadows", "Midtones", "Highlights"});
        mRegionSel->setSelectedIndex(0);
        mRegionSel->onChange = [this](int r) { mRegion = r; loadRegion(); };
        addChild(mRegionSel);

        auto slider = [&](double mn, double mx, double def) {
            auto s = std::make_shared<Slider>(theme.slider);
            s->setRange(mn, mx); s->setValue(def); s->setDefault(def);
            addChild(s);
            return s;
        };
        mHue = slider(0, 360, 0);
        mSat = slider(0, 100, 0);
        mLum = slider(-100, 100, 0);
        mBalance = slider(-100, 100, 0);
        for (auto *s : {mHue.get(), mSat.get(), mLum.get()}) s->onChange = [this](double) { emitGrade(); };
        mBalance->onChange = [this](double v) { mState.balance = v; if (onBalance) onBalance(v); };

        mRemap = std::make_shared<TextToggle>("remap", accent);
        mRemap->onChange = [this](bool on) { mState.remapOn = on; emitRemap(); };
        addChild(mRemap);

        mSrc = slider(0, 360, 0);
        mRange = slider(0, 180, 30);
        mDst = slider(0, 360, 0);
        mStrength = slider(0, 100, 0);
        for (auto *s : {mSrc.get(), mRange.get(), mDst.get(), mStrength.get()})
            s->onChange = [this](double) { emitRemap(); };

        mRows = {
            {mRegionSel, "", false},
            {mHue, "hue", true}, {mSat, "sat", true}, {mLum, "lum", true},
            {mBalance, "balance", true},
            {mRemap, "", false},
            {mSrc, "src", true}, {mRange, "range", true}, {mDst, "dst", true}, {mStrength, "amount", true},
        };
    }

    void ColorGradingPanel::layout(double w, double h)
    {
        width.set(w);
        height.set(h);
        const int rows = (int)mRows.size();
        const double usable = h - kHeaderH - kPad;
        const double rowH = rows > 0 ? usable / rows : usable;
        for (int i = 0; i < rows; ++i)
        {
            const double top = kHeaderH + i * rowH;
            Row &r = mRows[i];
            r.baseY = top + rowH * 0.5 + 3.0;
            const bool labeled = r.labeled;
            const double cx = labeled ? kLabelW : kPad;
            const double cw = w - cx - kPad;
            r.ctrl->x.set(cx);
            r.ctrl->width.set(cw > 20 ? cw : 20);
            if (labeled)
            {
                r.ctrl->y.set(top + (rowH - kCtrlH) * 0.5);
                r.ctrl->height.set(kCtrlH);
            }
            else
            {
                // combo / toggle: a bit taller
                const double ch = rowH > 26 ? 22.0 : rowH - 2.0;
                r.ctrl->y.set(top + (rowH - ch) * 0.5);
                r.ctrl->height.set(ch);
            }
        }
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
        t.setFill(palette::muted());
        for (const auto &r : mRows)
            if (r.labeled)
                t.drawText(r.label, 10.0, r.baseY, 10.0);
    }
}
}
