/*
 *  cosmo_v2 by arstro — HueCurveEditor: the CYCLIC hue mapper the colour mixer
 *  edits with (task point 10: "Mixer need to use curve to edit, refer from
 *  original cosmo"). X = pixel input hue [0,360); Y = adjustment [-1,1]
 *  (0 = no change). Bezier-capable control points (Alt-drag pulls tangent
 *  handles); the curve wraps continuously across the 360/0 seam. In
 *  "mapped-hue" mode (the Hue channel) the line is coloured by the OUTPUT hue.
 *  Emits its bezier CONTROL points (EditParams::mixer[channel]) so a reopened
 *  project restores the exact editable curve; the engine flattens them to a LUT.
 *
 *  Adapted from cosmo/widgets/HueCurveEditor with the cosmo_v2 palette.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "../../ImageProcessing/src/base/CurvePoint.h"
#include <functional>
#include <utility>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class HueCurveEditor : public artboard::Segment
    {
    public:
        HueCurveEditor();

        std::function<void(const std::vector<CurvePoint> &)> onChange;
        void setPoints(const std::vector<CurvePoint> &pts);  // bezier control points
        void reset();                                  // back to a flat (no-op) curve
        void setMappedHue(bool m) { mMappedHue = m; }  // colour the line by output hue (Hue channel)

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        double plotTop() const { return 10.0; }
        double plotBot() const { return height.value() - 10.0 - 10.0; }
        double midY() const { return (plotTop() + plotBot()) * 0.5; }
        double halfH() const { return (plotBot() - plotTop()) * 0.5; }
        double pxh(double hue) const;
        double pyv(double y) const;
        double nxh(double px, bool wrap) const;
        double nyv(double py) const;
        int pointAt(const artboard::Point &local) const;
        bool handleAt(const artboard::Point &local, int &idx, int &kind) const;
        void emit();

        std::vector<CurvePoint> mPts;  // x in [0,360), y in [-1,1]
        bool mMappedHue = false;
        int mDragIdx = -1;
        int mDragKind = 0;  // 0 body, 1 in, 2 out, 3 symmetric pull
    };
}
}
