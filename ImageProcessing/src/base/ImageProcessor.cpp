#include "ImageProcessor.h"
#include <algorithm>

namespace arstro
{
    std::vector<ImageProcessor *> ImageProcessor::sInstances;

    ImageProcessor::ImageProcessor() : ImageProcessor(0) {}

    ImageProcessor::ImageProcessor(int propertyCount)
    {
        mParams.resize(propertyCount);
        sInstances.push_back(this);
    }

    ImageProcessor::~ImageProcessor()
    {
        for (size_t i = 0; i < sInstances.size(); ++i)
        {
            if (sInstances[i] == this)
            {
                sInstances.erase(sInstances.begin() + i);
                break;
            }
        }
    }

    void ImageProcessor::initProperty(int id, Pixel value)
    {
        mParams.init(id, value);
    }

    void ImageProcessor::setProperty(int id, Pixel value)
    {
        if (mParams.target(id) == value)
            return;
        mParams.beginRamp();          // anchor a ramp at the current value
        mParams.setTarget(id, value);
        if (!mSmoothEnable)
            mParams.snap(id);         // stills: take effect immediately
        update();                     // recompute derived state
        onPropertyChanged(id, value);
    }

    Pixel ImageProcessor::getProperty(int id) { return mParams.current(id); }
    Pixel ImageProcessor::getPropertyTargetValue(int id) { return mParams.target(id); }

    void ImageProcessor::apply(const Image &in, Image &out)
    {
        if (!mSmoothEnable)
            mParams.snapAll();
        update();
        if (mBypass)
        {
            out.resizeLike(in);
            const size_t n = (size_t)in.width() * in.height() * in.channels();
            if (n)
                std::copy(in.data(), in.data() + n, out.data());
            out.setSpace(in.space());
            return;
        }
        process(in, out);
    }

    Image ImageProcessor::apply(const Image &in)
    {
        Image out;
        apply(in, out);
        return out;
    }

    void ImageProcessor::update() {}
    void ImageProcessor::prepare() {}
    void ImageProcessor::onPropertyChanged(int, Pixel) {}
    void ImageProcessor::onConfigChanged() {}

    void ImageProcessor::notifyConfigChanged()
    {
        for (auto *p : sInstances)
            p->onConfigChanged();
    }

    // ── PointProcessor ──
    void PointProcessor::process(const Image &in, Image &out)
    {
        out.resizeLike(in);
        const int ch = in.channels();
        const size_t px = in.pixelCount();
        const Pixel *s = in.data();
        Pixel *d = out.data();
        for (size_t i = 0; i < px; ++i)
            processPixel(s + i * ch, d + i * ch, ch);
    }
}
