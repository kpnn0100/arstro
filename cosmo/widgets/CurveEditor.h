/*
 *  Cosmo by arstro — CurveEditor: an interactive tone-curve editor over [0,1]x[0,1].
 *  Control points are CORNERS by default (straight segments); Alt-drag a point to
 *  pull out symmetric bezier handles. Once a point has handles, dragging either one
 *  mirrors the opposite handle (keeps a straight tangent through the point); hold
 *  Alt while dragging a handle to break the mirror and move just that one. Drag a
 *  point to move it (endpoints keep x); double-click empty space to add a corner
 *  point, double-click a point to remove it.
 *  Emits a dense piecewise-linear sampling of the curve (the engine keeps a simple
 *  LUT); the visible curve is the same dense sampling, so it reads smooth.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "BezierCurve.h"
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
        void setPoints(const std::vector<std::pair<float, float>> &pts);  // corner points
        void reset();
        /** Faint luminance histogram drawn behind the curve (bins normalised 0..1). */
        void setHistogram(std::vector<float> bins) { mHist = std::move(bins); }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        double px(double x) const;   // normalized x -> pixel (no clamp)
        double py(double y) const;
        double nx(double pxv) const; // pixel -> normalized (no clamp)
        double ny(double pyv) const;
        int pointAt(const artboard::Point &local) const;
        bool handleAt(const artboard::Point &local, int &idx, int &kind) const;  // kind 1=in,2=out
        void emit();

        std::vector<CtrlPoint> mPts;
        std::vector<float> mHist;  // luminance histogram background
        artboard::Color mAccent;
        int mDragIdx = -1;
        int mDragKind = 0;  // 0 body, 1 in-handle, 2 out-handle, 3 symmetric pull (alt)
    };
}
}
