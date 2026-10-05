/*
 *  interstellar_render — AudioMix: the master sum (R-AUD-5 amended, R-AUD-9). Knows no project: the
 *  service resolves a timeline into an `AudioPlan` — every audible clip with its file, placement,
 *  gain, fades and pan — and this sums any stretch of it, sample-accurately, so a render's frame k
 *  carries exactly the samples [k·rate/fps, (k+1)·rate/fps) and playback hears the same numbers.
 *
 *  Per clip: the file's sound from `in` at `speed` (varispeed: the pitch follows, as a tape's would
 *  — said, not hidden), linear-in-amplitude fades in and out over their lengths, the gain in dB, and
 *  a constant-power pan (−3 dB at centre is NOT applied: centre is unity, so a mono-folded stereo
 *  file is not quieter than it was). The master gain multiplies the sum. Nothing limits or clips:
 *  the meters say when the sum passes full scale (R-AUD-8), and a render writes what was mixed.
 *
 *  Pure: the same plan and range give the same samples whatever the order of reads.
 */
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar
{
class IAudioSource;
namespace render
{
    struct AudioItem
    {
        std::string media;                  // the file (resolved)
        double at = 0, in = 0, out = 0;     // timeline seconds; source seconds
        double speed = 1.0;
        double gainDb = 0.0;                // clip + track, dB
        double fadeIn = 0.0, fadeOut = 0.0; // seconds of timeline
        double pan = 0.0;                   // -1 … +1
        double end() const { return at + (out - in) / (speed > 0 ? speed : 1.0); }
    };

    struct AudioPlan
    {
        std::vector<AudioItem> items;
        double masterDb = 0.0;
        int rate = 48000;
        bool empty() const { return items.empty(); }
        std::string key() const;            // identity of the samples this plan produces
    };

    /** The source for `media`, opened at `rate` (the caller owns and caches them); null = silent. */
    using AudioSourceFor = std::function<IAudioSource *(const std::string &media)>;

    /** Sum `frames` stereo frames of `plan` from timeline frame `start` (at plan.rate) into `out`
     *  (2 × frames floats, overwritten). */
    void mixAudio(const AudioPlan &plan, const AudioSourceFor &sourceFor, long long start, int frames, float *out);
}
}
}
