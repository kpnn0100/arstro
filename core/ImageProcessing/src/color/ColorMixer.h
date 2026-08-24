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
 *  y maps to: Hue -> hue += y*180 deg; Sat -> s *= (1 + y); Lum -> l += y*0.5.
 *
 *  ...each scaled by a CHROMA WEIGHT, and that is not a refinement — it is what keeps a
 *  per-hue tool from firing on a pixel that has no hue. `rgbToHsl` computes hue by dividing
 *  channel differences by chroma, so as chroma goes to zero the hue of a pixel is decided by
 *  its last bit of noise: neighbouring pixels in a flat grey read as red, green and blue at
 *  random. The Lum channel is additive, so every one of them took a DIFFERENT full-strength
 *  lift and a smooth grey broke into speckle.
 *
 *  So each channel's y is multiplied by `smoothstep(kChromaFloor, kChromaFull, chroma)`:
 *  exactly zero for a pixel indistinguishable from neutral, full for anything with real
 *  colour, a ramp between. The weight is on CHROMA, not on HSL saturation, because HSL
 *  saturation is normalised by lightness (`s = d/(mx+mn)`) and therefore reports a large
 *  value for a tiny chroma in the shadows — exactly where noise lives. Chroma is recovered
 *  from the HSL pair without a second min/max pass: `d = s * (1 - |2l - 1|)`, the algebraic
 *  inverse of that formula.
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

        /** The chroma ramp (linear-light units, full scale 1.0). Below the floor a pixel is
         *  treated as neutral and no per-hue adjustment reaches it; above `kChromaFull` the
         *  curve applies in full. The floor is set by MEASUREMENT, not by taste: a flat grey
         *  carrying ±1/255 of per-channel noise reaches ~2/255 = 0.008 of chroma, so a floor
         *  of 0.010 puts that entirely at zero — which is the point, since anything the weight
         *  merely attenuates still fans out when the curve is steep. The full point at 0.040
         *  (~10/255) is where hue is solidly determined; a faint-but-real tint (s≈0.03) lands
         *  mid-ramp and a normal colour is untouched. */
        static constexpr float kChromaFloor = 0.010f;
        static constexpr float kChromaFull = 0.040f;

        ColorMixer();

        /** Set a channel's curve: bezier CONTROL points (hue 0..360, y in [-1,1]); the
         *  curve is cyclic (wraps across the 360/0 seam). The control points are flattened
         *  to a dense polyline (curve::sample) before the LUT is built, so smooth handles
         *  are honoured instead of linearly connecting the bare control points. */
        void setCurve(Channel c, const std::vector<CurvePoint> &points);

        /** True when all three curves are flat at zero: no hue shift, no saturation and
         *  no luminance move, so `rgbToHsl`/`hslToRgb` and the chroma weight never run
         *  (12.29 ms on a 1.7 Mpx preview — R-PREVIEW-6, D-45). */
        bool isIdentity() const override { return mIdentity; }

    protected:
        void processPixel(const Pixel *in, Pixel *out, int channels) override;

    private:
        void rebuild(int c);      // rebuildLut + refreshIdentity
        void rebuildLut(int c);   // fill mLut[c] from mPoints[c]
        float sampleCyclic(int c, float hue) const;  // hue in [0,360) -> interpolated y

        void refreshIdentity();

        std::vector<std::pair<float, float>> mPoints[3];
        float mLut[3][kLut];
        bool mIdentity = true;   // refreshed by refreshIdentity() on every rebuild
    };
}
