#include "ImageConfig.h"
#include "ImageProcessor.h"

namespace arstro
{
    ImageConfig &ImageConfig::instance()
    {
        static ImageConfig sInstance;
        return sInstance;
    }

    void ImageConfig::setWorkingSpace(ColorSpace s)
    {
        mWorking = s;
        ImageProcessor::notifyConfigChanged();
    }

    void ImageConfig::setMaxPreviewEdge(int px)
    {
        if (px < 1)
            px = 1;
        mMaxPreviewEdge = px;
    }
}
