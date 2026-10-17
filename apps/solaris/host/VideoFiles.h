/*
 *  solaris_host — the song's picture (R-VIDEO-1; docs/discussion.md Round 18): what the core is forbidden to touch
 *  (R-SVC-4), FFmpeg's as the audio's.
 *
 *  `probeVideo` reads a file's picture — its length, size and frame rate; a file with no picture (audio, a cover image,
 *  text) is no video. `VideoReader` decodes the frame showing at a time, scaled to fit a box (its aspect kept), RGBA —
 *  stepping forward from the last frame it decoded, seeking only to go back or far ahead. `muxVideo` writes the export:
 *  the picture's own stream COPIED (never re-encoded), its first frame `startAt` seconds into the file — a negative
 *  `startAt` cuts it at the first keyframe from there, so what shows is still in time — and cut where the mix ends; the
 *  mix beside it encoded (AAC in .mp4 / .mov, FLAC in .mkv).
 */
#pragma once
#include "SolarisService.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris_host
{
    bool probeVideo(const std::string &path, solaris::VideoInfo &info, std::string &err);
    bool muxVideo(const std::string &video, double startAt, const std::vector<std::vector<float>> &mix, int rate,
                  const std::string &out, std::string &err);

    struct VideoFrame
    {
        int width = 0, height = 0;
        double time = -1;                 // the frame's own time in the picture (seconds)
        std::vector<uint8_t> rgba;        // width × height × 4, rows top first
    };

    class VideoReader
    {
    public:
        VideoReader();
        ~VideoReader();
        bool open(const std::string &path, std::string &err);
        /** The frame showing `t` seconds into the picture, scaled to fit boxW × boxH. */
        bool frame(double t, int boxW, int boxH, VideoFrame &out, std::string &err);
        int seeks() const; // how often it went back to a keyframe (a test reads it: playing forward never does)

    private:
        struct Impl;
        std::unique_ptr<Impl> d;
    };
}
}
