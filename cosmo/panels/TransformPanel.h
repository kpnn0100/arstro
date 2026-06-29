/*
 *  Cosmo by arstro — TransformPanel: straighten (slider), 90-degree rotation via two
 *  buttons (CCW / CW — clearer than a combobox), and crop via normalized x/y/w/h
 *  sliders, laid out in a column. Responsive via layout(w,h).
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <functional>
#include <memory>
#include <vector>

namespace arstro
{
namespace cosmo
{
    class TransformPanel : public artboard::Segment
    {
    public:
        TransformPanel(const artboard::Theme &theme, const artboard::Color &accent);

        std::function<void(double)> onRotate;                       // degrees
        std::function<void(int)> onQuarterTurns;                    // 0..3
        std::function<void(double, double, double, double)> onCrop;  // normalized x,y,w,h
        std::function<void(double)> onAspect;                       // pixel ratio w:h, 0 = free

        struct State { double rotation = 0; int quarter = 0; double cropX = 0, cropY = 0, cropW = 1, cropH = 1; };
        void setState(const State &s);
        void layout(double w, double h);

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        void emitCrop();

        artboard::Color mAccent;
        std::shared_ptr<artboard::Slider> mRotate, mX, mY, mW, mH;
        std::shared_ptr<artboard::Button> mCCW, mCW;
        std::shared_ptr<artboard::ComboBox> mAspectSel;
        int mQuarter = 0;
        double mRowY[7] = {0, 0, 0, 0, 0, 0, 0};  // label baselines
    };
}
}
