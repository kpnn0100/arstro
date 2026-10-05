/*
 *  interstellar_core — the audio codec seam (R-AUD-5, amended). The core NAMES it and carries no
 *  codec; the host fills it with FFmpeg, exactly as it fills `IFrameSource`.
 *
 *  A source is read in FRAMES of the mix rate (one frame = one sample per channel), always as
 *  interleaved stereo float: the decoder resamples and folds to stereo, so the mixer never meets a
 *  file's own rate or layout. Reads are SEQUENTIAL in the common case (playback, a render) and the
 *  implementation keeps its position; a read elsewhere seeks. Outside the file is silence.
 */
#pragma once
#include <string>

namespace arstro
{
namespace interstellar
{
    class IAudioSource
    {
    public:
        struct Info
        {
            int rate = 48000;           // the mix rate the source was opened at
            double duration = 0.0;      // seconds of sound
            int fileRate = 0, fileChannels = 0;   // what the file carries, for display
            bool valid() const { return duration > 0.0; }
        };

        virtual ~IAudioSource() = default;
        /** False when the file has no audio stream (a silent video, a still) or cannot be read. */
        virtual bool open(const std::string &path, int rate, Info &out) = 0;
        /** `frames` stereo frames starting at frame `start` (at the open rate) into `stereo`
         *  (2 × frames floats). Silence before 0 and past the end. */
        virtual bool read(long long start, int frames, float *stereo) = 0;
    };
}
}
