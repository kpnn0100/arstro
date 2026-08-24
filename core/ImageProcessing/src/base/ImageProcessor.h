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
 *    6. override isIdentity() to say when the current parameters make this stage a
 *       no-op, so a chain can DROP it instead of running it (see below).
 *
 *  Identity skipping (R-PREVIEW-6, D-45): every processor must be able to say
 *  whether, as currently parameterised, it would return its own input. Without it
 *  a preview render at DEFAULT parameters ran all seventeen stages of the edit
 *  pipeline and 132 of its 193 ms went to arithmetic that reproduced the buffer it
 *  was handed — the dominant cost of the interaction a photographer performs most.
 *  Answering `true` is not an optimisation the caller may ignore: it is how the
 *  chain knows the stage carries no information, so ImageBlock removes it and no
 *  buffer is touched at all (not even copied).
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

        /** True when the CURRENT parameters make this stage a no-op — it would write
         *  its input back out unchanged. Default false: a processor that does not
         *  answer is always run, so adding a stage can never silently drop pixels.
         *
         *  Judged on `paramValue()` (the value last set), and only consulted once
         *  parameters have been resolved — see resolveAndIsSkippable(). It must be
         *  conservative in one direction only: claiming identity when the stage would
         *  in fact change a pixel is a wrong-output bug, while failing to claim it is
         *  merely slow. */
        virtual bool isIdentity() const { return false; }

        /** Resolve parameters (snap when smoothing is off, then update()) and report
         *  whether this stage can be skipped entirely — bypassed or at identity.
         *
         *  This exists so a chain can decide BEFORE calling apply(): apply() only knows
         *  how to copy the input through, and a copy of a 27 MB preview buffer per
         *  skipped stage was itself 1.46 ms x 9 stages. ImageBlock uses it to leave the
         *  stage out of the run, so a skipped stage costs nothing at all.
         *
         *  Identity is never claimed while smoothing is ENABLED: a smoothed parameter's
         *  current value may still be ramping toward a target that reads as neutral, and
         *  skipping would jump the ramp. Stills disable smoothing (mSmoothEnable = false),
         *  so this is the video path paying for its own generality. */
        bool resolveAndIsSkippable();

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
        /** The value a property was last SET to, readable from a const method — which
         *  is what isIdentity() must judge on, since it runs after apply() has snapped
         *  and `target` is the only value that is meaningful either way. */
        Pixel paramValue(int id) const { return mParams.target(id); }
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
