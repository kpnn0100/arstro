#include "VideoFiles.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>

#ifdef SOLARIS_HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#ifdef SOLARIS_HAVE_SWSCALE
#include <libswscale/swscale.h>
#endif
}
// FFmpeg 5.1 replaced channel_layout/channels with AVChannelLayout (AudioFiles.cpp, the same switch)
#define SOLARIS_AV_CH_LAYOUT (LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100))
#endif

namespace arstro
{
namespace solaris_host
{
#ifdef SOLARIS_HAVE_FFMPEG
    namespace
    {
        // the file's picture: its best video stream that is not a cover image (an MP3's art is a "video" stream too)
        int pictureStream(AVFormatContext *f)
        {
            const int best = av_find_best_stream(f, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
            if (best >= 0 && !(f->streams[best]->disposition & AV_DISPOSITION_ATTACHED_PIC)) return best;
            for (unsigned i = 0; i < f->nb_streams; ++i)
                if (f->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !(f->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC)) return (int)i;
            return -1;
        }

        struct Input
        {
            AVFormatContext *f = nullptr;
            int s = -1;
            ~Input()
            {
                if (f) avformat_close_input(&f);
            }
            bool open(const std::string &path, std::string &err)
            {
                if (avformat_open_input(&f, path.c_str(), nullptr, nullptr) < 0)
                {
                    f = nullptr;
                    err = "not a file FFmpeg reads";
                    return false;
                }
                if (avformat_find_stream_info(f, nullptr) < 0) { err = "its streams cannot be read"; return false; }
                const std::string kind = f->iformat && f->iformat->name ? f->iformat->name : "";
                if (kind == "image2" || (kind.size() > 5 && kind.compare(kind.size() - 5, 5, "_pipe") == 0)) { err = "a still image, not a video"; return false; }
                s = pictureStream(f);
                if (s < 0) { err = "it has no picture"; return false; }
                return true;
            }
            AVStream *st() const { return f->streams[s]; }
            int64_t startTs() const { return st()->start_time == AV_NOPTS_VALUE ? 0 : st()->start_time; }
            double duration() const
            {
                if (st()->duration != AV_NOPTS_VALUE && st()->duration > 0) return (double)st()->duration * av_q2d(st()->time_base);
                return f->duration != AV_NOPTS_VALUE && f->duration > 0 ? (double)f->duration / AV_TIME_BASE : 0.0;
            }
            double fps() const
            {
                const AVRational r = av_guess_frame_rate(f, st(), nullptr);
                return r.num > 0 && r.den > 0 ? av_q2d(r) : 0.0;
            }
        };
    }
#endif

    bool probeVideo(const std::string &path, solaris::VideoInfo &info, std::string &err)
    {
#ifndef SOLARIS_HAVE_FFMPEG
        (void)path; (void)info;
        err = "this build has no FFmpeg, so it reads no video";
        return false;
#else
        Input in;
        if (!in.open(path, err)) return false;
        info.duration = in.duration();
        info.fps = in.fps();
        info.width = in.st()->codecpar->width;
        info.height = in.st()->codecpar->height;
        if (info.width <= 0 || info.height <= 0) { err = "its picture has no size"; return false; }
        return true;
#endif
    }

    // ---------------------------------------------------------------------------------------------------------------
    // the frame decoder: `cur` shows from curPts until `nxt` (one frame ahead, so where cur ends is known)

    struct VideoReader::Impl
    {
        int seeks = 0;
#ifdef SOLARIS_HAVE_FFMPEG
        Input in;
        AVCodecContext *cc = nullptr;
        AVFrame *cur = nullptr, *nxt = nullptr;
        AVPacket *pkt = nullptr;
        int64_t curPts = 0, nxtPts = 0;
        bool haveCur = false, haveNext = false, draining = false, eof = false;
#ifdef SOLARIS_HAVE_SWSCALE
        SwsContext *sws = nullptr;
#endif
        ~Impl()
        {
#ifdef SOLARIS_HAVE_SWSCALE
            sws_freeContext(sws);
#endif
            av_frame_free(&cur);
            av_frame_free(&nxt);
            av_packet_free(&pkt);
            avcodec_free_context(&cc);
        }
        // the next decoded frame into `nxt`; false at the picture's end
        bool pull()
        {
            while (true)
            {
                const int r = avcodec_receive_frame(cc, nxt);
                if (r == 0)
                {
                    nxtPts = nxt->best_effort_timestamp != AV_NOPTS_VALUE ? nxt->best_effort_timestamp : nxt->pts != AV_NOPTS_VALUE ? nxt->pts : haveCur ? curPts + 1 : in.startTs();
                    haveNext = true;
                    return true;
                }
                if (r != AVERROR(EAGAIN) || draining) // the end (or a broken stream): the last frame stays
                {
                    eof = true;
                    return false;
                }
                while (true) // feed it a packet of the picture
                {
                    if (av_read_frame(in.f, pkt) < 0)
                    {
                        avcodec_send_packet(cc, nullptr); // the end: what it holds comes out
                        draining = true;
                        break;
                    }
                    const bool mine = pkt->stream_index == in.s;
                    if (mine) avcodec_send_packet(cc, pkt);
                    av_packet_unref(pkt);
                    if (mine) break;
                }
            }
        }
        void seek(int64_t target)
        {
            av_seek_frame(in.f, in.s, target, AVSEEK_FLAG_BACKWARD); // the keyframe at or before it
            avcodec_flush_buffers(cc);
            haveCur = haveNext = draining = eof = false;
            ++seeks;
        }
#endif
    };

    VideoReader::VideoReader() : d(new Impl()) {}
    VideoReader::~VideoReader() = default;
    int VideoReader::seeks() const { return d->seeks; }

    bool VideoReader::open(const std::string &path, std::string &err)
    {
#ifndef SOLARIS_HAVE_FFMPEG
        (void)path;
        err = "this build has no FFmpeg, so it reads no video";
        return false;
#else
        d.reset(new Impl());
        if (!d->in.open(path, err)) return false;
        const AVCodecParameters *par = d->in.st()->codecpar;
        const AVCodec *codec = avcodec_find_decoder(par->codec_id);
        if (!codec) { err = std::string("this FFmpeg cannot decode ") + avcodec_get_name(par->codec_id); return false; }
        d->cc = avcodec_alloc_context3(codec);
        d->cur = av_frame_alloc();
        d->nxt = av_frame_alloc();
        d->pkt = av_packet_alloc();
        if (!d->cc || !d->cur || !d->nxt || !d->pkt) { err = "out of memory"; return false; }
        if (avcodec_parameters_to_context(d->cc, par) < 0 || avcodec_open2(d->cc, codec, nullptr) < 0) { err = std::string("cannot open the ") + codec->name + " decoder"; return false; }
        return true;
#endif
    }

    bool VideoReader::frame(double t, int boxW, int boxH, VideoFrame &out, std::string &err)
    {
#ifndef SOLARIS_HAVE_FFMPEG
        (void)t; (void)boxW; (void)boxH; (void)out;
        err = "this build has no FFmpeg, so it reads no video";
        return false;
#else
        Impl &m = *d;
        if (!m.cc) { err = "no video is open"; return false; }
        const double tb = av_q2d(m.in.st()->time_base);
        const int64_t target = m.in.startTs() + (int64_t)std::llround(std::max(0.0, t) / tb);
        // forward from the frame showing, as playing does; back, or far ahead, from the keyframe before it
        if (!m.haveCur || target < m.curPts || (double)(target - m.curPts) * tb > 2.0) m.seek(target);
        while (true)
        {
            if (!m.haveNext && !m.pull()) break;   // the end: the last frame stays
            if (m.haveCur && m.nxtPts > target) break; // the frame showing covers it
            std::swap(m.cur, m.nxt);
            m.curPts = m.nxtPts;
            m.haveCur = true;
            m.haveNext = false;
        }
        if (!m.haveCur) { err = "no frame to show"; return false; }
#ifndef SOLARIS_HAVE_SWSCALE
        (void)boxW; (void)boxH; (void)out;
        err = "this build has no libswscale, so it scales no picture";
        return false;
#else
        const int w = m.cur->width, h = m.cur->height;
        if (w <= 0 || h <= 0 || boxW <= 0 || boxH <= 0) { err = "nothing to draw into"; return false; }
        const double k = std::min((double)boxW / w, (double)boxH / h); // fitted, its aspect kept
        const int ow = std::max(1, (int)std::lround(w * k)), oh = std::max(1, (int)std::lround(h * k));
        m.sws = sws_getCachedContext(m.sws, w, h, (AVPixelFormat)m.cur->format, ow, oh, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!m.sws) { err = "cannot scale its picture"; return false; }
        out.width = ow;
        out.height = oh;
        out.rgba.assign((size_t)ow * (size_t)oh * 4, 0);
        uint8_t *dst[4] = {out.rgba.data(), nullptr, nullptr, nullptr};
        int stride[4] = {ow * 4, 0, 0, 0};
        sws_scale(m.sws, m.cur->data, m.cur->linesize, 0, h, dst, stride);
        out.time = (double)(m.curPts - m.in.startTs()) * tb;
        return true;
#endif
#endif
    }

    // ---------------------------------------------------------------------------------------------------------------

    bool muxVideo(const std::string &video, double startAt, const std::vector<std::vector<float>> &mix, int rate,
                  const std::string &out, std::string &err)
    {
#ifndef SOLARIS_HAVE_FFMPEG
        (void)video; (void)startAt; (void)mix; (void)rate; (void)out;
        err = "this build has no FFmpeg, so it writes no video";
        return false;
#else
        if (mix.size() != 2 || rate <= 0) { err = "nothing to write"; return false; }
        if (av_log_get_level() > AV_LOG_WARNING) av_log_set_level(AV_LOG_WARNING); // the AAC encoder's statistics are no one's business
        Input in;
        if (!in.open(video, err)) { err = "cannot read " + video + ": " + err; return false; }
        AVStream *ist = in.st();
        const double mixLen = (double)mix[0].size() / rate, picLen = in.duration();
        if (startAt >= mixLen) { err = "the picture starts after the render ends"; return false; }
        if (picLen > 0 && startAt + picLen <= 0) { err = "the picture ends before the render starts"; return false; }
        std::string ext = std::filesystem::path(out).extension().string();
        for (auto &ch : ext) ch = (char)std::tolower((unsigned char)ch);
        const bool mkv = ext == ".mkv";
        AVFormatContext *of = nullptr;
        if (avformat_alloc_output_context2(&of, nullptr, mkv ? "matroska" : ext == ".mov" ? "mov" : "mp4", out.c_str()) < 0 || !of)
        {
            err = "cannot make " + out;
            return false;
        }
        const AVCodec *codec = avcodec_find_encoder(mkv ? AV_CODEC_ID_FLAC : AV_CODEC_ID_AAC);
        AVCodecContext *cc = codec ? avcodec_alloc_context3(codec) : nullptr;
        AVFrame *fr = av_frame_alloc();
        AVPacket *vp = av_packet_alloc(), *ap = av_packet_alloc();
        bool ok = false;
        do
        {
            if (!codec) { err = std::string("this FFmpeg has no ") + (mkv ? "FLAC" : "AAC") + " encoder"; break; }
            if (!cc || !fr || !vp || !ap) { err = "out of memory"; break; }
            if (avformat_query_codec(of->oformat, ist->codecpar->codec_id, FF_COMPLIANCE_NORMAL) == 0)
            {
                err = std::string("a ") + avcodec_get_name(ist->codecpar->codec_id) + " picture does not go in a " + ext + " file — try .mkv";
                break;
            }
            // the picture: its own stream, copied
            AVStream *vs = avformat_new_stream(of, nullptr);
            if (!vs || avcodec_parameters_copy(vs->codecpar, ist->codecpar) < 0) { err = "cannot describe the picture"; break; }
            vs->codecpar->codec_tag = 0; // the container's own tag for the codec
            vs->time_base = ist->time_base;
            // the mix: AAC (float, planar) or FLAC (24 bits in the top of 32)
            const AVSampleFormat sf = mkv ? AV_SAMPLE_FMT_S32 : AV_SAMPLE_FMT_FLTP;
            cc->sample_fmt = sf;
            cc->sample_rate = rate;
            cc->time_base = AVRational{1, rate};
#if SOLARIS_AV_CH_LAYOUT
            av_channel_layout_default(&cc->ch_layout, 2);
#else
            cc->channel_layout = AV_CH_LAYOUT_STEREO;
            cc->channels = 2;
#endif
            if (mkv) cc->bits_per_raw_sample = 24;
            else cc->bit_rate = 256000;
            if (of->oformat->flags & AVFMT_GLOBALHEADER) cc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            if (avcodec_open2(cc, codec, nullptr) < 0) { err = std::string("cannot open the ") + codec->name + " encoder at " + std::to_string(rate) + " Hz"; break; }
            AVStream *as = avformat_new_stream(of, nullptr);
            if (!as || avcodec_parameters_from_context(as->codecpar, cc) < 0) { err = "cannot describe the mix"; break; }
            as->time_base = cc->time_base;
            if (!(of->oformat->flags & AVFMT_NOFILE) && avio_open(&of->pb, out.c_str(), AVIO_FLAG_WRITE) < 0) { err = "cannot write " + out; break; }
            if (avformat_write_header(of, nullptr) < 0) { err = "cannot write the " + ext + " header"; break; }

            // the mix, a block at a time, up to a time — so its packets and the picture's arrive interleaved
            const size_t frames = mix[0].size();
            const bool smallLast = codec->capabilities & (AV_CODEC_CAP_SMALL_LAST_FRAME | AV_CODEC_CAP_VARIABLE_FRAME_SIZE);
            const int block = cc->frame_size > 0 ? cc->frame_size : 4096;
            size_t apos = 0;
            auto drain = [&]() -> bool {
                while (true)
                {
                    const int r = avcodec_receive_packet(cc, ap);
                    if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return true;
                    if (r < 0) return false;
                    av_packet_rescale_ts(ap, cc->time_base, as->time_base);
                    ap->stream_index = as->index;
                    if (av_interleaved_write_frame(of, ap) < 0) return false;
                }
            };
            auto mixUpTo = [&](double sec) -> bool {
                while (apos < frames && (double)apos / rate < sec)
                {
                    const int have = (int)std::min<size_t>((size_t)block, frames - apos);
                    const int n = smallLast ? have : block; // a codec that needs whole frames: the last one padded with silence
                    av_frame_unref(fr);
                    fr->nb_samples = n;
                    fr->format = sf;
                    fr->sample_rate = rate;
#if SOLARIS_AV_CH_LAYOUT
                    av_channel_layout_copy(&fr->ch_layout, &cc->ch_layout);
#else
                    fr->channel_layout = cc->channel_layout;
                    fr->channels = 2;
#endif
                    if (av_frame_get_buffer(fr, 0) < 0 || av_frame_make_writable(fr) < 0) return false;
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < n; ++i)
                        {
                            const float x = i < have ? mix[(size_t)ch][apos + (size_t)i] : 0.0f;
                            if (mkv) ((int32_t *)fr->data[0])[i * 2 + ch] = (int32_t)std::max(-8388608.0, std::min(8388607.0, (double)std::lround((double)x * 8388608.0))) * 256;
                            else ((float *)fr->data[ch])[i] = x;
                        }
                    fr->pts = (int64_t)apos;
                    if (avcodec_send_frame(cc, fr) < 0 || !drain()) return false;
                    apos += (size_t)n;
                }
                return true;
            };

            // the picture's packets: a frame `t` into it lands at t + startAt in the file (the input's time base)
            const double tb = av_q2d(ist->time_base);
            const int64_t shift = (int64_t)std::llround(startAt / tb) - in.startTs();
            const int64_t endTs = (int64_t)std::llround(mixLen / tb);
            bool started = startAt >= 0, failed = false;
            int64_t firstAt = std::numeric_limits<int64_t>::min();
            int written = 0;
            while (!failed && av_read_frame(in.f, vp) >= 0)
            {
                if (vp->stream_index != in.s) { av_packet_unref(vp); continue; }
                const int64_t pts = vp->pts != AV_NOPTS_VALUE ? vp->pts : vp->dts;
                // cut where the mix ends, in DECODE order: every frame decoded before it is kept, so each that shows
                // decodes (a B-frame's forward reference with it — shown a frame or two past the end, never broken)
                const int64_t order = vp->dts != AV_NOPTS_VALUE ? vp->dts : pts;
                if (order != AV_NOPTS_VALUE && order + shift >= endTs) { av_packet_unref(vp); break; }
                const int64_t at = pts == AV_NOPTS_VALUE ? firstAt : pts + shift;
                if (!started) // cut: the first keyframe at or after the render's start, so what shows decodes
                {
                    if (!(vp->flags & AV_PKT_FLAG_KEY) || at < 0) { av_packet_unref(vp); continue; }
                    started = true;
                    firstAt = at;
                }
                if (at < firstAt) { av_packet_unref(vp); continue; } // an open GOP's lead-in: it leans on what was cut
                if (!mixUpTo((double)at * tb)) { failed = true; break; }
                if (vp->pts != AV_NOPTS_VALUE) vp->pts += shift;
                if (vp->dts != AV_NOPTS_VALUE) vp->dts += shift;
                vp->stream_index = vs->index;
                vp->pos = -1;
                av_packet_rescale_ts(vp, ist->time_base, vs->time_base);
                if (av_interleaved_write_frame(of, vp) < 0) { failed = true; break; }
                ++written;
            }
            av_packet_unref(vp);
            if (failed) { err = "the picture or the mix could not be written"; break; }
            if (written == 0) { err = "no frame of the picture falls in the render"; break; }
            if (!mixUpTo(std::numeric_limits<double>::infinity()) || avcodec_send_frame(cc, nullptr) < 0 || !drain()) { err = "the mix's encoder failed"; break; }
            if (av_write_trailer(of) < 0) { err = "cannot finish " + out; break; }
            ok = true;
        } while (false);
        av_packet_free(&vp);
        av_packet_free(&ap);
        av_frame_free(&fr);
        avcodec_free_context(&cc);
        if (!(of->oformat->flags & AVFMT_NOFILE)) avio_closep(&of->pb);
        avformat_free_context(of);
        return ok;
#endif
    }
}
}
