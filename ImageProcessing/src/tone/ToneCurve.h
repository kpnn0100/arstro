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

        /** RGB master control points in [0,1]x[0,1] (display domain). Sorted/clamped here. */
        void setPoints(const std::vector<std::pair<float, float>> &points);
        /** Per-channel control points for channel ch (0=R,1=G,2=B); identity = no-op. */
        void setChannelPoints(int ch, const std::vector<std::pair<float, float>> &points);
        /** true = Log/perceptual (sRGB-encoded) domain (default); false = Linear. */
        void setLogScale(bool log);
        bool logScale() const { return mLog; }

    protected:
        void processPixel(const Pixel *in, Pixel *out, int channels) override;

    private:
        // Clamp/sort a copy of `pts` (identity if empty) and fill `lut` by evaluating
        // the piecewise-linear curve at each entry.
        static void buildLut(std::vector<std::pair<float, float>> pts, Pixel *lut);
        static Pixel sampleLut(const Pixel *lut, Pixel d);  // sample at display coord d in [0,1]

        Pixel mLut[kLut];                      // RGB master
        Pixel mChanLut[kChannels][kLut];       // per-channel R/G/B
        bool mLog = true;
    };
}
