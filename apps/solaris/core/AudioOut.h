/*
 *  solaris_core — the audio OUTPUT seam (R-PLAY-1, R-DEV-8). The core names it; the host fills it
 *  with the sound server (PulseAudio's API, which PipeWire also serves). Interleaved float.
 *
 *  `write` BLOCKS until the device has room: that back-pressure is the clock — the player renders
 *  exactly as fast as the device plays, and what is heard now is what was written minus `latency`.
 */
#pragma once
#include <string>

namespace arstro
{
namespace solaris
{
    class IAudioOut
    {
    public:
        virtual ~IAudioOut() = default;
        /** Open `device` ("" = the system default) at `rate` with `channels`, ~`bufferFrames` deep. */
        virtual bool open(const std::string &device, int rate, int channels, int bufferFrames, std::string &err) = 0;
        virtual bool write(const float *interleaved, int frames) = 0;
        /** Seconds between a frame handed to `write` and it being heard. */
        virtual double latency() = 0;
        /** Drop what is queued but not yet heard (a stop, a seek). */
        virtual void flush() {}
    };
}
}
