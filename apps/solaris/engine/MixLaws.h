/*
 *  solaris_engine — the mix laws (R-MIX-11). Not DSP: a gain and a fade ramp, and they must be
 *  EXACTLY Interstellar's (`apps/interstellar/render/AudioMix.cpp`), because the two apps share
 *  one audio format and a project must sound the same in both.
 *
 *    (M1) dB → linear:  10^(dB/20)
 *    (M2) balance pan, unity at centre: the side panned away from falls on a quarter cosine
 *           gL = pan > 0 ? cos(pan·π/2) : 1        gR = pan < 0 ? cos(−pan·π/2) : 1
 *    (M3) fades linear in AMPLITUDE over their lengths:
 *           e = min( t < fadeIn ? t/fadeIn : 1 ,  t > dur − fadeOut ? (dur − t)/fadeOut : 1 )
 */
#pragma once
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace solaris
{
namespace engine
{
    inline double dbToLinear(double db) { return std::pow(10.0, db / 20.0); }

    inline void balancePan(double pan, double &gl, double &gr)
    {
        constexpr double kHalfPi = 1.5707963267948966;
        gl = pan > 0 ? std::cos(std::min(1.0, pan) * kHalfPi) : 1.0;
        gr = pan < 0 ? std::cos(std::min(1.0, -pan) * kHalfPi) : 1.0;
    }

    /** The fade envelope at sample `t` of a region `dur` samples long. */
    inline double fadeGain(long long t, long long dur, long long fadeIn, long long fadeOut)
    {
        double e = 1.0;
        if (fadeIn > 0 && t < fadeIn) e = std::max(0.0, (double)t / (double)fadeIn);
        if (fadeOut > 0 && t > dur - fadeOut) e = std::min(e, std::max(0.0, (double)(dur - t) / (double)fadeOut));
        return e;
    }
}
}
}
