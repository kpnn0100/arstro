#include "Exposure.h"
#include <cmath>

namespace arstro
{
    Exposure::Exposure() : PointProcessor(propertyCount)
    {
        mSmoothEnable = false;
        initProperty(exposureEvID, 0);
        update();
    }

    void Exposure::update()
    {
        // D-36: `2^250` is +inf, and inf reaching the pipeline becomes NaN at the first
        // `inf - inf` or `inf * 0` — which is where every crash in this family started. The
        // parameter itself is a finite number, so the non-finite *parameter* guard cannot
        // catch it; the overflow happens here, in the derived gain.
        //
        // Clamped rather than rejected, and to a huge-but-finite bound: the engine clamps to
        // 1.0 on output anyway, so any gain past ~1e6 already renders pure white and the
        // clamp changes nothing a photographer can see. What it buys is that an absurd value
        // renders WHITE instead of poisoning the frame — which is exactly what D-36 said the
        // expected behaviour was.
        const double ev = (double)getProperty(exposureEvID);
        double gain = std::pow(2.0, ev < -400.0 ? -400.0 : (ev > 400.0 ? 400.0 : ev));
        if (!(gain > 0.0)) gain = 0.0;              // NaN or negative -> black
        else if (gain > 1e30) gain = 1e30;          // absurd -> white, but finite
        mGain = (Pixel)gain;
    }

    void Exposure::processPixel(const Pixel *in, Pixel *out, int channels)
    {
        const int colorCh = channels >= 3 ? 3 : channels;
        for (int c = 0; c < colorCh; ++c)
            out[c] = in[c] * mGain;
        for (int c = colorCh; c < channels; ++c)
            out[c] = in[c];  // pass alpha through
    }
}
