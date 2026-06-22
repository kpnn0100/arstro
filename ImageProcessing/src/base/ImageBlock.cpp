#include "ImageBlock.h"
#include <algorithm>

namespace arstro
{
    void ImageBlock::add(ImageProcessor *p)
    {
        if (p)
            mChain.push_back(p);
    }

    void ImageBlock::remove(ImageProcessor *p)
    {
        mChain.erase(std::remove(mChain.begin(), mChain.end(), p), mChain.end());
    }

    void ImageBlock::clear() { mChain.clear(); }

    void ImageBlock::prepare()
    {
        for (auto *p : mChain)
            if (p)
                p->prepare();
    }

    void ImageBlock::process(const Image &in, Image &out)
    {
        // Active stages only (a bypassed stage is skipped, not copied through).
        std::vector<ImageProcessor *> active;
        active.reserve(mChain.size());
        for (auto *p : mChain)
            if (p && !p->isBypassed())
                active.push_back(p);

        if (active.empty())
        {
            out.resizeLike(in);
            const size_t n = (size_t)in.width() * in.height() * in.channels();
            if (n)
                std::copy(in.data(), in.data() + n, out.data());
            out.setSpace(in.space());
            return;
        }

        // Ping-pong the two scratch buffers; the final stage writes straight to out.
        const Image *src = &in;
        for (size_t i = 0; i < active.size(); ++i)
        {
            Image *dst;
            if (i + 1 == active.size())
                dst = &out;
            else
                dst = (i % 2 == 0) ? &mScratchA : &mScratchB;
            active[i]->apply(*src, *dst);
            src = dst;
        }
    }
}
