#include "ColorSpace.h"
#include "Parallel.h"
#include <cmath>
#include <algorithm>

namespace arstro
{
    namespace color
    {
        Pixel srgbEncodeExact(Pixel linear)
        {
            if (linear <= (Pixel)0)
                return (Pixel)0;
            if (linear >= (Pixel)1)
                return (Pixel)1;
            if (linear <= (Pixel)0.0031308)
                return (Pixel)12.92 * linear;
            return (Pixel)1.055 * (Pixel)std::pow((double)linear, 1.0 / 2.4) - (Pixel)0.055;
        }

        Pixel srgbDecodeExact(Pixel encoded)
        {
            if (encoded <= (Pixel)0)
                return (Pixel)0;
            if (encoded >= (Pixel)1)
                return (Pixel)1;
            if (encoded <= (Pixel)0.04045)
                return encoded / (Pixel)12.92;
            return (Pixel)std::pow(((double)encoded + 0.055) / 1.055, 2.4);
        }

        namespace
        {
            // 4096 knots + linear interpolation, built once from the closed form. The size
            // is set by ERROR, not by taste: linear-interpolation error is bounded by
            // |f''|max * h^2 / 8, and the encode direction's second derivative peaks at the
            // 0.0031308 knee where it is ~2.4e3, giving ~1.8e-5 across a cell of 1/4095.
            // Everything below that knee is exactly linear, so the first thirteen cells are
            // interpolated with no error at all — which matters, because that is where a
            // uniform table over a power function would otherwise be at its worst.
            //
            // 16 KB per table, so both sit in L1 alongside the pixels being streamed. A
            // steeper table would not buy accuracy anyone can see: 1.8e-5 is 1/218th of one
            // 8-bit step, and every consumer of these functions quantises to 8 bits or bins
            // to 256 buckets immediately afterwards.
            constexpr int kTfLut = 4096;

            struct TransferTables
            {
                Pixel encode[kTfLut];
                Pixel decode[kTfLut];
                TransferTables()
                {
                    for (int i = 0; i < kTfLut; ++i)
                    {
                        const Pixel x = (Pixel)i / (Pixel)(kTfLut - 1);
                        encode[i] = srgbEncodeExact(x);
                        decode[i] = srgbDecodeExact(x);
                    }
                }
            };

            // Function-local static: built once, thread-safe initialisation, and no static
            // initialisation order to reason about (arstro_image is a library).
            const TransferTables &tables()
            {
                static const TransferTables t;
                return t;
            }

            inline Pixel sampleTf(const Pixel *lut, Pixel x)
            {
                // `!(x > 0)` rather than `x <= 0`, and `!(x < 1)` rather than `x >= 1`:
                // **every comparison with NaN is false**, so the obvious spelling lets a NaN
                // straight through, `(int)(NaN * 4095)` is undefined (INT_MIN in practice)
                // and `lut[INT_MIN]` reads wild memory. That is not hypothetical — it is
                // D-47a, a segfault in a render worker while the user dragged an exposure
                // slider, and it is the same bug D-36 filed against `ToneCurve::sampleLut`.
                // Turning these two functions into tables is what made a NaN fatal instead
                // of merely wrong, so the guard belongs here whatever produced the NaN.
                if (!(x > (Pixel)0)) return (Pixel)0;    // negatives AND NaN
                if (!(x < (Pixel)1)) return (Pixel)1;
                const Pixel f = x * (Pixel)(kTfLut - 1);
                int i = (int)f;
                // Clamped anyway. The guards above already bound `x`, so this cannot trigger
                // today — and that is the point: an index derived from a float must be
                // clamped where it is USED, not trusted because something upstream checked
                // the input. Every crash in this family has been an upstream check that did
                // not hold for one value.
                if (i < 0) i = 0;
                if (i > kTfLut - 2) i = kTfLut - 2;
                const Pixel frac = f - (Pixel)i;
                return lut[i] + (lut[i + 1] - lut[i]) * frac;
            }
        }

        Pixel srgbEncode(Pixel linear) { return sampleTf(tables().encode, linear); }
        Pixel srgbDecode(Pixel encoded) { return sampleTf(tables().decode, encoded); }

        void encodeInPlace(Image &img)
        {
            if (img.space() == ColorSpace::EncodedSRGB)
                return;
            const int ch = img.channels();
            const int colorCh = ch >= 3 ? 3 : ch;  // leave alpha untouched
            Pixel *d = img.data();
            const int h = img.height(), rowN = img.width() * ch;
            // Row-independent sRGB encode — parallelised; this runs on every preview
            // render, so serialising it stalls switching + edits. Each call is now a
            // table lookup rather than a `pow` (R-PREVIEW-6).
            par::parallelFor(h, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y)
                {
                    Pixel *r = d + (size_t)y * rowN;
                    for (int x = 0; x < rowN; x += ch)
                        for (int c = 0; c < colorCh; ++c)
                            r[x + c] = srgbEncode(r[x + c]);
                }
            });
            img.setSpace(ColorSpace::EncodedSRGB);
        }

        void decodeInPlace(Image &img)
        {
            if (img.space() == ColorSpace::LinearSRGB)
                return;
            const int ch = img.channels();
            const int colorCh = ch >= 3 ? 3 : ch;
            Pixel *d = img.data();
            const int h = img.height(), rowN = img.width() * ch;
            // Row-independent sRGB→linear decode, parallelised (the dominant cost of
            // decoding an image into the engine on load). Table-driven (R-PREVIEW-6);
            // note the 8-bit ingest path has its own exact 256-entry table in
            // EditEngine's `kSrgbToLinear`, which this does not replace.
            par::parallelFor(h, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y)
                {
                    Pixel *r = d + (size_t)y * rowN;
                    for (int x = 0; x < rowN; x += ch)
                        for (int c = 0; c < colorCh; ++c)
                            r[x + c] = srgbDecode(r[x + c]);
                }
            });
            img.setSpace(ColorSpace::LinearSRGB);
        }

        Pixel luminance(Pixel r, Pixel g, Pixel b)
        {
            return (Pixel)0.2126 * r + (Pixel)0.7152 * g + (Pixel)0.0722 * b;
        }

        void rgbToHsl(Pixel r, Pixel g, Pixel b, Pixel &h, Pixel &s, Pixel &l)
        {
            const Pixel mx = std::max(r, std::max(g, b));
            const Pixel mn = std::min(r, std::min(g, b));
            const Pixel d = mx - mn;
            l = (mx + mn) * (Pixel)0.5;
            if (d <= (Pixel)1e-9)
            {
                h = 0;
                s = 0;
                return;
            }
            s = l > (Pixel)0.5 ? d / ((Pixel)2 - mx - mn) : d / (mx + mn);
            Pixel hh;
            if (mx == r) hh = (g - b) / d + (g < b ? (Pixel)6 : (Pixel)0);
            else if (mx == g) hh = (b - r) / d + (Pixel)2;
            else hh = (r - g) / d + (Pixel)4;
            h = hh * (Pixel)60;
            if (h >= (Pixel)360) h -= (Pixel)360;  // canonical [0,360)
        }

        static Pixel hue2rgb(Pixel p, Pixel q, Pixel t)
        {
            if (t < 0) t += 1;
            if (t > 1) t -= 1;
            if (t < (Pixel)1 / 6) return p + (q - p) * (Pixel)6 * t;
            if (t < (Pixel)1 / 2) return q;
            if (t < (Pixel)2 / 3) return p + (q - p) * ((Pixel)2 / 3 - t) * (Pixel)6;
            return p;
        }

        void hslToRgb(Pixel h, Pixel s, Pixel l, Pixel &r, Pixel &g, Pixel &b)
        {
            if (s <= (Pixel)1e-9)
            {
                r = g = b = l;
                return;
            }
            const Pixel hn = h / (Pixel)360;
            const Pixel q = l < (Pixel)0.5 ? l * ((Pixel)1 + s) : l + s - l * s;
            const Pixel p = (Pixel)2 * l - q;
            r = hue2rgb(p, q, hn + (Pixel)1 / 3);
            g = hue2rgb(p, q, hn);
            b = hue2rgb(p, q, hn - (Pixel)1 / 3);
        }

        void kelvinToRgbGain(Pixel kelvin, Pixel tint, Pixel &gr, Pixel &gg, Pixel &gb)
        {
            // Heuristic, monotonic warm/cool around the 6500 K working white.
            double w = ((double)kelvin - 6500.0) / 6500.0;
            if (w > 2.0) w = 2.0;
            if (w < -1.0) w = -1.0;
            const double t = (double)tint / 150.0;  // -1 (green) .. +1 (magenta)
            gr = (Pixel)(1.0 + 0.45 * w);
            gb = (Pixel)(1.0 - 0.45 * w);
            gg = (Pixel)(1.0 - 0.30 * t);
            // Normalize so a neutral grey keeps its luminance.
            const Pixel lum = luminance(gr, gg, gb);
            if (lum > (Pixel)1e-6)
            {
                gr /= lum;
                gg /= lum;
                gb /= lum;
            }
        }

        Pixel hueDelta(Pixel a, Pixel b)
        {
            Pixel d = std::fmod(b - a + (Pixel)540, (Pixel)360) - (Pixel)180;
            return d;
        }
    }
}
