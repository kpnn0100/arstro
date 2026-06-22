/*
 *  Arstro ImageProcessing Library
 *
 *  ColorSpace: stateless colour math shared by the processors. The engine works
 *  in linear-light sRGB; these free functions convert to/from gamma-encoded sRGB
 *  (for file ingest/egress and the histogram) and compute luminance.
 *
 *  HSL / Kelvin helpers are added alongside the colour processors that need them.
 */
#pragma once
#include "Pixel.h"
#include "Image.h"

namespace arstro
{
    namespace color
    {
        /** Gamma-encode one linear-light channel value to sRGB (IEC 61966-2-1). */
        Pixel srgbEncode(Pixel linear);

        /** Decode one gamma-encoded sRGB channel value to linear light. */
        Pixel srgbDecode(Pixel encoded);

        /** Convert an Image LinearSRGB -> EncodedSRGB in place (RGB channels only). */
        void encodeInPlace(Image &img);

        /** Convert an Image EncodedSRGB -> LinearSRGB in place (RGB channels only). */
        void decodeInPlace(Image &img);

        /** Rec.709 relative luminance of a linear-light RGB triple. */
        Pixel luminance(Pixel r, Pixel g, Pixel b);
    }
}
