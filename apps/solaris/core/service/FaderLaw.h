#pragma once
/*
 *  The mixer's fader law (R-MIX-11, R-CTRL-1) — ONE copy: the dock draws a gain at `faderPos(dB)` and turns a drag back
 *  with `faderDb`, and a MIDI control mapped to a gain moves it by the same law, so a hardware fader and the screen's
 *  agree to the pixel. 0 … 1 travel: −∞ (−120 dB) at the bottom, 0 dB at 0.708, +6 dB at the top (40 dB a decade).
 */
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace solaris
{
    inline double faderPos(double dB) { return dB <= -119.95 ? 0.0 : std::clamp(std::pow(10.0, (dB - 6.0) / 40.0), 0.0, 1.0); }
    inline double faderDb(double pos) { return pos <= 0.001 ? -120.0 : std::clamp(6.0 + 40.0 * std::log10(pos), -120.0, 6.0); }
}
}
