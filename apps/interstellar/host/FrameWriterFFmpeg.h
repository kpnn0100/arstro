/*
 *  interstellar/host — FrameWriterFFmpeg: `IFrameWriter` over libavformat/libavcodec (R-RENDER-2).
 *
 *  Host layer, for the same reason the source is: the core carries no codec.
 *
 *  **Two targets, and the choice is not a preference.** H.264 in MP4 is what a person can watch,
 *  and ProRes 422 in MOV is what an edit round-trips through without a generation loss. A third
 *  exists and is not a codec at all: the CLI's PPM sequence, which is the only output a golden
 *  test can compare byte for byte (R-RENDER-3) — so it stays, and this class does not replace it.
 *
 *  The container is chosen from the output extension rather than a flag, because a `.mp4` that
 *  silently contained ProRes would be a worse surprise than an unsupported-extension error.
 */
#pragma once
#include "service/InterstellarService.h"
#include <string>

struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace arstro
{
namespace interstellar_host
{
    class FrameWriterFFmpeg : public interstellar::IFrameWriter
    {
    public:
        FrameWriterFFmpeg() = default;
        ~FrameWriterFFmpeg() override;
        FrameWriterFFmpeg(const FrameWriterFFmpeg &) = delete;
        FrameWriterFFmpeg &operator=(const FrameWriterFFmpeg &) = delete;

        bool begin(const std::string &path, int w, int h, double fps, long long frames) override;
        bool write(const interstellar::Raster &frame) override;
        bool end() override;

        /** True when this class can write `path`, judged by its extension. The CLI asks first so
         *  it can fall back to its PPM sequence rather than failing a render. */
        static bool handles(const std::string &path);
        /** The last error, for a rejection message that names what went wrong. */
        const std::string &error() const { return mError; }

    private:
        bool drain(bool flush);
        void closeAll();

        AVFormatContext *mFmt = nullptr;
        AVCodecContext *mEnc = nullptr;
        AVStream *mStream = nullptr;
        AVFrame *mFrame = nullptr;
        AVPacket *mPkt = nullptr;
        SwsContext *mSws = nullptr;
        long long mNext = 0;
        bool mOpen = false;
        std::string mError;
    };
}
}
