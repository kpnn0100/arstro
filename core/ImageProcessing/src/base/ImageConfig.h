/*
 *  Arstro ImageProcessing Library
 *
 *  ImageConfig: single source of truth for global image-engine settings — the
 *  image-domain analogue of the DSP library's AudioConfig (Meyers singleton).
 *  Holds the working colour space, the interactive preview size cap, and whether
 *  outputs are clamped to the unit range. Changing the working space notifies
 *  every live ImageProcessor (onConfigChanged).
 */
#pragma once
#include "Image.h"

namespace arstro
{
    class ImageConfig
    {
    public:
        static ImageConfig &instance();

        ColorSpace workingSpace() const { return mWorking; }
        int maxPreviewEdge() const { return mMaxPreviewEdge; }
        bool clampToUnit() const { return mClamp; }

        void setWorkingSpace(ColorSpace s);  ///< notifies all processors
        void setMaxPreviewEdge(int px);
        void setClampToUnit(bool b) { mClamp = b; }

    private:
        ImageConfig() = default;
        ImageConfig(const ImageConfig &) = delete;
        ImageConfig &operator=(const ImageConfig &) = delete;

        ColorSpace mWorking = ColorSpace::LinearSRGB;
        int mMaxPreviewEdge = 2048;
        bool mClamp = true;
    };
}
