/*
 *  Arstro ImageProcessing Library
 *
 *  FileImageSource: holds an already-decoded Image and emits it. This is the
 *  seam that keeps the engine core dependency-free / WASM-friendly: the actual
 *  file decoding (LibRaw for RAW, stb_image for JPEG/PNG, or the browser on the
 *  web) lives in the APP layer, which hands the decoded pixels to setImage().
 */
#pragma once
#include "../base/ImageSource.h"
#include <cstdint>

namespace arstro
{
    class FileImageSource : public ImageSource
    {
    public:
        FileImageSource() = default;

        /** Store a linear-light image directly. */
        void setImage(const Image &img) { mImage = img.clone(); }

        /** Store decoded, gamma-encoded sRGB bytes (interleaved RGBA8 or RGB8),
         *  converting to a linear-light Image. This is what the app's decoder
         *  feeds in. */
        void setEncodedBytes(const uint8_t *bytes, int width, int height, int channels);

        Image generate() override { return mImage.clone(); }
        const Image &image() const { return mImage; }

    private:
        Image mImage;
    };
}
