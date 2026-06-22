/*
 *  Cosmo by arstro — TransformPanel: straighten (rotation knob), 90-degree steps
 *  (ComboBox), and crop via normalized x/y/w/h knobs. Built from existing controls;
 *  a drag-handle crop overlay on the photo is a later visual refinement.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
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

        std::function<void(double)> onRotate;                       // degrees
        std::function<void(int)> onQuarterTurns;                    // 0..3
        std::function<void(double, double, double, double)> onCrop;  // normalized x,y,w,h

        struct State { double rotation = 0; int quarter = 0; double cropX = 0, cropY = 0, cropW = 1, cropH = 1; };
        void setState(const State &s);

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        void emitCrop();
        artboard::Color mAccent;
        std::shared_ptr<artboard::Knob> mRotate, mX, mY, mW, mH;
        std::shared_ptr<artboard::ComboBox> mQuarter;
    };
}
}
