/*
 *  Arstro ImageProcessing Library
 *
 *  ColorSpace: stateless colour math shared by the processors. The engine works
 *  in linear-light sRGB; these free functions convert to/from gamma-encoded sRGB
 *  (for file ingest/egress and the histogram) and compute luminance.
 *
 *  HSL / Kelvin helpers are added alongside the colour processors that need them.
 *
 *  The two transfer functions are TABLE-DRIVEN (R-PREVIEW-6). They are the hottest
 *  scalar functions in the library — a preview render calls them per colour channel
 *  per pixel from encodeInPlace, from all three histogram taps and, at the default
 *  `curveLog` domain, twice more from ToneCurve — and `std::pow(double, 1/2.4)` per
 *  call was ~39 ms of a 1600 px render on a 24-thread desktop. `srgbExact*()` keeps
 *  the closed form available as the reference the accuracy test asserts against.
 */
#pragma once
#include "Pixel.h"
#include "Image.h"

namespace arstro
{
    namespace color
    {
        /** Gamma-encode one linear-light channel value to sRGB (IEC 61966-2-1).
         *  Table-driven: a 4096-entry LUT with linear interpolation, whose worst-case
         *  error is ~2e-5 — two orders of magnitude below one 8-bit step (1/255). */
        Pixel srgbEncode(Pixel linear);

        /** Decode one gamma-encoded sRGB channel value to linear light. Table-driven
         *  as above; the decode direction is far gentler, worst case ~1e-7. */
        Pixel srgbDecode(Pixel encoded);

        /** The closed-form transfer functions, straight from IEC 61966-2-1. These are
         *  the REFERENCE the tables are built from and measured against — call them
         *  when correctness matters more than throughput, not in a per-pixel loop. */
        Pixel srgbEncodeExact(Pixel linear);
        Pixel srgbDecodeExact(Pixel encoded);

        /** How many non-finite values the transfer functions have had to substitute, since
         *  the last `takeNonFiniteCount()`. **This exists because the fix for D-48 made the
         *  failure quiet.** A NaN pixel used to crash the process, which at least reported
         *  itself; now it becomes 0 and nobody would ever know. So the guard counts, the
         *  engine reads the count per render, and `frame.ready` says so — a NaN reaching a
         *  frame is now a log line instead of either a core dump or a mystery.
         *
         *  Free in the normal case: the increment sits on the branch that is not taken, and
         *  is relaxed because the number is a diagnostic, not a decision. */
        unsigned long long takeNonFiniteCount();

        /** Convert an Image LinearSRGB -> EncodedSRGB in place (RGB channels only). */
        void encodeInPlace(Image &img);

        /** Convert an Image EncodedSRGB -> LinearSRGB in place (RGB channels only). */
        void decodeInPlace(Image &img);

        /** Rec.709 relative luminance of a linear-light RGB triple. */
        Pixel luminance(Pixel r, Pixel g, Pixel b);

        /** RGB (0..1) -> HSL with hue in [0,360), saturation/lightness in [0,1]. */
        void rgbToHsl(Pixel r, Pixel g, Pixel b, Pixel &h, Pixel &s, Pixel &l);
        /** HSL (hue [0,360), s/l in [0,1]) -> RGB (0..1). */
        void hslToRgb(Pixel h, Pixel s, Pixel l, Pixel &r, Pixel &g, Pixel &b);

        /** Per-channel white-balance gains for a temperature (Kelvin) + tint
         *  (green<->magenta), normalized so a neutral grey keeps its luminance. */
        void kelvinToRgbGain(Pixel kelvin, Pixel tint, Pixel &gr, Pixel &gg, Pixel &gb);

        /** Smallest signed angular delta from hue a to hue b, in degrees [-180,180]. */
        Pixel hueDelta(Pixel a, Pixel b);
    }
}
