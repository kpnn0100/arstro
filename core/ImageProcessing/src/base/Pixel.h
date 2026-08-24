/*
 *  Arstro ImageProcessing Library
 *  Organization: Arstro
 *
 *  Pixel: the library's pixel/parameter scalar type — the image-domain analogue
 *  of the DSP library's `Sample` (see DigitalSignalProcessing/src/base/Sample.h).
 *
 *  Default is `float` (32-bit). A 24 MP RGBA image is ~192 MB as float vs ~384 MB
 *  as double, and image edits are single-pass point/area ops that do not accumulate
 *  error the way audio feedback loops do — so float is the correct default here,
 *  the mirror-image of the DSP rule (which defaults to double). Define
 *  ARSTRO_USE_DOUBLE_PIXEL for a double-precision build (parity with the DSP
 *  library's ARSTRO_USE_FLOAT escape hatch).
 *
 *  License: MIT License
 */
#pragma once

#ifdef ARSTRO_USE_DOUBLE_PIXEL
typedef double Pixel;
#else
typedef float Pixel;
#endif

namespace arstro
{
    /** Clamp x into the unit range [0, 1]. **NaN maps to 0**, deliberately: this feeds the
     *  float -> 8-bit pack, and `(uint8_t)(NaN * 255 + 0.5)` is undefined — a garbage byte
     *  that differs between builds and reads as a plausible pixel. 0 at least differs the
     *  same way every time, so a NaN reaching a frame is reproducible instead of spooky.
     *
     *  This is a backstop, not the protection: a NaN should never get this far, which is
     *  what the guards in `sampleTf`/`sampleLut` and the non-finite parameter rejection in
     *  `CosmoService::applySetFields` are for (D-36 / D-47a). */
    inline Pixel clamp01(Pixel x)
    {
        return !(x > (Pixel)0) ? (Pixel)0 : (x > (Pixel)1 ? (Pixel)1 : x);
    }

    /** Linear interpolation: a at t=0, b at t=1. */
    inline Pixel lerp(Pixel a, Pixel b, Pixel t) { return a + (b - a) * t; }
}
