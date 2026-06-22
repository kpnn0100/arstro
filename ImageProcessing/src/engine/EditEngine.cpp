#include "EditEngine.h"
#include "../base/ColorSpace.h"
#include <algorithm>

namespace arstro
{
    EditEngine::EditEngine()
    {
        buildPipeline();
    }

    void EditEngine::buildPipeline()
    {
        mPipeline.clear();
        // Reserved canonical order; stages added in later milestones slot in here.
        mPipeline.add(&mExposure);
        mPipeline.add(&mContrast);
    }

    // Decode straight RGBA8/RGB8 (gamma sRGB) into a linear-light Image.
    static Image decodeToLinear(const uint8_t *rgba, int w, int h, int channels)
    {
        Image img(w, h, channels, ColorSpace::EncodedSRGB);
        const size_t n = (size_t)w * h * channels;
        Pixel *d = img.data();
        for (size_t i = 0; i < n; ++i)
            d[i] = (Pixel)rgba[i] / (Pixel)255;
        color::decodeInPlace(img);
        return img;
    }

    int EditEngine::addImage(const uint8_t *rgba, int width, int height, int channels)
    {
        if (!rgba || width <= 0 || height <= 0 || channels < 1)
            return -1;
        Slot s;
        s.source = decodeToLinear(rgba, width, height, channels);
        mSlots.push_back(std::move(s));
        const int slot = (int)mSlots.size() - 1;
        if (mCurrent < 0)
            selectImage(slot);
        return slot;
    }

    void EditEngine::selectImage(int slot)
    {
        if (slot < 0 || slot >= (int)mSlots.size())
            return;
        mCurrent = slot;
        mProxySlot = -1;  // invalidate proxy cache
        applyParamsToProcessors();
    }

    void EditEngine::applyParamsToProcessors()
    {
        // Callers (selectImage / resetAll) guarantee a valid current slot.
        const Params &p = mSlots[mCurrent].params;
        mExposure.setExposureEv((Pixel)p.exposure);
        mContrast.setContrast((Pixel)p.contrast);
    }

    void EditEngine::setPreviewSize(int maxEdge)
    {
        if (maxEdge < 1)
            maxEdge = 1;
        mPreviewMaxEdge = maxEdge;
    }

    void EditEngine::setExposure(float ev)
    {
        if (mCurrent < 0) return;
        mSlots[mCurrent].params.exposure = ev;
        mExposure.setExposureEv((Pixel)ev);
    }

    void EditEngine::setContrast(float v)
    {
        if (mCurrent < 0) return;
        mSlots[mCurrent].params.contrast = v;
        mContrast.setContrast((Pixel)v);
    }

    void EditEngine::setBypass(bool b) { mPipeline.setBypass(b); }

    void EditEngine::resetAll()
    {
        if (mCurrent < 0) return;
        mSlots[mCurrent].params = Params{};
        applyParamsToProcessors();
    }

    // Area-average downscale in linear light (no upscaling: scale clamped to 1).
    static Image downscaleLinear(const Image &src, int maxEdge)
    {
        const int sw = src.width(), sh = src.height();
        const int longEdge = sw > sh ? sw : sh;
        if (longEdge <= maxEdge)
            return src.clone();
        const double scale = (double)maxEdge / (double)longEdge;
        int tw = (int)(sw * scale + 0.5);
        int th = (int)(sh * scale + 0.5);
        if (tw < 1) tw = 1;
        if (th < 1) th = 1;
        const int ch = src.channels();
        Image out(tw, th, ch, src.space());
        for (int ty = 0; ty < th; ++ty)
        {
            const int y0 = (int)((double)ty * sh / th);
            int y1 = (int)((double)(ty + 1) * sh / th);
            if (y1 <= y0) y1 = y0 + 1;
            for (int tx = 0; tx < tw; ++tx)
            {
                const int x0 = (int)((double)tx * sw / tw);
                int x1 = (int)((double)(tx + 1) * sw / tw);
                if (x1 <= x0) x1 = x0 + 1;
                for (int c = 0; c < ch; ++c)
                {
                    double sum = 0.0;
                    int n = 0;
                    for (int y = y0; y < y1; ++y)
                        for (int x = x0; x < x1; ++x)
                        {
                            sum += src.at(x, y, c);
                            ++n;
                        }
                    out.at(tx, ty, c) = (Pixel)(sum / (n > 0 ? n : 1));
                }
            }
        }
        return out;
    }

    void EditEngine::ensurePreviewProxy()
    {
        if (mProxySlot == mCurrent && mProxyEdge == mPreviewMaxEdge)
            return;
        mPreviewProxy = downscaleLinear(mSlots[mCurrent].source, mPreviewMaxEdge);
        mProxySlot = mCurrent;
        mProxyEdge = mPreviewMaxEdge;
    }

    PreviewBuffer EditEngine::renderInto(const Image &linearSource, std::vector<uint8_t> &outBytes)
    {
        Image processed;
        mPipeline.apply(linearSource, processed);

        // Egress: encode linear -> sRGB for display and histogram.
        color::encodeInPlace(processed);
        mLastHistogram = Histogram::compute(processed);

        const int w = processed.width(), h = processed.height();
        const int ch = processed.channels();
        outBytes.assign((size_t)w * h * 4, 255);
        const Pixel *s = processed.data();
        uint8_t *d = outBytes.data();
        for (size_t i = 0; i < (size_t)w * h; ++i)
        {
            const Pixel *p = s + i * ch;
            uint8_t *q = d + i * 4;
            Pixel r = ch >= 1 ? p[0] : 0;
            Pixel g = ch >= 3 ? p[1] : r;
            Pixel b = ch >= 3 ? p[2] : r;
            Pixel a = ch >= 4 ? p[3] : (Pixel)1;
            q[0] = (uint8_t)(clamp01(r) * 255 + 0.5f);
            q[1] = (uint8_t)(clamp01(g) * 255 + 0.5f);
            q[2] = (uint8_t)(clamp01(b) * 255 + 0.5f);
            q[3] = (uint8_t)(clamp01(a) * 255 + 0.5f);
        }
        PreviewBuffer pb;
        pb.rgba = outBytes.data();
        pb.width = w;
        pb.height = h;
        return pb;
    }

    PreviewBuffer EditEngine::renderPreview()
    {
        if (mCurrent < 0)
            return PreviewBuffer{};
        ensurePreviewProxy();
        return renderInto(mPreviewProxy, mPreviewOut);
    }

    PreviewBuffer EditEngine::renderFull()
    {
        if (mCurrent < 0)
            return PreviewBuffer{};
        return renderInto(mSlots[mCurrent].source, mFullOut);
    }
}
