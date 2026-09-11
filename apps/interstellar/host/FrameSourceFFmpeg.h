/*
 *  interstellar/host — FrameSourceFFmpeg: `IFrameSource` over libavformat/libavcodec/libswscale
 *  (R-PLAY-1).
 *
 *  It lives in the HOST layer because `interstellar_core` carries no codec (R-SVC-7): the core
 *  names the seam and the host fills it, exactly as `cosmo_core` names `IImageDecoder` and its
 *  host fills it with GdkPixbuf and LibRaw.
 *
 *  **It is sequential, and that is the whole performance story.** A video file is a sequence of
 *  compressed frames that mostly depend on the ones before them, so decoding frame N+1 after
 *  frame N is nearly free while decoding frame N+1 after a seek is not. This keeps its position
 *  and seeks only when asked to go backwards, or forward far enough that decoding through would
 *  cost more than a seek. A decoder that reopened or re-seeked per frame would turn a scrub into
 *  a seek storm — which is the difference between an editor and a slideshow.
 *
 *  **A still image is a one-frame video**, so the timeline needs no second code path for stills:
 *  libavformat demuxes PNG/JPEG/TIFF as a single-frame stream, and `frameAt` clamps to it.
 *
 *  One object per media path, never shared between threads: it holds a decoder position and
 *  libav's contexts are not thread-safe.
 */
#pragma once
#include "service/InterstellarService.h"
#include <string>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace arstro
{
namespace interstellar_host
{
    class FrameSourceFFmpeg : public interstellar::IFrameSource
    {
    public:
        FrameSourceFFmpeg() = default;
        ~FrameSourceFFmpeg() override;
        FrameSourceFFmpeg(const FrameSourceFFmpeg &) = delete;
        FrameSourceFFmpeg &operator=(const FrameSourceFFmpeg &) = delete;

        bool open(const std::string &path, Info &out) override;
        bool frameAt(long long frame, interstellar::Raster &out) override;

    private:
        /** Decode forward until the stream's position reaches `frame`. Returns false at EOF. */
        bool decodeUntil(long long frame);
        bool seekTo(long long frame);
        void convertCurrent(interstellar::Raster &out);
        void closeAll();

        AVFormatContext *mFmt = nullptr;
        AVCodecContext *mDec = nullptr;
        AVFrame *mFrame = nullptr;
        AVPacket *mPkt = nullptr;
        SwsContext *mSws = nullptr;
        int mStream = -1;
        Info mInfo;
        /** The frame index the decoder is currently HOLDING, or -1 before the first decode. */
        long long mHeld = -1;
        bool mEof = false;
        /** Beyond this many frames ahead, seeking beats decoding through. Small because a seek
         *  lands on a keyframe and then has to decode forward anyway. */
        static constexpr long long kSeekThreshold = 24;
    };
}
}
