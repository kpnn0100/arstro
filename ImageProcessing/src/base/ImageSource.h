/*
 *  Arstro ImageProcessing Library
 *
 *  ImageSource: the starting point of a pipeline — the image-domain analogue of
 *  the DSP library's SignalGenerator. It PRODUCES an Image rather than consuming
 *  one; its input (if any) is ignored. Concrete sources include a decoded file
 *  (FileImageSource), a solid canvas (SolidImageSource), or generated content
 *  (NoiseImageSource).
 */
#pragma once
#include "Image.h"
#include "ImageProcessor.h"

namespace arstro
{
    class ImageSource : public ImageProcessor
    {
    public:
        ImageSource() = default;
        explicit ImageSource(int propertyCount) : ImageProcessor(propertyCount) {}

        /** Produce the source image. */
        virtual Image generate() = 0;

        /** A source ignores its input and emits generate(). */
        void process(const Image & /*in*/, Image &out) override { out = generate(); }
    };
}
