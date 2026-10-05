/*
 *  interstellar_v1 — Scopes: what the SCOPES panel draws, computed from the frame the monitor shows
 *  (R-UI-15). Pure functions over a Raster, so they are tested with fixed inputs.
 *
 *   * WAVEFORM — luma (BT.709) by column: x = the picture's x, y = the level, brightness = how many
 *     pixels of that column sit at that level (log-scaled, so a few clipped pixels still show).
 *   * PARADE — the same per channel, R | G | B side by side, each in its own colour.
 *   * VECTORSCOPE — chroma (Cb, Cr) as a 2-D density: angle is hue, distance from the centre is
 *     saturation. The panel draws the graticule (targets, the skin-tone line) over it.
 *   * The numbers that say what is wrong in words: the share of pixels CLIPPED at white (a channel
 *     at 255) and CRUSHED at black (a channel at 0), per channel; and the LEVELS USED — how many of
 *     the 256 code values each channel actually takes (the least of the three is reported), counted
 *     over every pixel: a graded 8-bit frame using 140 of 256 levels will band.
 *   * The CLIP MASK — an overlay for the monitor: a channel at 255 red, a channel at 0 blue,
 *     transparent elsewhere.
 */
#pragma once
#include "../../core/Raster.h"
#include <algorithm>
#include "../../../../core/ImageProcessing/src/analysis/Histogram.h"

namespace arstro
{
namespace interstellar_v1
{
    struct ScopeData
    {
        bool valid = false;
        HistogramData hist;
        interstellar::Raster waveform;   // kScopeW x kScopeH
        interstellar::Raster parade;     // 3 * kParadeW x kScopeH
        interstellar::Raster vector;     // kVectorN x kVectorN
        double clipHi[3] = {0, 0, 0};    // % of pixels with that channel at 255
        double clipLo[3] = {0, 0, 0};    // % at 0
        int levels[3] = {0, 0, 0};       // distinct code values used per channel
        int width = 0, height = 0;       // the frame measured
        static constexpr int kScopeW = 256, kScopeH = 128, kParadeW = 128, kVectorN = 128;
        int levelsUsed() const { return std::min(levels[0], std::min(levels[1], levels[2])); }
        double clipHiAny() const { return std::max(clipHi[0], std::max(clipHi[1], clipHi[2])); }
        double clipLoAny() const { return std::max(clipLo[0], std::max(clipLo[1], clipLo[2])); }
    };

    ScopeData scopesOf(const interstellar::Raster &frame);
    /** Red where a channel is at 255, blue where a channel is at 0, transparent elsewhere. */
    interstellar::Raster clipMaskOf(const interstellar::Raster &frame);
}
}
