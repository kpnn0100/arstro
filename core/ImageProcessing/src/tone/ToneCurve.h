/*
 *  Arstro ImageProcessing Library
 *
 *  ToneCurve: point/parametric tone curves applied via 1024-entry LUTs. Control
 *  points are monotone in x and the curve is evaluated piecewise-linearly between
 *  them (no overshoot). The curve domain toggles between Log (perceptual / sRGB-
 *  encoded — the default, matching how curves are drawn) and Linear.
 *
 *  There are four independent curves: an RGB **master** applied to every colour
 *  channel, plus a **per-channel** curve for R, G and B. For colour channel c the
 *  mapping is `out_c = channel_c( master( x_c ) )` — the master shapes overall
 *  tone, then each channel curve trims that channel. A default (identity) channel
 *  curve is a no-op, so a master-only edit behaves exactly as before.
 *
 *  The control points are not scalar smoothed properties, so they use dedicated
 *  setters rather than the property system.
 */
#pragma once
#include "../base/ImageProcessor.h"
#include "../base/CurvePoint.h"
#include <utility>
#include <vector>

namespace arstro
{
    class ToneCurve : public PointProcessor
    {
    public:
        static constexpr int kLut = 1024;
        static constexpr int kChannels = 3;  // per-channel curves: 0=R, 1=G, 2=B

        ToneCurve();

        /** RGB master bezier control points in [0,1]x[0,1] (display domain). */
        void setPoints(const std::vector<CurvePoint> &points);
        /** Per-channel bezier control points for channel ch (0=R,1=G,2=B); identity = no-op. */
        void setChannelPoints(int ch, const std::vector<CurvePoint> &points);
        /** true = Log/perceptual (sRGB-encoded) domain (default); false = Linear. */
        void setLogScale(bool log);
        bool logScale() const { return mLog; }

        /** True when the master and all three channel curves are the identity, so no
         *  pixel would move. Worth having as a cached flag rather than a scan: this is
         *  the most expensive stage in the pipeline at identity — 28.11 ms on a 1.7 Mpx
         *  preview — because `curveLog` is on by default, so every channel of every
         *  pixel paid an srgbEncode + srgbDecode (two `std::pow`) to look up a straight
         *  line: 10.3 M transcendentals to reproduce the input (R-PREVIEW-6, D-45). */
        bool isIdentity() const override { return mIdentity; }

    protected:
        void processPixel(const Pixel *in, Pixel *out, int channels) override;

    private:
        // Clamp/sort a copy of `pts` (identity if empty) and fill `lut` by evaluating
        // the piecewise-linear curve at each entry.
        static void buildLut(std::vector<std::pair<float, float>> pts, Pixel *lut);
        /** Is `lut` the straight line, to within the error buildLut's own piecewise-
         *  linear evaluation can introduce? Judged on the LUT rather than on the control
         *  points because the points arrive by several routes (two-point identity, empty,
         *  a flattened bezier that happens to be straight) and the LUT is what renders. */
        static bool lutIsIdentity(const Pixel *lut);
        void refreshIdentity();
        static Pixel sampleLut(const Pixel *lut, Pixel d);  // sample at display coord d in [0,1]

        Pixel mLut[kLut];                      // RGB master
        Pixel mChanLut[kChannels][kLut];       // per-channel R/G/B
        bool mLog = true;
        bool mIdentity = true;   // refreshed by refreshIdentity() on every LUT rebuild
    };
}
