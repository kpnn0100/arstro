/*
 *  solaris_core — the capture seam (R-REC-1): what a host gives the core to hear an input device by.
 *
 *  Beside `IAudioOut`: the core never names a device API (R-SVC-4). A host's input runs its own thread and keeps what
 *  the device captured in a ring; the audio thread takes a block of it each cycle and NEVER waits — what has not
 *  arrived yet is silence, and counted, so a slow capture cannot stall the output that is the clock.
 */
#pragma once
#include <string>

namespace arstro
{
namespace solaris
{
    class IAudioIn
    {
    public:
        virtual ~IAudioIn() = default;
        /** Open `device` ("" = the system default input) at `rate` with `channels` (1 or 2), ~`bufferFrames` deep. */
        virtual bool open(const std::string &device, int rate, int channels, int bufferFrames, std::string &err) = 0;
        /** Up to `frames` captured frames, interleaved by the opened channels; never blocks — what has not arrived is
         *  written as zeros. Returns how many were real. Called from the audio thread only. */
        virtual int read(float *interleaved, int frames) = 0;
        /** Seconds between a sound reaching the input and its frame being readable. */
        virtual double latency() = 0;
        /** The frames `read` had to fill with silence since `open` (an overrun of the capture, or it falling behind). */
        virtual long long missed() const { return 0; }
    };
}
}
