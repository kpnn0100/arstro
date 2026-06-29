/*
 *  Cosmo by arstro — TransformPanel: a rotate KNOB with a live degree readout,
 *  90-degree CCW/CW icon buttons, and an aspect-ratio selector (with a Custom
 *  entry that locks the crop box's current ratio). Cropping itself is done by
 *  dragging the on-photo CropOverlay, so this panel no longer has crop sliders.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "../widgets/IconButton.h"
#include <functional>
#include <memory>

namespace arstro
{
namespace cosmo
{
    class TransformPanel : public artboard::Segment
    {
    public:
        TransformPanel(const artboard::Theme &theme, const artboard::Color &accent);

        std::function<void(double)> onRotate;       // straighten, degrees
        std::function<void(int)> onQuarterTurns;    // 0..3
        std::function<void(double)> onAspect;       // pixel ratio w:h, 0 = free
        std::function<double()> currentRatio;       // host supplies the crop box's live ratio (Custom)

        struct State { double rotation = 0; int quarter = 0; double cropX = 0, cropY = 0, cropW = 1, cropH = 1; };
        void setState(const State &s);
        void layout(double w, double h);

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        artboard::Color mAccent;
        std::shared_ptr<artboard::Knob> mRotateKnob;
        std::shared_ptr<IconButton> mCCW, mCW;
        std::shared_ptr<artboard::ComboBox> mAspectSel;
        int mQuarter = 0;
        double mKnobX = 0, mKnobY = 0, mKnobSize = 0;
        double mTurnLabelY = 0, mAspectLabelY = 0;
    };
}
}
