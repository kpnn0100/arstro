#include "FrameWriterFFmpeg.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/hwcontext.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mastering_display_metadata.h>
#include <libswscale/swscale.h>
}

// FFmpeg 5.1 replaced the channel_layout/channels pair with AVChannelLayout; 7 removed the old pair
#define IS_AV_CH_LAYOUT (LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100))
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
        if (mAFrame) av_frame_free(&mAFrame);
        if (mAEnc) avcodec_free_context(&mAEnc);
        mAStream = nullptr;
        if (mSEnc) avcodec_free_context(&mSEnc);
        mSStream = nullptr;
        mAudioFifo.clear();
        mAudioNext = 0;
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
        // D-9: BT.709, video range, and SAID in the stream — R-COLOR-4: or what --output made the
        // pixels: sRGB's transfer, P3-D65's primaries (gamma 2.6 has no tag: unspecified), Rec.2100
        const std::string &out = spec.output;
        const bool hdr = out == "pq" || out == "hlg";
        mEnc->color_primaries = hdr ? AVCOL_PRI_BT2020 : out == "p3d65" ? AVCOL_PRI_SMPTE432 : AVCOL_PRI_BT709;
        mEnc->color_trc = out == "pq" ? AVCOL_TRC_SMPTE2084 : out == "hlg" ? AVCOL_TRC_ARIB_STD_B67
                        : out == "srgb" ? AVCOL_TRC_IEC61966_2_1 : out == "p3d65" ? AVCOL_TRC_UNSPECIFIED : AVCOL_TRC_BT709;
        mEnc->colorspace = hdr ? AVCOL_SPC_BT2020_NCL : AVCOL_SPC_BT709;
        mEnc->color_range = AVCOL_RANGE_MPEG;
        mSwsMatrix = hdr ? SWS_CS_BT2020 : SWS_CS_ITU709;
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
                std::string xp = "log-level=error";
                if (hdr)
                {
                    // the HDR signalling in the bitstream itself: colour description, and for PQ the
                    // mastering display (Rec.2020 primaries, D65, the peak, 0.005 cd/m²) — MaxCLL/MaxFALL
                    // are 0, "unknown": a one-pass encode cannot know them before the first frame
                    xp += std::string(":repeat-headers=1:colorprim=bt2020:colormatrix=bt2020nc:transfer=") +
                          (out == "pq" ? "smpte2084:hdr10-opt=1" : "arib-std-b67");
                    if (out == "pq")
                        xp += ":master-display=G(8500,39850)B(6550,2300)R(35400,14600)WP(15635,16450)L(" +
                              std::to_string((long long)std::llround(spec.peak * 10000.0)) + ",50):max-cll=0,0";
                }
                av_opt_set(mEnc->priv_data, "x265-params", xp.c_str(), 0);
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

        if (spec.audioRate > 0)
        {
            // R-AUD-9: the master beside the picture — AAC for the delivery codecs, 24-bit PCM for the
            // intermediates an editor or a mixer takes in
            const bool pcm = spec.codec == "prores" || spec.codec == "dnxhr";
            const AVCodec *ac = avcodec_find_encoder_by_name(pcm ? "pcm_s24le" : "aac");
            if (!ac) { mError = std::string("this FFmpeg build has no ") + (pcm ? "pcm_s24le" : "aac") + " encoder"; return false; }
            mAStream = avformat_new_stream(mFmt, nullptr);
            mAEnc = avcodec_alloc_context3(ac);
            if (!mAStream || !mAEnc) { mError = "cannot allocate the audio encoder"; return false; }
            mAEnc->sample_rate = spec.audioRate;
#if IS_AV_CH_LAYOUT
            av_channel_layout_from_mask(&mAEnc->ch_layout, AV_CH_LAYOUT_STEREO);
#else
            mAEnc->channel_layout = AV_CH_LAYOUT_STEREO;
            mAEnc->channels = 2;
#endif
            mAEnc->sample_fmt = pcm ? AV_SAMPLE_FMT_S32 : AV_SAMPLE_FMT_FLTP;
            if (!pcm) mAEnc->bit_rate = 320000;
            mAEnc->time_base = AVRational{1, spec.audioRate};
            mAStream->time_base = mAEnc->time_base;
            if (mFmt->oformat->flags & AVFMT_GLOBALHEADER) mAEnc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            if (avcodec_open2(mAEnc, ac, nullptr) < 0) { mError = "the audio encoder refused these settings"; return false; }
            if (avcodec_parameters_from_context(mAStream->codecpar, mAEnc) < 0) { mError = "cannot describe the audio stream"; return false; }
            mAudioFrameSize = mAEnc->frame_size > 0 ? mAEnc->frame_size : 1024;
            mAFrame = av_frame_alloc();
            if (!mAFrame) { mError = "cannot allocate an audio frame"; return false; }
        }
        if (!spec.subtitles.empty())
        {
            // R-DLV-1: the captions as a subtitle stream — 3GPP timed text in MP4/MOV, SubRip in MKV.
            // Both encoders take ASS events, so they need the ASS header every FFmpeg subtitle encoder reads.
            const bool mkv = ext == "mkv";
            if (!mkv && ext != "mp4" && ext != "mov") { mError = "a subtitle track goes in an MP4, MOV or MKV, not ." + ext; return false; }
            const AVCodec *sc = avcodec_find_encoder(mkv ? AV_CODEC_ID_SUBRIP : AV_CODEC_ID_MOV_TEXT);
            if (!sc) { mError = std::string("this FFmpeg build has no ") + (mkv ? "SubRip" : "mov_text") + " encoder"; return false; }
            mSStream = avformat_new_stream(mFmt, nullptr);
            mSEnc = avcodec_alloc_context3(sc);
            if (!mSStream || !mSEnc) { mError = "cannot allocate the subtitle encoder"; return false; }
            static const char kAss[] =
                "[Script Info]\r\nScriptType: v4.00+\r\nPlayResX: 384\r\nPlayResY: 288\r\n\r\n[V4+ Styles]\r\n"
                "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, "
                "StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\r\n"
                "Style: Default,Arial,16,&Hffffff,&Hffffff,&H0,&H0,0,0,0,0,100,100,0,0,1,1,0,2,10,10,10,0\r\n\r\n[Events]\r\n"
                "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n";
            mSEnc->subtitle_header = reinterpret_cast<uint8_t *>(av_strdup(kAss));
            mSEnc->subtitle_header_size = (int)sizeof kAss - 1;
            mSEnc->time_base = AVRational{1, 1000};
            if (mFmt->oformat->flags & AVFMT_GLOBALHEADER) mSEnc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            if (avcodec_open2(mSEnc, sc, nullptr) < 0) { mError = "the subtitle encoder refused these settings"; return false; }
            if (avcodec_parameters_from_context(mSStream->codecpar, mSEnc) < 0) { mError = "cannot describe the subtitle stream"; return false; }
            mSStream->time_base = mSEnc->time_base;
        }
        const uint32_t tag = mStream->codecpar->codec_tag;
        if (avcodec_parameters_from_context(mStream->codecpar, mEnc) < 0) { mError = "cannot describe the stream"; return false; }
        if (tag) mStream->codecpar->codec_tag = tag;   // the parameters copy resets it
        if (out == "pq")
        {
            // the container's copy of the mastering display (Matroska writes it; MOV here does not)
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(60, 31, 102)   // FFmpeg 7 moved stream side data onto codecpar
            AVPacketSideData *sd = av_packet_side_data_new(&mStream->codecpar->coded_side_data, &mStream->codecpar->nb_coded_side_data,
                                                           AV_PKT_DATA_MASTERING_DISPLAY_METADATA, sizeof(AVMasteringDisplayMetadata), 0);
            if (auto *md = reinterpret_cast<AVMasteringDisplayMetadata *>(sd ? sd->data : nullptr))
#else
            if (auto *md = reinterpret_cast<AVMasteringDisplayMetadata *>(
                    av_stream_new_side_data(mStream, AV_PKT_DATA_MASTERING_DISPLAY_METADATA, sizeof(AVMasteringDisplayMetadata))))
#endif
            {
                *md = AVMasteringDisplayMetadata{};
                const double prim[3][2] = {{0.708, 0.292}, {0.170, 0.797}, {0.131, 0.046}};
                for (int i = 0; i < 3; ++i)
                {
                    md->display_primaries[i][0] = av_d2q(prim[i][0], 50000);
                    md->display_primaries[i][1] = av_d2q(prim[i][1], 50000);
                }
                md->white_point[0] = av_d2q(0.3127, 50000);
                md->white_point[1] = av_d2q(0.3290, 50000);
                md->max_luminance = av_d2q(spec.peak, 10000);
                md->min_luminance = av_d2q(0.005, 10000);
                md->has_primaries = md->has_luminance = 1;
            }
        }
        if (avio_open(&mFmt->pb, mPath.c_str(), AVIO_FLAG_WRITE) < 0) { mError = "cannot write " + mPath; return false; }
        if (avformat_write_header(mFmt, nullptr) < 0) { mError = "cannot write the container header"; return false; }
        if (mSEnc && !writeSubtitles(spec.subtitles)) return false;   // the muxer interleaves them by time

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
            // D-9: full-range RGB in, BT.709 (BT.2020 for HDR) video-range YUV out — what the stream is tagged as
            const int *coeffs = sws_getCoefficients(mSwsMatrix);
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
        // D-11: ProRes writes its frame header's colour bytes from the FRAME, not the encoder — unset,
        // every ProRes master said "unspecified" whatever the container's colr atom claimed
        mFrame->color_primaries = mEnc->color_primaries;
        mFrame->color_trc = mEnc->color_trc;
        mFrame->colorspace = mEnc->colorspace;
        mFrame->color_range = mEnc->color_range;

        if (avcodec_send_frame(mEnc, mFrame) < 0) { mError = "the encoder rejected a frame"; return false; }
        return drain(false);
    }

    bool FrameWriterFFmpeg::sendAudioFrame(int frames)
    {
        av_frame_unref(mAFrame);
        mAFrame->nb_samples = frames;
        mAFrame->format = mAEnc->sample_fmt;
#if IS_AV_CH_LAYOUT
        av_channel_layout_copy(&mAFrame->ch_layout, &mAEnc->ch_layout);
#else
        mAFrame->channel_layout = mAEnc->channel_layout;
        mAFrame->channels = 2;
#endif
        mAFrame->sample_rate = mAEnc->sample_rate;
        if (av_frame_get_buffer(mAFrame, 0) < 0) { mError = "cannot allocate audio frame storage"; return false; }
        const float *s = mAudioFifo.data();
        if (mAEnc->sample_fmt == AV_SAMPLE_FMT_FLTP)
        {
            float *l = reinterpret_cast<float *>(mAFrame->data[0]), *r = reinterpret_cast<float *>(mAFrame->data[1]);
            for (int i = 0; i < frames; ++i) { l[i] = s[i * 2]; r[i] = s[i * 2 + 1]; }
        }
        else
        {
            // 24-bit PCM rides in the high bits of S32; full scale clips, as any converter does
            int32_t *d = reinterpret_cast<int32_t *>(mAFrame->data[0]);
            for (int i = 0; i < frames * 2; ++i)
                d[i] = (int32_t)std::lround(std::clamp((double)s[i], -1.0, 1.0 - 1.0 / 8388608.0) * 2147483648.0);
        }
        mAFrame->pts = mAudioNext;
        mAudioNext += frames;
        mAudioFifo.erase(mAudioFifo.begin(), mAudioFifo.begin() + (size_t)frames * 2);
        if (avcodec_send_frame(mAEnc, mAFrame) < 0) { mError = "the audio encoder rejected a frame"; return false; }
        return drainAudio(false);
    }

    bool FrameWriterFFmpeg::drainAudio(bool flush)
    {
        for (;;)
        {
            const int r = avcodec_receive_packet(mAEnc, mPkt);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return true;
            if (r < 0) { mError = "the audio encoder failed"; return false; }
            av_packet_rescale_ts(mPkt, mAEnc->time_base, mAStream->time_base);
            mPkt->stream_index = mAStream->index;
            const int w = av_interleaved_write_frame(mFmt, mPkt);
            av_packet_unref(mPkt);
            if (w < 0) { mError = "cannot write an audio packet"; return false; }
            (void)flush;
        }
    }

    bool FrameWriterFFmpeg::writeSubtitles(const std::vector<interstellar::EncodeSpec::Cue> &cues)
    {
        std::vector<uint8_t> buf(1 << 16);
        long long lastMs = -1;
        int order = 0;
        for (const auto &c : cues)
        {
            long long startMs = std::llround(c.start * 1000.0);
            const long long endMs = std::llround(c.end * 1000.0);
            startMs = std::max(startMs, lastMs + 1);   // a muxer takes each stream's times strictly in order
            if (endMs <= startMs) continue;
            lastMs = startMs;
            std::string text;
            for (const char ch : c.text)
                if (ch == '\n') text += "\\N";        // an ASS line break
                else if (ch != '\r') text += ch;
            AVSubtitle sub{};
            sub.format = 1;                          // text
            sub.pts = startMs * 1000;                // AV_TIME_BASE
            sub.end_display_time = (uint32_t)(endMs - startMs);
            sub.num_rects = 1;
            sub.rects = static_cast<AVSubtitleRect **>(av_mallocz(sizeof(AVSubtitleRect *)));
            if (sub.rects) sub.rects[0] = static_cast<AVSubtitleRect *>(av_mallocz(sizeof(AVSubtitleRect)));
            if (!sub.rects || !sub.rects[0]) { avsubtitle_free(&sub); mError = "cannot allocate a subtitle"; return false; }
            sub.rects[0]->type = SUBTITLE_ASS;
            sub.rects[0]->ass = av_strdup((std::to_string(order++) + ",0,Default,,0,0,0,," + text).c_str());   // ReadOrder,Layer,Style,…,Text
            const int n = avcodec_encode_subtitle(mSEnc, buf.data(), (int)buf.size(), &sub);
            avsubtitle_free(&sub);
            if (n < 0) { mError = "the subtitle encoder rejected a caption"; return false; }
            AVPacket *pkt = av_packet_alloc();
            if (!pkt || av_new_packet(pkt, n) < 0) { av_packet_free(&pkt); mError = "cannot allocate a subtitle packet"; return false; }
            std::copy_n(buf.data(), n, pkt->data);
            pkt->stream_index = mSStream->index;
            pkt->pts = pkt->dts = av_rescale_q(startMs, AVRational{1, 1000}, mSStream->time_base);
            pkt->duration = av_rescale_q(endMs - startMs, AVRational{1, 1000}, mSStream->time_base);
            const int w = av_interleaved_write_frame(mFmt, pkt);
            av_packet_free(&pkt);
            if (w < 0) { mError = "cannot write a subtitle packet"; return false; }
        }
        return true;
    }

    bool FrameWriterFFmpeg::writeAudio(const float *stereo, int frames)
    {
        if (!mOpen) return false;
        if (!mAEnc || frames <= 0) return true;   // begun without sound
        mAudioFifo.insert(mAudioFifo.end(), stereo, stereo + (size_t)frames * 2);
        while ((int)(mAudioFifo.size() / 2) >= mAudioFrameSize)
            if (!sendAudioFrame(mAudioFrameSize)) return false;
        return true;
    }

    bool FrameWriterFFmpeg::end()
    {
        if (!mOpen) return false;
        if (mAEnc)
        {
            // the tail: a short last frame where the encoder allows one, else padded with silence
            const int rest = (int)(mAudioFifo.size() / 2);
            if (rest > 0)
            {
                if (!(mAEnc->codec->capabilities & AV_CODEC_CAP_SMALL_LAST_FRAME) && !(mAEnc->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE))
                    mAudioFifo.resize((size_t)mAudioFrameSize * 2, 0.0f);
                sendAudioFrame((int)(mAudioFifo.size() / 2));
            }
            avcodec_send_frame(mAEnc, nullptr);
            drainAudio(true);
        }
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
