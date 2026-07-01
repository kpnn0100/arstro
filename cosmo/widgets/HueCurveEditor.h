/*
 *  Cosmo by arstro — HueCurveEditor: a CYCLIC 2-axis mapper for the colour mixer.
 *  X = pixel input hue [0,360); Y = adjustment [-1,1] (0 = no change). Bezier-capable
 *  control points (Alt-drag for handles, like CurveEditor); the curve wraps
 *  continuously across the 360/0 seam (a point dragged off the left edge rejoins on
 *  the right with no break). In "mapped-hue" mode (the Hue channel) the line is
 *  coloured by the OUTPUT hue, so you see what each input hue maps to. Emits a dense
 *  sampling for the engine.
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
    class HueCurveEditor : public artboard::Segment
    {
    public:
        explicit HueCurveEditor(const artboard::Color &accent);

        std::function<void(const std::vector<std::pair<float, float>> &)> onChange;
        void setPoints(const std::vector<std::pair<float, float>> &pts);  // corner points
        void setMappedHue(bool m) { mMappedHue = m; }  // colour the line by output hue (Hue channel)
        /** Hue distribution of the image (bins over 0..360, normalised 0..1), drawn
         *  behind the curve so you see which hues the edit affects. */
        void setHueHistogram(std::vector<float> bins) { mHueHist = std::move(bins); }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        double plotTop() const { return 12.0; }
        double plotBot() const { return height.value() - 12.0 - 10.0; }
        double midY() const { return (plotTop() + plotBot()) * 0.5; }
        double halfH() const { return (plotBot() - plotTop()) * 0.5; }
        double pxh(double hue) const;
        double pyv(double y) const;
        double nxh(double px, bool wrap) const;
        double nyv(double py) const;
        int pointAt(const artboard::Point &local) const;
        bool handleAt(const artboard::Point &local, int &idx, int &kind) const;
        void emit();

        std::vector<CtrlPoint> mPts;  // x in [0,360), y in [-1,1]
        std::vector<float> mHueHist;  // hue distribution background
        artboard::Color mAccent;
        bool mMappedHue = false;
        int mDragIdx = -1;
        int mDragKind = 0;  // 0 body, 1 in, 2 out, 3 symmetric pull
    };
}
}
