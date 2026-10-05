#include "FrameWriterFFmpeg.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
}
#include <algorithm>
#include <cstdlib>
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
        if (mSwFrame) { av_frame_free(&mSwFrame); mSwFrame = nullptr; }
        if (mHwFrames) av_buffer_unref(&mHwFrames);
        if (mHwDevice) av_buffer_unref(&mHwDevice);
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

    bool FrameWriterFFmpeg::begin(const std::string &path, int w, int h, double fps, long long, const interstellar::EncodeSpec &spec)
    {
        closeAll();
        mError.clear();
        mPath = path;
        mFps = fps;
        if (w <= 0 || h <= 0) { mError = "the output raster is empty"; return false; }
        // Every encoder here wants even dimensions (4:2:0 / 4:2:2 chroma). Refusing is better than
        // silently cropping a frame the user asked for.
        if ((w % 2) || (h % 2))
        {
            mError = "the output raster must have even dimensions for a video codec (" +
                     std::to_string(w) + "x" + std::to_string(h) + ")";
            return false;
        }

        // The codec comes from the spec; the container must be one that carries it.
        const std::string ext = lowerExt(path);
        const bool intermediate = spec.codec == "prores" || spec.codec == "dnxhr";
        const bool okContainer = intermediate ? ext == "mov" : (ext == "mp4" || ext == "mkv" || ext == "mov");
        if (!okContainer)
        {
            mError = spec.codec + " is written into " + (intermediate ? ".mov" : ".mp4, .mkv or .mov") + ", not ." + ext;
            return false;
        }
        const char *encName = spec.codec == "h264" ? "libx264" : spec.codec == "h265" ? "libx265"
                            : spec.codec == "prores" ? "prores_ks" : spec.codec == "dnxhr" ? "dnxhd" : nullptr;
        if (!encName) { mError = "no encoder for codec " + spec.codec; return false; }
        mNote.clear();
        // R-PLAY-3: the GPU's video unit, when asked for and present; otherwise the software encoder
        if (spec.hardware && (spec.codec == "h264" || spec.codec == "h265"))
        {
            const char *dev = std::getenv("INTERSTELLAR_VAAPI_DEVICE");
            const AVCodec *hw = avcodec_find_encoder_by_name(spec.codec == "h264" ? "h264_vaapi" : "hevc_vaapi");
            if (hw && av_hwdevice_ctx_create(&mHwDevice, AV_HWDEVICE_TYPE_VAAPI, dev && *dev ? dev : nullptr, nullptr, 0) >= 0)
            {
                if (avformat_alloc_output_context2(&mFmt, nullptr, nullptr, path.c_str()) >= 0 && mFmt && openEncoder(hw, spec, w, h, ext))
                {
                    mOpen = true;
                    return true;
                }
                const std::string why = mError;
                closeAll();
                mError.clear();
                mNote = "hardware video unavailable (" + (why.empty() ? std::string("the VA-API encoder refused") : why) + ") — encoded in software";
            }
            else
            {
                closeAll();
                mNote = std::string("hardware video unavailable (") + (hw ? "no VA-API device" : "this FFmpeg has no VA-API encoder") + ") — encoded in software";
            }
        }

        if (avformat_alloc_output_context2(&mFmt, nullptr, nullptr, path.c_str()) < 0 || !mFmt)
        { mError = "cannot infer a container for " + path; return false; }

        const AVCodec *codec = avcodec_find_encoder_by_name(encName);
        if (!codec)
        {
            // Named honestly: this build of FFmpeg simply does not have it, and telling the user
            // which encoder is missing is what lets them pick another.
            mError = std::string("this FFmpeg build has no ") + encName + " encoder";
            closeAll();
            return false;
        }
        if (!openEncoder(codec, spec, w, h, ext)) { closeAll(); return false; }
        mOpen = true;
        return true;
    }

    bool FrameWriterFFmpeg::openEncoder(const AVCodec *codec, const interstellar::EncodeSpec &spec, int w, int h, const std::string &ext)
    {
        const bool hw = mHwDevice != nullptr;
        mStream = avformat_new_stream(mFmt, nullptr);
        mEnc = avcodec_alloc_context3(codec);
        if (!mStream || !mEnc) { mError = "cannot allocate the encoder"; return false; }

        mEnc->width = w;
        mEnc->height = h;
        const std::string &pf = spec.profile;
        if (hw)
        {
            // the surface pool the encoder reads from: NV12 (8-bit) or P010 (10-bit H.265)
            mHwFrames = av_hwframe_ctx_alloc(mHwDevice);
            if (!mHwFrames) { mError = "cannot allocate VA-API surfaces"; return false; }
            auto *fc = reinterpret_cast<AVHWFramesContext *>(mHwFrames->data);
            fc->format = AV_PIX_FMT_VAAPI;
            fc->sw_format = spec.codec == "h265" && spec.bitDepth == 10 ? AV_PIX_FMT_P010 : AV_PIX_FMT_NV12;
            fc->width = w;
            fc->height = h;
            fc->initial_pool_size = 16;
            if (av_hwframe_ctx_init(mHwFrames) < 0) { mError = "the VA-API surface pool refused " + std::to_string(w) + "x" + std::to_string(h); return false; }
            mEnc->hw_frames_ctx = av_buffer_ref(mHwFrames);
            mEnc->pix_fmt = AV_PIX_FMT_VAAPI;
        }
        else if (spec.codec == "h264") mEnc->pix_fmt = AV_PIX_FMT_YUV420P;
        else if (spec.codec == "h265") mEnc->pix_fmt = spec.bitDepth == 10 ? AV_PIX_FMT_YUV420P10LE : AV_PIX_FMT_YUV420P;
        else if (spec.codec == "prores") mEnc->pix_fmt = pf == "4444" ? AV_PIX_FMT_YUV444P10LE : AV_PIX_FMT_YUV422P10LE;
        else mEnc->pix_fmt = pf == "444" ? AV_PIX_FMT_YUV444P10LE : pf == "hqx" ? AV_PIX_FMT_YUV422P10LE : AV_PIX_FMT_YUV422P;
        // D-9: BT.709, video range, and SAID in the stream
        mEnc->color_primaries = AVCOL_PRI_BT709;
        mEnc->color_trc = AVCOL_TRC_BT709;
        mEnc->colorspace = AVCOL_SPC_BT709;
        mEnc->color_range = AVCOL_RANGE_MPEG;
        AVRational tb = av_d2q(1.0 / (mFps > 0 ? mFps : 24.0), 1000000);
        mEnc->time_base = tb;
        mEnc->framerate = av_inv_q(tb);
        mStream->time_base = tb;
        mEnc->thread_count = 0;
        if (hw)
        {
            // constant quality: the CRF number as the quantiser, the closest VA-API has
            av_opt_set(mEnc->priv_data, "rc_mode", "CQP", 0);
            mEnc->global_quality = spec.quality;
            if (spec.codec == "h265" && ext != "mkv") mStream->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');
        }
        else if (spec.codec == "h264" || spec.codec == "h265")
        {
            av_opt_set(mEnc->priv_data, "preset", spec.speed.c_str(), 0);
            av_opt_set(mEnc->priv_data, "crf", std::to_string(spec.quality).c_str(), 0);
            if (spec.codec == "h265")
            {
                av_opt_set(mEnc->priv_data, "x265-params", "log-level=error", 0);
                if (ext != "mkv") mStream->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');   // QuickTime plays hvc1
            }
        }
        else if (spec.codec == "prores")
        {
            const char *p = pf == "proxy" ? "0" : pf == "lt" ? "1" : pf == "hq" ? "3" : pf == "4444" ? "4" : "2";
            av_opt_set(mEnc->priv_data, "profile", p, 0);
            av_opt_set(mEnc->priv_data, "vendor", "apl0", 0);
        }
        else
            av_opt_set(mEnc->priv_data, "profile", ("dnxhr_" + (pf.empty() ? std::string("hq") : pf)).c_str(), 0);
        if (mFmt->oformat->flags & AVFMT_GLOBALHEADER) mEnc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        if (avcodec_open2(mEnc, codec, nullptr) < 0) { mError = "the encoder refused these settings"; return false; }
        const uint32_t tag = mStream->codecpar->codec_tag;
        if (avcodec_parameters_from_context(mStream->codecpar, mEnc) < 0) { mError = "cannot describe the stream"; return false; }
        if (tag) mStream->codecpar->codec_tag = tag;   // the parameters copy resets it
        if (avio_open(&mFmt->pb, mPath.c_str(), AVIO_FLAG_WRITE) < 0) { mError = "cannot write " + mPath; return false; }
        if (avformat_write_header(mFmt, nullptr) < 0) { mError = "cannot write the container header"; return false; }

        mFrame = av_frame_alloc();
        mPkt = av_packet_alloc();
        if (!mFrame || !mPkt) { mError = "cannot allocate a frame"; return false; }
        if (hw)
        {
            // RGBA → NV12/P010 in software, then uploaded to a surface per frame
            mSwFrame = av_frame_alloc();
            if (!mSwFrame) { mError = "cannot allocate a frame"; return false; }
            mSwFrame->format = reinterpret_cast<AVHWFramesContext *>(mHwFrames->data)->sw_format;
            mSwFrame->width = w;
            mSwFrame->height = h;
            if (av_frame_get_buffer(mSwFrame, 0) < 0) { mError = "cannot allocate frame storage"; return false; }
            return true;
        }
        mFrame->format = mEnc->pix_fmt;
        mFrame->width = w;
        mFrame->height = h;
        if (av_frame_get_buffer(mFrame, 0) < 0) { mError = "cannot allocate frame storage"; return false; }
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

        AVFrame *target = mSwFrame ? mSwFrame : mFrame;   // hardware: convert into the software frame first
        SwsContext *prev = mSws;
        // a deep frame (R-COLOR-1) goes in as RGBA64, so a 10-bit encode gets 10 bits of picture
        mSws = sws_getCachedContext(mSws, frame.width, frame.height, frame.deep() ? AV_PIX_FMT_RGBA64 : AV_PIX_FMT_RGBA, mEnc->width,
                                    mEnc->height, (AVPixelFormat)target->format, SWS_BILINEAR, nullptr, nullptr,
                                    nullptr);
        if (!mSws) { mError = "cannot build the colour converter"; return false; }
        if (mSws != prev)
        {
            // D-9: full-range RGB in, BT.709 video-range YUV out — what the stream is tagged as
            const int *coeffs = sws_getCoefficients(SWS_CS_ITU709);
            sws_setColorspaceDetails(mSws, coeffs, 1, coeffs, 0, 0, 1 << 16, 1 << 16);
        }
        if (av_frame_make_writable(target) < 0) { mError = "the frame is not writable"; return false; }

        const uint8_t *src[4] = {frame.deep() ? reinterpret_cast<const uint8_t *>(frame.rgba16.data()) : frame.rgba.data(), nullptr, nullptr, nullptr};
        int stride[4] = {frame.width * (frame.deep() ? 8 : 4), 0, 0, 0};
        sws_scale(mSws, src, stride, 0, frame.height, target->data, target->linesize);
        if (mSwFrame)
        {
            // a fresh surface from the pool, the converted frame uploaded into it
            av_frame_unref(mFrame);
            if (av_hwframe_get_buffer(mHwFrames, mFrame, 0) < 0) { mError = "no free VA-API surface"; return false; }
            if (av_hwframe_transfer_data(mFrame, mSwFrame, 0) < 0) { mError = "the upload to the GPU failed"; return false; }
        }
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
