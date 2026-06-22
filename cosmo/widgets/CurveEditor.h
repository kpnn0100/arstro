/*
 *  Cosmo by arstro — CurveEditor: an interactive tone-curve editor. Control points
 *  in [0,1]x[0,1] joined piecewise-linearly (matching the engine's ToneCurve LUT).
 *  Drag a point to move it (endpoints keep their x; interior points stay ordered),
 *  double-click empty space to add a point, double-click a point to remove it.
 *  Emits the point list on every change.
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
    class CurveEditor : public artboard::Segment
    {
    public:
        explicit CurveEditor(const artboard::Color &accent);

        std::function<void(const std::vector<std::pair<float, float>> &)> onChange;

        void setPoints(const std::vector<std::pair<float, float>> &pts);
        void reset();  // identity diagonal

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &localPoint) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        double px(double nx) const;  // normalized x -> pixel
        double py(double ny) const;
        double nx(double pxv) const; // pixel -> normalized
        double ny(double pyv) const;
        int nodeAt(const artboard::Point &local) const;  // index within hit radius, else -1
        void emit();

        std::vector<std::pair<float, float>> mPts;  // sorted by x, in [0,1]
        artboard::Color mAccent;
        int mDrag = -1;
    };
}
}
