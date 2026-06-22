/*
 *  Arstro ImageProcessing Library
 *
 *  Image: the pixel buffer that flows through the processing pipeline — the
 *  image-domain analogue of an audio sample stream. A processor consumes one
 *  Image and produces another (point ops keep the dimensions; geometry ops
 *  such as Crop/Rotate change them).
 *
 *  Layout: contiguous, interleaved, row-major, no padding. Channels are 3 (RGB)
 *  or 4 (RGBA). The 4th (alpha) channel is carried through untouched by colour
 *  ops and is never colour-managed.
 *
 *  Colour space: an Image is tagged LinearSRGB or EncodedSRGB. The engine works
 *  in LINEAR light (so +1 EV doubles values, white balance is a channel gain,
 *  and any averaging — blur/downscale — is physically correct), decoding sRGB to
 *  linear once at ingest and encoding back once at egress. See base/ColorSpace.h.
 */
#pragma once
#include "Pixel.h"
#include <vector>
#include <cstdint>
#include <cstddef>

namespace arstro
{
    enum class ColorSpace : uint8_t
    {
        LinearSRGB,  ///< linear-light, sRGB primaries (the working space)
        EncodedSRGB  ///< gamma-encoded sRGB (display-referred: files, histogram, UI)
    };

    class Image
    {
    public:
        Image() = default;
        Image(int width, int height, int channels,
              ColorSpace space = ColorSpace::LinearSRGB);

        // ── allocation / lifecycle ──
        void allocate(int width, int height, int channels, ColorSpace space);
        void resizeLike(const Image &other);  ///< match dims+channels (reuses buffer)
        Image clone() const;                   ///< deep copy
        bool empty() const { return mData.empty(); }

        // ── geometry / metadata ──
        int width() const { return mWidth; }
        int height() const { return mHeight; }
        int channels() const { return mChannels; }
        size_t pixelCount() const { return (size_t)mWidth * (size_t)mHeight; }
        ColorSpace space() const { return mSpace; }
        void setSpace(ColorSpace s) { mSpace = s; }  ///< metadata only; see ColorSpace.h to convert

        // ── access (interleaved, row-major) ──
        Pixel *data() { return mData.data(); }
        const Pixel *data() const { return mData.data(); }
        Pixel *row(int y) { return mData.data() + (size_t)y * mWidth * mChannels; }
        const Pixel *row(int y) const { return mData.data() + (size_t)y * mWidth * mChannels; }

        Pixel at(int x, int y, int c) const
        {
            return mData[(((size_t)y * mWidth + x) * mChannels) + c];
        }
        Pixel &at(int x, int y, int c)
        {
            return mData[(((size_t)y * mWidth + x) * mChannels) + c];
        }

        /** Bilinear sample of channel c at fractional pixel coords (clamped edges). */
        Pixel sampleBilinear(float x, float y, int c) const;

    private:
        int mWidth = 0, mHeight = 0, mChannels = 0;
        ColorSpace mSpace = ColorSpace::LinearSRGB;
        std::vector<Pixel> mData;  ///< size = width * height * channels
    };
}
