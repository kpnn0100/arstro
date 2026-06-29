#include "TransformPanel.h"
#include "../Chrome.h"
#include "../CosmoTheme.h"
#include <cstdio>

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    namespace
    {
        constexpr double kHeaderH = 34.0;
        constexpr double kPad = 12.0;
        // aspect presets; index 5 = Custom (lock the crop box's current ratio)
        const double kAspectRatios[5] = {0.0, 1.0, 1.5, 4.0 / 3.0, 16.0 / 9.0};
    }

    TransformPanel::TransformPanel(const Theme &theme, const Color &accent) : mAccent(accent)
    {
        width.set(300.0);
        height.set(220.0);

        mRotateKnob = std::make_shared<Knob>(theme.knob);
        mRotateKnob->setRange(-45.0, 45.0); mRotateKnob->setValue(0.0); mRotateKnob->setDefault(0.0);
        mRotateKnob->sensitivity = 220.0;  // gentle: full range over a long drag
        mRotateKnob->onChange = [this](double v) { if (onRotate) onRotate(v); };
        addChild(mRotateKnob);

        mCCW = std::make_shared<IconButton>(IconButton::Icon::RotateCCW, accent);
        mCCW->onClick = [this] { mQuarter = (mQuarter + 3) & 3; if (onQuarterTurns) onQuarterTurns(mQuarter); };
        addChild(mCCW);
        mCW = std::make_shared<IconButton>(IconButton::Icon::RotateCW, accent);
        mCW->onClick = [this] { mQuarter = (mQuarter + 1) & 3; if (onQuarterTurns) onQuarterTurns(mQuarter); };
        addChild(mCW);

        mAspectSel = std::make_shared<ComboBox>(theme.combo);
        mAspectSel->setOptions({"Free", "1:1", "3:2", "4:3", "16:9", "Custom"});
        mAspectSel->setSelectedIndex(0);
        mAspectSel->onChange = [this](int i) {
            if (!onAspect) return;
            if (i == 5) onAspect(currentRatio ? currentRatio() : 0.0);  // lock current crop ratio
            else onAspect(kAspectRatios[i >= 0 && i < 5 ? i : 0]);
        };
        addChild(mAspectSel);
    }

    void TransformPanel::layout(double w, double h)
    {
        width.set(w); height.set(h);
        const double cw = w - 2 * kPad;
        double y = kHeaderH + 10.0;

        mKnobSize = 56.0; mKnobX = kPad; mKnobY = y;
        mRotateKnob->x.set(mKnobX); mRotateKnob->y.set(mKnobY);
        mRotateKnob->width.set(mKnobSize); mRotateKnob->height.set(mKnobSize);
        y += mKnobSize + 14.0;

        const double ib = 30.0;
        mTurnLabelY = y + ib * 0.5 + 4.0;
        mCCW->x.set(kPad + cw - 2 * ib - 8.0); mCCW->y.set(y); mCCW->width.set(ib); mCCW->height.set(ib);
        mCW->x.set(kPad + cw - ib); mCW->y.set(y); mCW->width.set(ib); mCW->height.set(ib);
        y += ib + 16.0;

        const double labelW = 52.0;
        mAspectLabelY = y + 13.0 + 4.0;
        mAspectSel->x.set(kPad + labelW); mAspectSel->y.set(y);
        mAspectSel->width.set(cw - labelW); mAspectSel->height.set(26.0);
    }

    void TransformPanel::setState(const State &s)
    {
        mRotateKnob->setValue(s.rotation);
        mQuarter = s.quarter;
    }

    void TransformPanel::onPaint(IRenderTarget &t) const
    {
        drawPanelChrome(t, width.value(), height.value(), "TRANSFORM");

        // "straighten" label + live degree readout beside the knob
        t.setFill(palette::muted());
        t.drawText("straighten", mKnobX + mKnobSize + 14.0, mKnobY + 18.0, 11.0);
        char deg[24];
        std::snprintf(deg, sizeof(deg), "%.1f°", mRotateKnob->value());
        t.setFill(palette::ink());
        t.drawText(deg, mKnobX + mKnobSize + 14.0, mKnobY + 40.0, 16.0);

        t.setFill(palette::muted());
        t.drawText("rotate 90", mKnobX, mTurnLabelY, 11.0);
        t.drawText("aspect", 10.0, mAspectLabelY, 10.0);
    }
}
}
