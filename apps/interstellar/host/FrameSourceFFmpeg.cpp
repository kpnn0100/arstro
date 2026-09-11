#include "FrameSourceFFmpeg.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace interstellar_host
{
    FrameSourceFFmpeg::~FrameSourceFFmpeg() { closeAll(); }

    void FrameSourceFFmpeg::closeAll()
    {
        if (mSws) { sws_freeContext(mSws); mSws = nullptr; }
        if (mFrame) { av_frame_free(&mFrame); mFrame = nullptr; }
        if (mPkt) { av_packet_free(&mPkt); mPkt = nullptr; }
        if (mDec) { avcodec_free_context(&mDec); mDec = nullptr; }
        if (mFmt) { avformat_close_input(&mFmt); mFmt = nullptr; }
        mStream = -1;
        mHeld = -1;
        mEof = false;
    }

    bool FrameSourceFFmpeg::open(const std::string &path, Info &out)
    {
        closeAll();
        if (avformat_open_input(&mFmt, path.c_str(), nullptr, nullptr) < 0) return false;
        if (avformat_find_stream_info(mFmt, nullptr) < 0) { closeAll(); return false; }

        // libavformat 58's `av_find_best_stream` still takes a non-const `AVCodec**` (it became
        // const in 59), so the decoder pointer is fetched by id instead — which is version-neutral
        // and needs no cast.
        mStream = av_find_best_stream(mFmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (mStream < 0) { closeAll(); return false; }
        const AVCodec *codec = avcodec_find_decoder(mFmt->streams[mStream]->codecpar->codec_id);
        if (!codec) { closeAll(); return false; }

        mDec = avcodec_alloc_context3(codec);
        if (!mDec) { closeAll(); return false; }
        if (avcodec_parameters_to_context(mDec, mFmt->streams[mStream]->codecpar) < 0)
        { closeAll(); return false; }
        // Let libav use its own threading for the decode itself: this object is single-threaded
        // from the service's point of view, and frame-level threading inside one decoder is what
        // makes H.264 keep up at all.
        mDec->thread_count = 0;
        if (avcodec_open2(mDec, codec, nullptr) < 0) { closeAll(); return false; }

        mFrame = av_frame_alloc();
        mPkt = av_packet_alloc();
        if (!mFrame || !mPkt) { closeAll(); return false; }

        AVStream *st = mFmt->streams[mStream];
        mInfo.width = mDec->width;
        mInfo.height = mDec->height;
        // `avg_frame_rate` is what a container states; `r_frame_rate` is the best guess when it
        // does not. A still image has neither, so it falls back to 1 — which is right: a still is
        // a one-frame stream.
        double fps = st->avg_frame_rate.den ? av_q2d(st->avg_frame_rate) : 0.0;
        if (fps <= 0.0) fps = st->r_frame_rate.den ? av_q2d(st->r_frame_rate) : 0.0;
        if (fps <= 0.0) fps = 1.0;
        mInfo.fps = fps;
        if (st->nb_frames > 0) mInfo.frames = st->nb_frames;
        else if (st->duration > 0) mInfo.frames = (long long)(av_q2d(st->time_base) * st->duration * fps);
        else if (mFmt->duration > 0)
            mInfo.frames = (long long)((double)mFmt->duration / AV_TIME_BASE * fps);
        else mInfo.frames = 1;
        if (mInfo.frames < 1) mInfo.frames = 1;

        if (mInfo.width <= 0 || mInfo.height <= 0) { closeAll(); return false; }
        out = mInfo;
        return true;
    }

    bool FrameSourceFFmpeg::seekTo(long long frame)
    {
        AVStream *st = mFmt->streams[mStream];
        // Seek in the stream's own time base, BACKWARD, so we land on a keyframe at or before the
        // target and can then decode forward to it. Seeking forward would land past the target.
        const double seconds = mInfo.fps > 0 ? (double)frame / mInfo.fps : 0.0;
        const int64_t ts = (int64_t)(seconds / av_q2d(st->time_base));
        if (av_seek_frame(mFmt, mStream, ts, AVSEEK_FLAG_BACKWARD) < 0) return false;
        avcodec_flush_buffers(mDec);
        mEof = false;
        // The decoder's position is now "somewhere at or before the target". Reading one frame
        // tells us where, and `decodeUntil` walks the rest.
        mHeld = -1;
        return true;
    }

    /** The frame index of what the decoder currently holds, from its presentation timestamp. */
    static long long indexOf(const AVFrame *f, const AVStream *st, double fps)
    {
        const int64_t pts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp
                                                                       : f->pts;
        if (pts == AV_NOPTS_VALUE) return -1;
        return (long long)std::llround(pts * av_q2d(st->time_base) * fps);
    }

    bool FrameSourceFFmpeg::decodeUntil(long long frame)
    {
        AVStream *st = mFmt->streams[mStream];
        for (;;)
        {
            // Drain whatever the decoder already has before feeding it more.
            int r = avcodec_receive_frame(mDec, mFrame);
            if (r == 0)
            {
                const long long idx = indexOf(mFrame, st, mInfo.fps);
                mHeld = idx >= 0 ? idx : (mHeld < 0 ? 0 : mHeld + 1);
                if (mHeld >= frame) return true;
                continue;
            }
            if (r == AVERROR_EOF) { mEof = true; return mHeld >= 0; }
            if (r != AVERROR(EAGAIN)) return mHeld >= 0;

            // Needs more input.
            av_packet_unref(mPkt);
            const int rr = av_read_frame(mFmt, mPkt);
            if (rr < 0)
            {
                // Flush the decoder so its buffered frames come out; a short file's last frames
                // only appear after this.
                avcodec_send_packet(mDec, nullptr);
                for (;;)
                {
                    if (avcodec_receive_frame(mDec, mFrame) != 0) { mEof = true; return mHeld >= 0; }
                    const long long idx = indexOf(mFrame, st, mInfo.fps);
                    mHeld = idx >= 0 ? idx : mHeld + 1;
                    if (mHeld >= frame) return true;
                }
            }
            if (mPkt->stream_index != mStream) continue;
            if (avcodec_send_packet(mDec, mPkt) < 0) return mHeld >= 0;
        }
    }

    void FrameSourceFFmpeg::convertCurrent(interstellar::Raster &out)
    {
        // Straight (non-premultiplied) RGBA8, matching `Raster` and `RenderService::Frame`, so a
        // frame goes decoder -> grade -> composite -> writer with no conversion in between.
        mSws = sws_getCachedContext(mSws, mFrame->width, mFrame->height,
                                    (AVPixelFormat)mFrame->format, mInfo.width, mInfo.height,
                                    AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!mSws) return;
        out.allocate(mInfo.width, mInfo.height, 255);
        uint8_t *dst[4] = {out.rgba.data(), nullptr, nullptr, nullptr};
        int stride[4] = {mInfo.width * 4, 0, 0, 0};
        sws_scale(mSws, mFrame->data, mFrame->linesize, 0, mFrame->height, dst, stride);
    }

    bool FrameSourceFFmpeg::frameAt(long long frame, interstellar::Raster &out)
    {
        if (!mFmt || !mDec) return false;
        // A still image is a one-frame stream, and a clip may run past the end of its source;
        // clamping is right in both cases and is not an error.
        if (frame < 0) frame = 0;
        if (mInfo.frames > 0 && frame >= mInfo.frames) frame = mInfo.frames - 1;

        // Already holding it: the common case during a playback or an export.
        if (mHeld == frame) { convertCurrent(out); return !out.empty(); }

        // Backwards, or far enough forward that a seek beats decoding through. Decoding forward a
        // short distance is nearly free; seeking costs a keyframe search plus the decode from it.
        const bool needSeek = frame < mHeld || mHeld < 0 || frame - mHeld > kSeekThreshold || mEof;
        if (needSeek && !seekTo(frame))
        {
            // A stream that cannot seek (a pipe, a broken index) is still usable forwards.
            if (frame < mHeld) return false;
        }
        if (!decodeUntil(frame)) return false;
        convertCurrent(out);
        return !out.empty();
    }
}
}
