/*
 *  cosmo_v2 by arstro — CurvePanel (App.tsx CurvePanel): RGB/R/G/B channel
 *  picker + Reset button above a 232x164 draggable tone-curve plot. The
 *  Figma mock draws one fixed bezier with a single static control point;
 *  this is genuinely interactive -- click-drag moves a point, click on empty
 *  curve space adds one, double-click removes one (not the endpoints).
 *  EditParams has ONE curve (no separate per-channel R/G/B data), so all
 *  four channel buttons currently edit that same shared curve; the picker
 *  is kept for visual completeness but is not decorative dead weight if the
 *  engine grows per-channel curves later -- it's just not invented data now.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "SegmentedControl.h"
#include "IconButton.h"
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class CurvePanel : public artboard::Segment
    {
    public:
        static constexpr double kPlotW = 232.0, kPlotH = 164.0;

        CurvePanel();

        void setCurve(const std::vector<std::pair<float, float>> &points) { mPoints = points; }
        void layout();

        std::function<void(std::vector<std::pair<float, float>>)> onCurveChange;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        artboard::Rect plotRect() const;
        int hitPoint(const artboard::Point &plotLocal) const;  // -1 if none within radius

        std::shared_ptr<SegmentedControl> mChannelPicker;
        std::shared_ptr<IconButton> mResetBtn;
        std::vector<std::pair<float, float>> mPoints{{0, 0}, {1, 1}};
        int mDragIndex = -1;
        double mPlotY = 0.0;
    };
}
}
