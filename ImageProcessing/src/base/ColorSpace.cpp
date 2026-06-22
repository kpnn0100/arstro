#include "ColorSpace.h"
#include <cmath>

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
    }
}
