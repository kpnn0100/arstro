#include "Image.h"

namespace arstro
{
    Image::Image(int width, int height, int channels, ColorSpace space)
    {
        allocate(width, height, channels, space);
    }

    void Image::allocate(int width, int height, int channels, ColorSpace space)
    {
        if (width < 0) width = 0;
        if (height < 0) height = 0;
        if (channels < 0) channels = 0;
        mWidth = width;
        mHeight = height;
        mChannels = channels;
        mSpace = space;
        mData.assign((size_t)width * height * channels, (Pixel)0);
    }

    void Image::resizeLike(const Image &other)
    {
        // Reuse the existing buffer when the size already matches.
        const size_t need = (size_t)other.mWidth * other.mHeight * other.mChannels;
        mWidth = other.mWidth;
        mHeight = other.mHeight;
        mChannels = other.mChannels;
        mSpace = other.mSpace;
        if (mData.size() != need)
            mData.resize(need);
    }

    Image Image::clone() const
    {
        Image out;
        out.mWidth = mWidth;
        out.mHeight = mHeight;
        out.mChannels = mChannels;
        out.mSpace = mSpace;
        out.mData = mData;
        return out;
    }

    Pixel Image::sampleBilinear(float fx, float fy, int c) const
    {
        if (mWidth <= 0 || mHeight <= 0)
            return (Pixel)0;
        // Clamp to the valid sampling range.
        if (fx < 0.0f) fx = 0.0f;
        if (fy < 0.0f) fy = 0.0f;
        const float maxX = (float)(mWidth - 1);
        const float maxY = (float)(mHeight - 1);
        if (fx > maxX) fx = maxX;
        if (fy > maxY) fy = maxY;

        const int x0 = (int)fx;
        const int y0 = (int)fy;
        const int x1 = x0 < mWidth - 1 ? x0 + 1 : x0;
        const int y1 = y0 < mHeight - 1 ? y0 + 1 : y0;
        const float tx = fx - (float)x0;
        const float ty = fy - (float)y0;

        const Pixel p00 = at(x0, y0, c);
        const Pixel p10 = at(x1, y0, c);
        const Pixel p01 = at(x0, y1, c);
        const Pixel p11 = at(x1, y1, c);
        const Pixel top = lerp(p00, p10, (Pixel)tx);
        const Pixel bot = lerp(p01, p11, (Pixel)tx);
        return lerp(top, bot, (Pixel)ty);
    }
}
