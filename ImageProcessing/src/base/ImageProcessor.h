/*
 *  Arstro ImageProcessing Library
 *
 *  ImageProcessor: the base processing node — the image-domain analogue of the
 *  DSP library's SignalProcessor. It keeps the SAME property mechanism (a
 *  PropertyIndex enum + initProperty/setProperty/getProperty + an update() hook
 *  that recomputes derived state on a parameter change), but the processing
 *  contract changes from a per-sample scalar to a whole-image transform, because
 *  images are random-access 2D buffers (a pixel is an RGB(A) vector and most ops
 *  need a neighbourhood or whole-image statistics).
 *
 *  Recipe for a concrete processor (mirrors Gain in the DSP library):
 *    1. enum PropertyIndex { fooID, ..., propertyCount };
 *    2. ctor : ImageProcessor(propertyCount) { initProperty(fooID, default); }
 *    3. setFoo(v) { setProperty(fooID, v); }
 *    4. override process(in, out) — or subclass PointProcessor and override
 *       processPixel() for a pure per-pixel point op.
 *    5. optionally override update() to recompute cached state (LUTs, gains).
 */
#pragma once
#include "Pixel.h"
#include "Image.h"
#include "SmoothedParameter.h"
#include <vector>
#include <string>

namespace arstro
{
    class ImageProcessor
    {
    public:
        ImageProcessor();
        explicit ImageProcessor(int propertyCount);
        virtual ~ImageProcessor();

        // ── public entry (analogue of SignalProcessor::out) ──
        /** Apply this processor: resolves parameters, then dispatches to process().
         *  `out` must be distinct from `in` (the engine passes separate buffers). */
        void apply(const Image &in, Image &out);
        /** Convenience overload that allocates and returns the output. */
        Image apply(const Image &in);

        // ── contract subclasses implement ──
        /** Transform `in` into `out`. The implementation MUST size `out`
         *  (resizeLike for point ops, arbitrary dims for Crop/Rotate). */
        virtual void process(const Image &in, Image &out) = 0;

        /** Recompute derived state after a property change (LUTs, gains). */
        virtual void update();
        /** Called once before a render pass. */
        virtual void prepare();

        // ── property system (mirrors SignalProcessor) ──
        void setProperty(int id, Pixel value);
        Pixel getProperty(int id);
        Pixel getPropertyTargetValue(int id);

        void setBypass(bool b) { mBypass = b; }
        bool isBypassed() const { return mBypass; }
        void setSmoothEnable(bool e) { mSmoothEnable = e; }
        void setName(std::string n) { mName = std::move(n); }
        const std::string &name() const { return mName; }

        /** Notify every live processor that the global ImageConfig changed. */
        static void notifyConfigChanged();

    protected:
        void initProperty(int id, Pixel value);
        virtual void onPropertyChanged(int id, Pixel value);
        /** Hook for a working-colour-space / config change (rarely needed). */
        virtual void onConfigChanged();

        ParameterSet mParams;
        bool mSmoothEnable = false;  ///< OFF by default for stills (instant edits)
        bool mBypass = false;
        std::string mName;

    private:
        static std::vector<ImageProcessor *> sInstances;
    };

    /**
     * PointProcessor: base for pure per-pixel point operations (exposure,
     * contrast, white balance, vibrance, tone curve, colour mixer/grading).
     * Implements the shared per-pixel loop so subclasses only write the kernel.
     */
    class PointProcessor : public ImageProcessor
    {
    public:
        PointProcessor() = default;
        explicit PointProcessor(int propertyCount) : ImageProcessor(propertyCount) {}

        void process(const Image &in, Image &out) final;

    protected:
        /** Transform one pixel. `in`/`out` point at `channels` contiguous values
         *  (RGB or RGBA, linear light). Alpha (channel 3) should be passed through. */
        virtual void processPixel(const Pixel *in, Pixel *out, int channels) = 0;
    };
}
