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

    void ImageBlock::releaseScratch()
    {
        mScratchA = Image();
        mScratchB = Image();
    }

    bool ImageBlock::isIdentity() const
    {
        // Const, so it may not resolve its children's parameters — it answers on what
        // they already report. That is exactly right for a nested block: the parent
        // resolved this block, and each child resolves itself when it is reached. A
        // child that would have become identity only after an unresolved update() is
        // simply run, which is the safe direction.
        for (auto *p : mChain)
            if (p && !p->isBypassed() && !p->isIdentity())
                return false;
        return true;
    }

    void ImageBlock::prepare()
    {
        for (auto *p : mChain)
            if (p)
                p->prepare();
    }

    void ImageBlock::process(const Image &in, Image &out)
    {
        // Active stages only. A stage is dropped from the run — not copied through —
        // when it is bypassed OR when its current parameters make it a no-op
        // (R-PREVIEW-6, D-45). Dropping rather than copying is the whole point: at
        // default EditParams seventeen stages ran over a 27 MB preview buffer to
        // reproduce it, which was 132 of the 193 ms a render cost, and the nine stages
        // that already early-outed did it with a SERIAL std::copy of that buffer each.
        //
        // resolveAndIsSkippable() (not isIdentity()) because a parameter has to be
        // snapped and update() has to have run before the question means anything; the
        // stages that survive then resolve a second time inside apply(), which is
        // idempotent and costs nothing measurable next to a pass over the image.
        std::vector<ImageProcessor *> active;
        active.reserve(mChain.size());
        for (auto *p : mChain)
            if (p && !p->resolveAndIsSkippable())
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
