/*
 *  solaris_host — audio files: what the core is forbidden to touch (R-SVC-4).
 *
 *  `decodeAudio` reads any file FFmpeg can (WAV, FLAC, MP3, OGG, AAC, …) WHOLE, resampled to the
 *  project's rate as interleaved stereo float — a mono file plays at its own level in both ears,
 *  the suite's centre being unity (R-MIX-11). `writeWav` writes 24-bit PCM or 32-bit float.
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
}
}
