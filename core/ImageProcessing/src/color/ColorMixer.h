/*
 *  Arstro ImageProcessing Library
 *
 *  ColorMixer: per-hue HSL adjustment driven by THREE cyclic curves over the input
 *  hue axis [0,360) — one each for hue-shift, saturation, and luminance. Each curve
 *  is a list of control points (hue, y) with y in [-1,1], evaluated into a cyclic
 *  LUT (the last point wraps continuously back to the first across the 360/0 seam),
 *  so adjustments vary smoothly around the colour wheel with no banding between
 *  discrete bands. A point op.
 *
 *  y maps to: Hue -> hue += y*60 deg; Sat -> s *= (1 + y); Lum -> l += y*0.5.
 */
#pragma once
#include "../base/CurvePoint.h"
#include "../base/ImageProcessor.h"
#include <utility>
#include <vector>

namespace arstro
{
    class ColorMixer : public PointProcessor
    {
    public:
        enum Channel { Hue = 0, Sat = 1, Lum = 2 };
        static constexpr int kLut = 256;

        ColorMixer();

        /** Set a channel's curve: bezier CONTROL points (hue 0..360, y in [-1,1]); the
         *  curve is cyclic (wraps across the 360/0 seam). The control points are flattened
         *  to a dense polyline (curve::sample) before the LUT is built, so smooth handles
         *  are honoured instead of linearly connecting the bare control points. */
        void setCurve(Channel c, const std::vector<CurvePoint> &points);

    protected:
        void processPixel(const Pixel *in, Pixel *out, int channels) override;

    private:
        void rebuild(int c);
        float sampleCyclic(int c, float hue) const;  // hue in [0,360) -> interpolated y

        std::vector<std::pair<float, float>> mPoints[3];
        float mLut[3][kLut];
    };
}
