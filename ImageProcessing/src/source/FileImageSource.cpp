#include "FileImageSource.h"
#include "../base/ColorSpace.h"

namespace arstro
{
    void FileImageSource::setEncodedBytes(const uint8_t *bytes, int width, int height, int channels)
    {
        if (!bytes || width <= 0 || height <= 0 || channels < 1)
        {
            mImage = Image();
            return;
        }
        mImage.allocate(width, height, channels, ColorSpace::EncodedSRGB);
        const size_t n = (size_t)width * height * channels;
        Pixel *d = mImage.data();
        for (size_t i = 0; i < n; ++i)
            d[i] = (Pixel)bytes[i] / (Pixel)255;
        // Bytes are gamma sRGB; convert to the linear working space.
        color::decodeInPlace(mImage);
    }
}
