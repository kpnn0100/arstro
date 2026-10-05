/*
 *  interstellar_core — the audio OUTPUT seam (R-AUD-6). The core names it; the host fills it with the
 *  machine's sound server. Interleaved stereo float at the mix rate.
 *
 *  `write` BLOCKS until the device has taken the samples — that back-pressure is the clock: frames
 *  written minus the device's latency is what the listener hears now, and playback's playhead is
 *  that, not the wall clock.
 */
#pragma once
#include <string>

namespace arstro
{
namespace interstellar
{
    class IAudioOut
    {
    public:
        virtual ~IAudioOut() = default;
        virtual bool open(int rate, std::string &err) = 0;
        virtual bool write(const float *stereo, int frames) = 0;
        /** Seconds between a sample handed to `write` and it being heard. */
        virtual double latency() = 0;
        /** Drop what is queued but not yet heard (a pause, a seek). */
        virtual void flush() {}
    };
}
}
