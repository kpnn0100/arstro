/*
 *  Cosmo by arstro — HueCurveEditor: a 2-axis mapper for the colour mixer. X is the
 *  pixel's input hue [0,360); Y is the adjustment in [-1,1] (centered at 0 = no
 *  change). The curve is CYCLIC — the last point wraps continuously back to the
 *  first across the 360/0 seam, so colours never go discrete. Drag points to shape
 *  it, double-click to add a point (or remove one). A hue gradient strip along the
 *  bottom gives context. Emits the point list on every change.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <functional>
#include <utility>
#include <vector>

namespace arstro
{
namespace cosmo
{
    class HueCurveEditor : public artboard::Segment
    {
    public:
        explicit HueCurveEditor(const artboard::Color &accent);

        std::function<void(const std::vector<std::pair<float, float>> &)> onChange;
        void setPoints(const std::vector<std::pair<float, float>> &pts);
        const std::vector<std::pair<float, float>> &points() const { return mPts; }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        double plotTop() const { return 12.0; }
        double plotBot() const { return height.value() - 12.0 - 10.0; }  // leave room for hue strip
        double midY() const { return (plotTop() + plotBot()) * 0.5; }
        double halfH() const { return (plotBot() - plotTop()) * 0.5; }
        double pxh(double hue) const;   // hue 0..360 -> pixel x
        double pyv(double y) const;     // y -1..1 -> pixel y
        double nxh(double px) const;    // pixel x -> hue 0..360
        double nyv(double py) const;    // pixel y -> y -1..1
        int pointAt(const artboard::Point &local) const;
        void emit();

        std::vector<std::pair<float, float>> mPts;  // (hue 0..360, y -1..1), unordered
        artboard::Color mAccent;
        int mDrag = -1;
    };
}
}
