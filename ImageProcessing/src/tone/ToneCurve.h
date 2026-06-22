/*
 *  Arstro ImageProcessing Library
 *
 *  ToneCurve: a point/parametric tone curve applied via a 1024-entry LUT. Control
 *  points are monotone in x and the curve is evaluated piecewise-linearly between
 *  them (no overshoot). The curve domain toggles between Log (perceptual / sRGB-
 *  encoded — the default, matching how curves are drawn) and Linear.
 *
 *  The control points are not scalar smoothed properties, so they use dedicated
 *  setters rather than the property system.
 */
#pragma once
#include "../base/ImageProcessor.h"
#include <utility>
#include <vector>

namespace arstro
{
    class ToneCurve : public PointProcessor
    {
    public:
        static constexpr int kLut = 1024;

        ToneCurve();

        /** Set control points in [0,1]x[0,1] (display domain). Sorted/clamped here. */
        void setPoints(const std::vector<std::pair<float, float>> &points);
        /** true = Log/perceptual (sRGB-encoded) domain (default); false = Linear. */
        void setLogScale(bool log);
        bool logScale() const { return mLog; }

    protected:
        void processPixel(const Pixel *in, Pixel *out, int channels) override;

    private:
        void rebuildLut();
        Pixel curveAt(Pixel d) const;  // sample LUT at display coord d in [0,1]

        std::vector<std::pair<float, float>> mPoints;
        Pixel mLut[kLut];
        bool mLog = true;
    };
}
