#include "FrameWriterFFmpeg.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace interstellar_host
{
    namespace
    {
        std::string lowerExt(const std::string &path)
        {
            const auto dot = path.find_last_of('.');
            if (dot == std::string::npos) return {};
            std::string e = path.substr(dot + 1);
            for (char &c : e) c = (char)std::tolower((unsigned char)c);
            return e;
        }
    }

    bool FrameWriterFFmpeg::handles(const std::string &path)
    {
        const std::string e = lowerExt(path);
        return e == "mp4" || e == "mov" || e == "mkv";
    }

    FrameWriterFFmpeg::~FrameWriterFFmpeg() { closeAll(); }

    void FrameWriterFFmpeg::closeAll()
    {
        if (mSws) { sws_freeContext(mSws); mSws = nullptr; }
        if (mFrame) { av_frame_free(&mFrame); mFrame = nullptr; }
        if (mPkt) { av_packet_free(&mPkt); mPkt = nullptr; }
        if (mEnc) { avcodec_free_context(&mEnc); mEnc = nullptr; }
        if (mFmt)
        {
            if (mFmt->pb) avio_closep(&mFmt->pb);
            avformat_free_context(mFmt);
            mFmt = nullptr;
        }
        mStream = nullptr;
        mOpen = false;
        mNext = 0;
    }

    bool FrameWriterFFmpeg::begin(const std::string &path, int w, int h, double fps, long long)
    {
        closeAll();
        mError.clear();
        if (w <= 0 || h <= 0) { mError = "the output raster is empty"; return false; }
        // Both encoders need even dimensions (4:2:0 chroma for H.264, and ProRes is happier with
        // them). Refusing is better than silently cropping a frame the user asked for.
        if ((w % 2) || (h % 2))
        {
            mError = "the output raster must have even dimensions for a video codec (" +
                     std::to_string(w) + "x" + std::to_string(h) + ")";
            return false;
        }

        const std::string ext = lowerExt(path);
        // The container picks the codec, from the extension — a `.mp4` that silently held ProRes
        // would be a worse surprise than an error.
        const bool prores = ext == "mov";
        const AVCodecID id = prores ? AV_CODEC_ID_PRORES : AV_CODEC_ID_H264;

        if (avformat_alloc_output_context2(&mFmt, nullptr, nullptr, path.c_str()) < 0 || !mFmt)
        { mError = "cannot infer a container for " + path; return false; }

        const AVCodec *codec = avcodec_find_encoder(id);
        if (!codec)
        {
            // Named honestly: this build of FFmpeg simply does not have it, and telling the user
            // which encoder is missing is what lets them pick another extension.
            mError = std::string("this FFmpeg build has no ") + (prores ? "ProRes" : "H.264") +
                     " encoder";
            closeAll();
            return false;
        }

        mStream = avformat_new_stream(mFmt, nullptr);
        mEnc = avcodec_alloc_context3(codec);
        if (!mStream || !mEnc) { mError = "cannot allocate the encoder"; closeAll(); return false; }

        mEnc->width = w;
        mEnc->height = h;
        mEnc->pix_fmt = prores ? AV_PIX_FMT_YUV422P10 : AV_PIX_FMT_YUV420P;
        // A rational time base derived from fps, so 23.976 and 29.97 are exact rather than
        // rounded — frames are the authority (R-CUT-5) and a drifting time base would make the
        // output disagree with the project about when a cut happens.
        AVRational tb = av_d2q(1.0 / (fps > 0 ? fps : 24.0), 1000000);
        mEnc->time_base = tb;
        mEnc->framerate = av_inv_q(tb);
        mStream->time_base = tb;
        mEnc->thread_count = 0;
        if (!prores)
        {
            av_opt_set(mEnc->priv_data, "preset", "medium", 0);
            av_opt_set(mEnc->priv_data, "crf", "18", 0);
        }
        if (mFmt->oformat->flags & AVFMT_GLOBALHEADER) mEnc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        if (avcodec_open2(mEnc, codec, nullptr) < 0)
        { mError = "the encoder refused these settings"; closeAll(); return false; }
        if (avcodec_parameters_from_context(mStream->codecpar, mEnc) < 0)
        { mError = "cannot describe the stream"; closeAll(); return false; }
        if (avio_open(&mFmt->pb, path.c_str(), AVIO_FLAG_WRITE) < 0)
        { mError = "cannot write " + path; closeAll(); return false; }
        if (avformat_write_header(mFmt, nullptr) < 0)
        { mError = "cannot write the container header"; closeAll(); return false; }

        mFrame = av_frame_alloc();
        mPkt = av_packet_alloc();
        if (!mFrame || !mPkt) { mError = "cannot allocate a frame"; closeAll(); return false; }
        mFrame->format = mEnc->pix_fmt;
        mFrame->width = w;
        mFrame->height = h;
        if (av_frame_get_buffer(mFrame, 0) < 0)
        { mError = "cannot allocate frame storage"; closeAll(); return false; }

        mOpen = true;
        return true;
    }

    bool FrameWriterFFmpeg::drain(bool flush)
    {
        for (;;)
        {
            const int r = avcodec_receive_packet(mEnc, mPkt);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return true;
            if (r < 0) { mError = "the encoder failed"; return false; }
            av_packet_rescale_ts(mPkt, mEnc->time_base, mStream->time_base);
            mPkt->stream_index = mStream->index;
            const int w = av_interleaved_write_frame(mFmt, mPkt);
            av_packet_unref(mPkt);
            if (w < 0) { mError = "cannot write a packet"; return false; }
            if (!flush) return true;   // one packet per frame is the normal case
        }
    }

    bool FrameWriterFFmpeg::write(const interstellar::Raster &frame)
    {
        if (!mOpen) return false;
        if (frame.width != mEnc->width || frame.height != mEnc->height)
        { mError = "a frame arrived at the wrong size"; return false; }

        mSws = sws_getCachedContext(mSws, frame.width, frame.height, AV_PIX_FMT_RGBA, mEnc->width,
                                    mEnc->height, mEnc->pix_fmt, SWS_BILINEAR, nullptr, nullptr,
                                    nullptr);
        if (!mSws) { mError = "cannot build the colour converter"; return false; }
        if (av_frame_make_writable(mFrame) < 0) { mError = "the frame is not writable"; return false; }

        const uint8_t *src[4] = {frame.rgba.data(), nullptr, nullptr, nullptr};
        int stride[4] = {frame.width * 4, 0, 0, 0};
        sws_scale(mSws, src, stride, 0, frame.height, mFrame->data, mFrame->linesize);
        mFrame->pts = mNext++;

        if (avcodec_send_frame(mEnc, mFrame) < 0) { mError = "the encoder rejected a frame"; return false; }
        return drain(false);
    }

    bool FrameWriterFFmpeg::end()
    {
        if (!mOpen) return false;
        // Flush: an encoder holds frames back (B-frames, lookahead), and a file closed without
        // this is short by however many it was holding — a truncated render that looks like a
        // rendering bug.
        avcodec_send_frame(mEnc, nullptr);
        const bool ok = drain(true);
        if (ok && av_write_trailer(mFmt) < 0) mError = "cannot write the container trailer";
        const bool good = ok && mError.empty();
        closeAll();
        return good;
    }
}
}
