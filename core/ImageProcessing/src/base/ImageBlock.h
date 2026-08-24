/*
 *  Arstro ImageProcessing Library
 *
 *  ImageBlock: an ordered chain of ImageProcessors — the image-domain analogue
 *  of the DSP library's Block, and the concrete form of a professional EDIT
 *  STACK. process(in, out) runs every (non-bypassed) stage in order, feeding
 *  each stage's output into the next. Two scratch Images are ping-ponged so a
 *  long stack does not allocate a fresh buffer per stage; stages may change the
 *  dimensions (Crop/Rotate), so the scratch buffers are full Images.
 *
 *  The chain is non-owning: the EditEngine owns the concrete processors.
 *
 *  A stage that is bypassed OR whose parameters make it a no-op is DROPPED from the
 *  run, not copied through (R-PREVIEW-6, D-45) — see process().
 */
#pragma once
#include "ImageProcessor.h"
#include <vector>

namespace arstro
{
    class ImageBlock : public ImageProcessor
    {
    public:
        ImageBlock() = default;

        void add(ImageProcessor *p);     ///< append to the chain
        void remove(ImageProcessor *p);  ///< remove from the chain
        void clear();
        int size() const { return (int)mChain.size(); }

        void process(const Image &in, Image &out) override;
        /** True when every stage in the chain is itself bypassed or at identity, so a
         *  nested block can be dropped by its parent rather than copied through. */
        bool isIdentity() const override;
        void prepare() override;

    private:
        std::vector<ImageProcessor *> mChain;  // non-owning
        Image mScratchA, mScratchB;
    };
}
