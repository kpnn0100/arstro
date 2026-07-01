/*
 *  Cosmo by arstro — MaskPanel: the "Mask" tab. A toolbar adds radial / linear /
 *  brush masks, a combo selects the active one, Delete removes it, and an invert
 *  toggle + feather slider shape its coverage. Below, an embedded ParamPanel of
 *  LOCAL adjustment sliders edits the selected mask's LocalAdjust (the same controls
 *  as Basic, applied only through the mask). The panel only emits MaskParams edits
 *  via callbacks; CosmoApp owns the mask list and pushes it to the engine.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "../../ImageProcessing/src/engine/EditParams.h"
#include "ParamPanel.h"
#include "../widgets/TextToggle.h"
#include "../widgets/IconButton.h"
#include <functional>
#include <memory>
#include <vector>

namespace arstro
{
namespace cosmo
{
    class MaskPanel : public artboard::Segment
    {
    public:
        MaskPanel(const artboard::Theme &theme, const artboard::Color &accent);

        std::function<void(int)> onAdd;                 // type (0 radial,1 linear,2 brush)
        std::function<void(int)> onSelect;              // mask index
        std::function<void()> onDelete;
        std::function<void(bool)> onInvert;
        std::function<void(double)> onFeather;          // 0..1
        std::function<void(const arstro::LocalAdjust &)> onLocal;

        /** Refresh the list + selected mask's controls (no callbacks). */
        void setMasks(const std::vector<arstro::MaskParams> &masks, int selected);
        void layout(double w, double h);

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        artboard::Color mAccent;
        std::shared_ptr<artboard::Button> mAddRadial, mAddLinear, mAddBrush;
        std::shared_ptr<IconButton> mDelete;
        std::shared_ptr<artboard::ComboBox> mSelect;
        std::shared_ptr<TextToggle> mInvert;
        std::shared_ptr<artboard::Slider> mFeather;
        std::shared_ptr<ParamPanel> mLocal;
        arstro::LocalAdjust mEditing;
        bool mHasSelection = false;
        double mFeatherLabelX = 0, mFeatherLabelY = 0;
    };
}
}
