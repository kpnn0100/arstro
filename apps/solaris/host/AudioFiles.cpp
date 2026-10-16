#include "AudioFiles.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>

#ifdef SOLARIS_HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}
// FFmpeg 5.1 replaced channel_layout/channels with AVChannelLayout (Interstellar's host, the same switch)
#define SOLARIS_AV_CH_LAYOUT (LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100))
#endif

namespace arstro
{
namespace solaris_host
{
    bool decodeAudio(const std::string &path, int rate, solaris::engine::Pcm &out, std::string &err)
    {
#ifndef SOLARIS_HAVE_FFMPEG
        (void)path; (void)rate; (void)out;
        err = "this build has no FFmpeg, so it cannot decode audio";
        return false;
#else
        AVFormatContext *fmt = nullptr;
        if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) { err = "cannot open " + path; return false; }
        struct Closer
        {
            AVFormatContext *&f;
            AVCodecContext *dec = nullptr;
            SwrContext *swr = nullptr;
            AVFrame *frame = nullptr;
            AVPacket *pkt = nullptr;
            ~Closer()
            {
                if (swr) swr_free(&swr);
                if (frame) av_frame_free(&frame);
                if (pkt) av_packet_free(&pkt);
                if (dec) avcodec_free_context(&dec);
                if (f) avformat_close_input(&f);
            }
        } c{fmt};
        if (avformat_find_stream_info(fmt, nullptr) < 0) { err = "cannot read " + path; return false; }
#if LIBAVFORMAT_VERSION_MAJOR >= 59
        const AVCodec *codec = nullptr;
#else
        AVCodec *codec = nullptr;
#endif
        const int stream = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
        if (stream < 0 || !codec) { err = path + " has no audio"; return false; }
        c.dec = avcodec_alloc_context3(codec);
        if (!c.dec || avcodec_parameters_to_context(c.dec, fmt->streams[stream]->codecpar) < 0 || avcodec_open2(c.dec, codec, nullptr) < 0)
        { err = "cannot decode " + path; return false; }
#if SOLARIS_AV_CH_LAYOUT
        const int channels = c.dec->ch_layout.nb_channels;
        AVChannelLayout inLayout{}, outLayout{};
        if (c.dec->ch_layout.order != AV_CHANNEL_ORDER_UNSPEC) av_channel_layout_copy(&inLayout, &c.dec->ch_layout);
        else av_channel_layout_default(&inLayout, channels);
        av_channel_layout_from_mask(&outLayout, AV_CH_LAYOUT_STEREO);
        if (swr_alloc_set_opts2(&c.swr, &outLayout, AV_SAMPLE_FMT_FLT, rate, &inLayout, c.dec->sample_fmt, c.dec->sample_rate, 0, nullptr) < 0)
            swr_free(&c.swr);
        av_channel_layout_uninit(&inLayout);
#else
        const int channels = c.dec->channels;
        const int64_t inLayout = c.dec->channel_layout ? (int64_t)c.dec->channel_layout : av_get_default_channel_layout(channels);
        c.swr = swr_alloc_set_opts(nullptr, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_FLT, rate, inLayout, c.dec->sample_fmt, c.dec->sample_rate, 0, nullptr);
#endif
        // libswresample folds pure mono at sqrt(1/2); the suite's centre is unity, so set it outright
        const double monoToStereo[2] = {1.0, 1.0};
        if (c.swr && channels == 1) swr_set_matrix(c.swr, monoToStereo, 1);
        if (!c.swr || swr_init(c.swr) < 0) { err = "cannot resample " + path; return false; }
        c.frame = av_frame_alloc();
        c.pkt = av_packet_alloc();

        std::vector<float> pcm;
        auto convert = [&](AVFrame *f) {
            const int in = f ? f->nb_samples : 0;
            const int cap = swr_get_out_samples(c.swr, in);
            if (cap <= 0) return 0;
            const size_t at = pcm.size();
            pcm.resize(at + (size_t)cap * 2);
            uint8_t *o = reinterpret_cast<uint8_t *>(pcm.data() + at);
            const int got = swr_convert(c.swr, &o, cap, f ? (const uint8_t **)f->extended_data : nullptr, in);
            pcm.resize(at + (size_t)std::max(0, got) * 2);
            return got;
        };
        auto drainFrames = [&]() {
            while (avcodec_receive_frame(c.dec, c.frame) == 0)
            {
                convert(c.frame);
                av_frame_unref(c.frame);
            }
        };
        while (av_read_frame(fmt, c.pkt) >= 0)
        {
            if (c.pkt->stream_index == stream && avcodec_send_packet(c.dec, c.pkt) >= 0) drainFrames();
            av_packet_unref(c.pkt);
        }
        avcodec_send_packet(c.dec, nullptr);
        drainFrames();
        while (convert(nullptr) > 0) {} // the resampler's tail
        out.channels = 2;
        out.frames = (long long)(pcm.size() / 2);
        out.samples = std::move(pcm);
        if (out.frames <= 0) { err = path + " decoded to nothing"; return false; }
        return true;
#endif
    }

    bool writeWav(const std::string &path, const std::vector<std::vector<float>> &channels, int rate, int bits, std::string &err)
    {
        const int nch = (int)channels.size();
        if (nch < 1) { err = "nothing to write"; return false; }
        const size_t frames = channels[0].size();
        const bool flt = bits == 32;
        const int bytes = flt ? 4 : bits == 16 ? 2 : 3;
        const double scale = bits == 16 ? 32768.0 : 8388608.0; // 2^(bits−1): the dither's grid lands on integers
        {
            // R-REC-1: a recording goes into a folder beside the song that may not be there yet
            std::error_code ec;
            const auto dir = std::filesystem::path(path).parent_path();
            if (!dir.empty()) std::filesystem::create_directories(dir, ec);
        }
        FILE *f = std::fopen(path.c_str(), "wb");
        if (!f) { err = "cannot write " + path; return false; }
        auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
        auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
        const uint32_t data = (uint32_t)(frames * (size_t)nch * (size_t)bytes);
        std::fwrite("RIFF", 1, 4, f); u32(36 + data); std::fwrite("WAVE", 1, 4, f);
        std::fwrite("fmt ", 1, 4, f); u32(16); u16(flt ? 3 : 1); u16((uint16_t)nch);
        u32((uint32_t)rate); u32((uint32_t)(rate * nch * bytes)); u16((uint16_t)(nch * bytes)); u16((uint16_t)bits);
        std::fwrite("data", 1, 4, f); u32(data);
        std::vector<uint8_t> buf;
        buf.reserve(frames * (size_t)nch * (size_t)bytes);
        for (size_t i = 0; i < frames; ++i)
            for (int ch = 0; ch < nch; ++ch)
            {
                const float s = i < channels[ch].size() ? channels[ch][i] : 0.0f;
                if (flt)
                {
                    uint8_t b[4];
                    std::memcpy(b, &s, 4);
                    buf.insert(buf.end(), b, b + 4);
                }
                else
                {
                    // the service dithered onto this grid (R-EXPORT-1); here only the integer, clipped
                    const int32_t v = (int32_t)std::max(-scale, std::min(scale - 1.0, (double)std::lround((double)s * scale)));
                    buf.push_back((uint8_t)(v & 0xff));
                    buf.push_back((uint8_t)((v >> 8) & 0xff));
                    if (bytes == 3) buf.push_back((uint8_t)((v >> 16) & 0xff));
                }
            }
        const bool ok = std::fwrite(buf.data(), 1, buf.size(), f) == buf.size();
        std::fclose(f);
        if (!ok) err = "short write to " + path;
        return ok;
    }

    bool writeAudio(const std::string &path, const std::vector<std::vector<float>> &channels, int rate,
                    const std::string &format, int bits, int kbps, std::string &err)
    {
#ifndef SOLARIS_HAVE_FFMPEG
        (void)path; (void)channels; (void)rate; (void)bits; (void)kbps;
        err = "this build has no FFmpeg, so it cannot write " + format;
        return false;
#else
        const int nch = (int)channels.size();
        if (nch < 1 || nch > 2) { err = "nothing to write"; return false; }
        const size_t frames = channels[0].size();
        const char *encoder = format == "flac" ? "flac" : format == "mp3" ? "libmp3lame" : format == "ogg" ? "libvorbis" : format == "opus" ? "libopus" : nullptr;
        if (!encoder) { err = "no encoder for `" + format + "`"; return false; }
        const AVCodec *codec = avcodec_find_encoder_by_name(encoder);
        if (!codec) { err = std::string("this FFmpeg has no ") + encoder + " encoder"; return false; }
        AVFormatContext *fmt = nullptr;
        if (avformat_alloc_output_context2(&fmt, nullptr, format == "ogg" ? "ogg" : format.c_str(), path.c_str()) < 0 || !fmt)
        {
            err = "cannot make a " + format + " file";
            return false;
        }
        AVCodecContext *cc = avcodec_alloc_context3(codec);
        AVFrame *fr = av_frame_alloc();
        AVPacket *pkt = av_packet_alloc();
        bool ok = false;
        auto done = [&](const std::string &e) {
            err = e;
            return false;
        };
        do
        {
            AVStream *st = avformat_new_stream(fmt, nullptr);
            if (!st || !cc || !fr || !pkt) { done("out of memory"); break; }
            // the sample format: FLAC takes integers (16, or 24 in the top of 32); the lossy ones float, else 16
            const bool flac = format == "flac";
            AVSampleFormat want[] = {AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_S16P, AV_SAMPLE_FMT_S16};
            AVSampleFormat sf = AV_SAMPLE_FMT_NONE;
            if (flac) sf = bits == 16 ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_S32;
            else
                for (AVSampleFormat w : want)
                    for (const AVSampleFormat *p = codec->sample_fmts; p && *p != AV_SAMPLE_FMT_NONE && sf == AV_SAMPLE_FMT_NONE; ++p)
                        if (*p == w) sf = w;
            if (sf == AV_SAMPLE_FMT_NONE) { done(std::string(encoder) + " takes no sample format this writer has"); break; }
            cc->sample_fmt = sf;
            cc->sample_rate = rate;
            cc->time_base = AVRational{1, rate};
#if SOLARIS_AV_CH_LAYOUT
            av_channel_layout_default(&cc->ch_layout, nch);
#else
            cc->channel_layout = (uint64_t)av_get_default_channel_layout(nch);
            cc->channels = nch;
#endif
            if (flac && bits == 24) cc->bits_per_raw_sample = 24;
            if (!flac) cc->bit_rate = (int64_t)kbps * 1000;
            if (fmt->oformat->flags & AVFMT_GLOBALHEADER) cc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            if (avcodec_open2(cc, codec, nullptr) < 0) { done(std::string("cannot open the ") + encoder + " encoder at " + std::to_string(rate) + " Hz"); break; }
            if (avcodec_parameters_from_context(st->codecpar, cc) < 0) { done("cannot describe the stream"); break; }
            st->time_base = cc->time_base;
            if (!(fmt->oformat->flags & AVFMT_NOFILE) && avio_open(&fmt->pb, path.c_str(), AVIO_FLAG_WRITE) < 0) { done("cannot write " + path); break; }
            if (avformat_write_header(fmt, nullptr) < 0) { done("cannot write the " + format + " header"); break; }
            const bool variable = codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE;
            const bool smallLast = codec->capabilities & AV_CODEC_CAP_SMALL_LAST_FRAME;
            const int block = variable || cc->frame_size <= 0 ? 4096 : cc->frame_size;
            auto drain = [&]() -> bool {
                while (true)
                {
                    const int r = avcodec_receive_packet(cc, pkt);
                    if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return true;
                    if (r < 0) return false;
                    av_packet_rescale_ts(pkt, cc->time_base, st->time_base);
                    pkt->stream_index = st->index;
                    if (av_interleaved_write_frame(fmt, pkt) < 0) return false;
                }
            };
            bool failed = false;
            for (size_t pos = 0; pos < frames && !failed; pos += (size_t)block)
            {
                const int have = (int)std::min<size_t>((size_t)block, frames - pos);
                const int n = smallLast || variable ? have : block; // a codec that needs whole frames: the last one padded with silence
                av_frame_unref(fr);
                fr->nb_samples = n;
                fr->format = sf;
                fr->sample_rate = rate;
#if SOLARIS_AV_CH_LAYOUT
                av_channel_layout_copy(&fr->ch_layout, &cc->ch_layout);
#else
                fr->channel_layout = cc->channel_layout;
                fr->channels = nch;
#endif
                if (av_frame_get_buffer(fr, 0) < 0 || av_frame_make_writable(fr) < 0) { failed = true; break; }
                const bool planar = av_sample_fmt_is_planar(sf);
                for (int ch = 0; ch < nch; ++ch)
                    for (int i = 0; i < n; ++i)
                    {
                        const float x = i < have ? channels[(size_t)ch][pos + (size_t)i] : 0.0f;
                        const int at = planar ? i : i * nch + ch;
                        uint8_t *base = fr->data[planar ? ch : 0];
                        switch (sf)
                        {
                        case AV_SAMPLE_FMT_FLTP: case AV_SAMPLE_FMT_FLT: ((float *)base)[at] = x; break;
                        case AV_SAMPLE_FMT_S16P: case AV_SAMPLE_FMT_S16:
                            ((int16_t *)base)[at] = (int16_t)std::max(-32768.0, std::min(32767.0, (double)std::lround((double)x * 32768.0)));
                            break;
                        default: // S32: 24 bits in the top of 32 (FFmpeg's FLAC reads them there)
                            ((int32_t *)base)[at] = (int32_t)std::max(-8388608.0, std::min(8388607.0, (double)std::lround((double)x * 8388608.0))) * 256;
                            break;
                        }
                    }
                fr->pts = (int64_t)pos;
                if (avcodec_send_frame(cc, fr) < 0 || !drain()) failed = true;
            }
            if (failed) { done("the " + format + " encoder failed"); break; }
            if (avcodec_send_frame(cc, nullptr) < 0 || !drain()) { done("the " + format + " encoder failed to finish"); break; }
            if (av_write_trailer(fmt) < 0) { done("cannot finish " + path); break; }
            ok = true;
        } while (false);
        av_packet_free(&pkt);
        av_frame_free(&fr);
        avcodec_free_context(&cc);
        if (fmt && !(fmt->oformat->flags & AVFMT_NOFILE)) avio_closep(&fmt->pb);
        avformat_free_context(fmt);
        return ok;
#endif
    }
}
}
