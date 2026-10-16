/*
 *  solaris_host — audio files: what the core is forbidden to touch (R-SVC-4).
 *
 *  `decodeAudio` reads any file FFmpeg can (WAV, FLAC, MP3, OGG, AAC, …) WHOLE, resampled to the
 *  project's rate as interleaved stereo float — a mono file plays at its own level in both ears,
 *  the suite's centre being unity (R-MIX-11). `writeWav` writes 16- or 24-bit PCM or 32-bit float; `writeAudio`
 *  the delivery formats through FFmpeg's encoders — FLAC (16/24), MP3, Ogg Vorbis, Ogg Opus (R-EXPORT-1). Integer
 *  samples are written as x·2^(bits−1), clamped: the grid the service's dither put them on, exactly.
 */
#pragma once
#include "MixGraph.h"
#include <string>
#include <vector>

namespace arstro
{
namespace solaris_host
{
    bool decodeAudio(const std::string &path, int rate, solaris::engine::Pcm &out, std::string &err);
    bool writeWav(const std::string &path, const std::vector<std::vector<float>> &channels, int rate, int bits, std::string &err);
    /** format: flac | mp3 | ogg | opus; bits for flac (16, 24); kbps for the lossy ones. */
    bool writeAudio(const std::string &path, const std::vector<std::vector<float>> &channels, int rate,
                    const std::string &format, int bits, int kbps, std::string &err);
}
}
