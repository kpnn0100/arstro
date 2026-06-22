/*
 *  Arstro ImageProcessing Library
 *
 *  SmoothedParameter / ParameterSet: the parameter concern, mirrored from the
 *  DSP library (DigitalSignalProcessing/src/base/SmoothedParameter.h). Each
 *  parameter tracks three values (last/target/current) so a value can ramp.
 *
 *  For still images smoothing is disabled (ImageProcessor sets mSmoothEnable =
 *  false), so a set is snapped instantly; the ramp machinery is kept for parity
 *  and for VideoProcessor, which can re-arm smoothing to interpolate a parameter
 *  across frames.
 */
#pragma once
#include "Pixel.h"
#include <vector>

namespace arstro
{
    /** One (optionally) smoothly-interpolated parameter. */
    class SmoothedParameter
    {
    public:
        Pixel last = 0;
        Pixel target = 0;
        Pixel current = 0;

        /** Set all three to the same value (no ramp — used at init / snap). */
        void init(Pixel value) { last = target = current = value; }

        /** Request a new destination. @return true if the target actually moved. */
        bool setTarget(Pixel value)
        {
            if (target == value)
                return false;
            target = value;
            return true;
        }

        /** Anchor a new ramp at wherever `current` is right now. */
        void beginRamp() { last = current; }

        /** Linear-interpolate `current` between `last` and `target`. ratio in [0,1]. */
        void interpolate(Pixel ratio)
        {
            current = last * ((Pixel)1 - ratio) + ratio * target;
        }

        /** Jump straight to `target` (ramp finished, or smoothing disabled). */
        void snap() { last = current = target; }
    };

    /** Fixed-size collection of SmoothedParameters addressed by a PropertyIndex. */
    class ParameterSet
    {
    public:
        void resize(int count) { mParams.resize(count); }
        int size() const { return (int)mParams.size(); }

        void init(int id, Pixel value) { mParams[id].init(value); }
        bool setTarget(int id, Pixel value) { return mParams[id].setTarget(value); }
        void snap(int id) { mParams[id].snap(); }

        Pixel current(int id) const { return mParams[id].current; }
        Pixel target(int id) const { return mParams[id].target; }

        void beginRamp()
        {
            for (auto &p : mParams)
                p.beginRamp();
        }
        void interpolate(Pixel ratio)
        {
            for (auto &p : mParams)
                p.interpolate(ratio);
        }
        void snapAll()
        {
            for (auto &p : mParams)
                p.snap();
        }

    private:
        std::vector<SmoothedParameter> mParams;
    };
}
