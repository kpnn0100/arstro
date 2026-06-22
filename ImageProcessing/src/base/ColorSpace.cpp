#include "ColorSpace.h"
#include <cmath>
#include <algorithm>

namespace arstro
{
    namespace color
    {
        Pixel srgbEncode(Pixel linear)
        {
            if (linear <= (Pixel)0)
                return (Pixel)0;
            if (linear >= (Pixel)1)
                return (Pixel)1;
            if (linear <= (Pixel)0.0031308)
                return (Pixel)12.92 * linear;
            return (Pixel)1.055 * (Pixel)std::pow((double)linear, 1.0 / 2.4) - (Pixel)0.055;
        }

        Pixel srgbDecode(Pixel encoded)
        {
            if (encoded <= (Pixel)0)
                return (Pixel)0;
            if (encoded >= (Pixel)1)
                return (Pixel)1;
            if (encoded <= (Pixel)0.04045)
                return encoded / (Pixel)12.92;
            return (Pixel)std::pow(((double)encoded + 0.055) / 1.055, 2.4);
        }

        void encodeInPlace(Image &img)
        {
            if (img.space() == ColorSpace::EncodedSRGB)
                return;
            const int ch = img.channels();
            const int colorCh = ch >= 3 ? 3 : ch;  // leave alpha untouched
            Pixel *d = img.data();
            const size_t px = img.pixelCount();
            for (size_t i = 0; i < px; ++i)
            {
                Pixel *p = d + i * ch;
                for (int c = 0; c < colorCh; ++c)
                    p[c] = srgbEncode(p[c]);
            }
            img.setSpace(ColorSpace::EncodedSRGB);
        }

        void decodeInPlace(Image &img)
        {
            if (img.space() == ColorSpace::LinearSRGB)
                return;
            const int ch = img.channels();
            const int colorCh = ch >= 3 ? 3 : ch;
            Pixel *d = img.data();
            const size_t px = img.pixelCount();
            for (size_t i = 0; i < px; ++i)
            {
                Pixel *p = d + i * ch;
                for (int c = 0; c < colorCh; ++c)
                    p[c] = srgbDecode(p[c]);
            }
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
