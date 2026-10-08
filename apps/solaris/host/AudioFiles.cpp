#include "AudioFiles.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

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
        const int bytes = flt ? 4 : 3;
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
                    // TPDF-free rounding to 24 bits, clipped (a render writes what was mixed; the meter says if it clipped)
                    const double c = std::max(-1.0, std::min(1.0, (double)s));
                    const int32_t v = (int32_t)std::lround(c * 8388607.0);
                    buf.push_back((uint8_t)(v & 0xff));
                    buf.push_back((uint8_t)((v >> 8) & 0xff));
                    buf.push_back((uint8_t)((v >> 16) & 0xff));
                }
            }
        const bool ok = std::fwrite(buf.data(), 1, buf.size(), f) == buf.size();
        std::fclose(f);
        if (!ok) err = "short write to " + path;
        return ok;
    }
}
}
