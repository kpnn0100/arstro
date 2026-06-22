/*
 *  Arstro ImageProcessing Library
 *
 *  Histogram: an analysis SINK (not an ImageProcessor) — it reads an image and
 *  reports tonal distribution, the way the DSP library keeps analysis out of the
 *  processing chain. 256 bins per channel (R/G/B + luminance), computed from the
 *  DISPLAY-referred (gamma-encoded sRGB) values so the bars match what the eye
 *  and the UI see. A linear-light image is encoded on the fly during binning
 *  (the source image is never mutated).
 */
#pragma once
#include "../base/Image.h"
#include <array>
#include <cstdint>

namespace arstro
{
    struct HistogramData
    {
        static constexpr int kBins = 256;
        std::array<uint32_t, kBins> r{};
        std::array<uint32_t, kBins> g{};
        std::array<uint32_t, kBins> b{};
        std::array<uint32_t, kBins> lum{};
        uint32_t maxCount = 0;  ///< largest bin across all channels (for normalising a plot)
    };

    class Histogram
    {
    public:
        /** Compute the histogram of `image` (encoded on the fly if linear). */
        static HistogramData compute(const Image &image);

        /** Fill a normalised [0,1] log-scaled readout (rows: 0=r,1=g,2=b,3=lum). */
        static void toLog(const HistogramData &in, float out[4][HistogramData::kBins]);

        /** Fill a normalised [0,1] linear-scaled readout (rows: 0=r,1=g,2=b,3=lum). */
        static void toLinear(const HistogramData &in, float out[4][HistogramData::kBins]);
    };
}
