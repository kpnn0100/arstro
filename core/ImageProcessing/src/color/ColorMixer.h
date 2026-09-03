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
 *  y maps to: Hue -> hue += y*180 deg; Sat -> s *= (1 + y); Lum -> a GAIN of 2^(y*1.5)
 *  applied to the pixel itself (R-MIXER-10..13).
 *
 *  ...and the Lum channel is the odd one out on purpose. It was `l += y*0.5` in HSL until
 *  2026-09-03, and that was three faults in one line: ADDITIVE, so every pixel of a hue moved
 *  by the same absolute amount and the tonal relationships inside it collapsed; applied to
 *  LINEAR light, where HSL lightness is not perceptual and mid-grey is 0.214, so `-0.5` was
 *  below zero and clamped to BLACK; and through HSL, whose `l` and `s` are coupled, so the
 *  saturation wandered on the way past. Measured, on a blue sky: -100 came out pure black, -50
 *  crushed four of five steps of a gradient to black, +25 pushed saturation 0.505 -> 0.874, and
 *  +100 was very nearly white.
 *
 *  What replaces it is Adobe's: the DNG spec's HueSatMap stores a hue shift, a saturation
 *  SCALE and a value SCALE, applied in HSV. And scaling V in HSV is exactly scaling the linear
 *  RGB triple by a constant — V is the max channel, hue and HSV saturation are ratios of the
 *  channels — so the right implementation is a multiply, and hue and saturation are then
 *  preserved by construction rather than by care.
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
 *
 *  ...and that weight is then SPREAD over a neighbourhood (R-MIXER-5..8), which is what turns a
 *  correct refusal into a correct answer. Refusing the noise pixel stopped the speckle but left the
 *  grain inside a red flower untouched while the flower moved, and left the BOKEH behind a subject
 *  behind entirely — a defocused green is a smeared green, and a per-pixel chroma gate reads it as
 *  neutral. Whether a pixel belongs to a colour is a question about its neighbourhood.
 *
 *  So each channel's `w * y(hue)` is built as a plane, softened by `mixerSpread`, and the pixel is
 *  moved by whichever of its own and the softened value is LARGER IN MAGNITUDE. Larger-wins, not
 *  the softened value outright: a plain blur would dilute the effect at the edge of any region
 *  smaller than the radius, so a small red flower would move LESS than it does today. The spread
 *  may only add reach, never remove it.
 */
#pragma once
#include "../base/CurvePoint.h"
#include "../base/Image.h"
#include "../base/ImageProcessor.h"
#include <utility>
#include <vector>

namespace arstro
{
    /** Not a `PointProcessor` any more, and that is the whole of R-MIXER-5: the answer for a
     *  pixel depends on its neighbours, so the stage owns its own two-pass loop. With
     *  `spread == 0` the two passes collapse to the one the point op ran, byte for byte. */
    class ColorMixer : public ImageProcessor
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

        /** Blur sigma at spread == 1, as a fraction of the image's SHORT EDGE. Relative and not
         *  a pixel count because cosmo renders the same edit at 200/400/800/1600 and full
         *  resolution (R-PREVIEW): a radius in pixels would make the preview stop predicting the
         *  export at the moment the photographer is judging colour. 0.4% is ~4 px on a 1000 px
         *  preview and ~16 px on a 4000 px export — a spread, not a smear. */
        static constexpr float kSpreadFraction = 0.004f;
        /** Below half a pixel the blur cannot move anything, so the spread path is skipped and
         *  the plain per-pixel one runs — which is also the exact old behaviour. */
        static constexpr float kMinSpreadSigma = 0.5f;

        /** Stops of gain at `y == 1` (R-MIXER-12). Stops rather than a percentage because a gain
         *  composes multiplicatively and a photographer already reads brightness that way — the
         *  same slider move does the same PERCEPTUAL thing to a shadow and to a highlight, which
         *  is the property the old additive version could not have at any setting. */
        static constexpr float kLumStops = 1.5f;

        /** Where the highlight shoulder starts, in linear light (R-MIXER-13). 0.75 linear is
         *  ~0.89 sRGB, so the gain is EXACT over the whole range a photograph actually lives in
         *  and only the top tenth is rolled off. */
        static constexpr float kShoulderKnee = 0.75f;

        /** Resolve a requested `gain` for a pixel whose brightest linear channel is `v` into the
         *  two things that actually happen to it (R-MIXER-13): a uniform `scale`, and — only
         *  when the scale alone could not deliver the gain — a pull `toNeutral` toward the
         *  pixel's own new brightness.
         *
         *  **Both preserve hue exactly**, and that is the point of splitting it this way. A
         *  uniform scale obviously does. So does the pull: it maps every channel's ratio to the
         *  max as `c/v -> (c/v)(1-d) + d`, an affine map applied identically to all three, so the
         *  RATIOS OF THE DIFFERENCES that define hue come out unchanged and only the saturation
         *  moves. A colour driven to the top therefore goes white through its own hue.
         *
         *  **The pull is not optional decoration; without it the + side does not work.** A pure
         *  scale cannot brighten a colour whose max channel is already at 1 — it stalls, and a
         *  saturated blue sky pushed to +100 comes back the same saturated blue. In a bounded
         *  display space you cannot make a saturated colour brighter without making it paler,
         *  which is also what happens when you add light to one.
         *
         *  `shoulder(v)` is in the denominator of the scale and not `v`, which is what makes
         *  `gain == 1` exactly identity: dividing by `v` would compress every highlight the
         *  moment the curve left zero by any amount — a discontinuity at the setting a
         *  photographer passes through most often. */
        static void lumAdjust(float v, float gain, float &scale, float &toNeutral);

        ColorMixer();

        /** Set a channel's curve: bezier CONTROL points (hue 0..360, y in [-1,1]); the
         *  curve is cyclic (wraps across the 360/0 seam). The control points are flattened
         *  to a dense polyline (curve::sample) before the LUT is built, so smooth handles
         *  are honoured instead of linearly connecting the bare control points. */
        void setCurve(Channel c, const std::vector<CurvePoint> &points);

        /** Neighbourhood spread of the per-hue selection, 0..1 (the engine maps EditParams'
         *  0..100). 0 is exactly the per-pixel behaviour and allocates nothing. */
        void setSpread(float amount01);
        float spread() const { return mSpread; }

        /** True when all three curves are flat at zero: no hue shift, no saturation and
         *  no luminance move, so `rgbToHsl`/`hslToRgb` and the chroma weight never run
         *  (12.29 ms on a 1.7 Mpx preview — R-PREVIEW-6, D-45). */
        bool isIdentity() const override { return mIdentity; }

        void process(const Image &in, Image &out) override;

    private:
        /** The per-pixel kernel, unchanged from the PointProcessor days. `adj` is the three
         *  channels' already-weighted y values; passing them in is what lets the spread path
         *  substitute a softened plane for the pixel's own answer. */
        void applyAdjust(const Pixel *in, Pixel *out, int channels, const float adj[3]) const;
        /** `w * y(hue)` for each channel of one pixel — the value the spread plane holds. */
        void weightedAdjust(const Pixel *in, int channels, float adj[3]) const;

        void rebuild(int c);      // rebuildLut + refreshIdentity
        void rebuildLut(int c);   // fill mLut[c] from mPoints[c]
        float sampleCyclic(int c, float hue) const;  // hue in [0,360) -> interpolated y

        void refreshIdentity();

        std::vector<std::pair<float, float>> mPoints[3];
        float mLut[3][kLut];
        bool mIdentity = true;      // refreshed by refreshIdentity() on every rebuild
        bool mFlat[3] = {true, true, true};  ///< per-channel: LUT is all zero, so no plane for it
        float mSpread = 0.f;        // 0..1
    };
}
