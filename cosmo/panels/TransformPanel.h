/*
 *  Cosmo by arstro — TransformPanel: straighten (slider), 90-degree steps
 *  (ComboBox), and crop via normalized x/y/w/h sliders, laid out in a column.
 *  Responsive via layout(w,h).
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <functional>
#include <memory>
#include <string>
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

        struct State { double rotation = 0; int quarter = 0; double cropX = 0, cropY = 0, cropW = 1, cropH = 1; };
        void setState(const State &s);
        void layout(double w, double h);

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        void emitCrop();
        struct Row { std::shared_ptr<artboard::Segment> ctrl; std::string label; bool labeled; double baseY = 0; };

        artboard::Color mAccent;
        std::shared_ptr<artboard::Slider> mRotate, mX, mY, mW, mH;
        std::shared_ptr<artboard::ComboBox> mQuarter;
        std::vector<Row> mRows;
    };
}
}
