#include "AudioSourceFFmpeg.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}
#include <algorithm>
#include <cmath>
#include <cstring>

namespace arstro
{
namespace interstellar_host
{
    AudioSourceFFmpeg::~AudioSourceFFmpeg() { closeAll(); }

    void AudioSourceFFmpeg::closeAll()
    {
        if (mSwr) swr_free(&mSwr);
        if (mFrame) av_frame_free(&mFrame);
        if (mPkt) av_packet_free(&mPkt);
        if (mDec) avcodec_free_context(&mDec);
        if (mFmt) avformat_close_input(&mFmt);
        mStream = -1;
        mBuf.clear();
        mPositioned = false;
        mEof = false;
        mDraining = false;
    }

    bool AudioSourceFFmpeg::open(const std::string &path, int rate, Info &out)
    {
        closeAll();
        out = Info{};
        mRate = rate > 0 ? rate : 48000;
        out.rate = mRate;
        if (avformat_open_input(&mFmt, path.c_str(), nullptr, nullptr) < 0) return false;
        if (avformat_find_stream_info(mFmt, nullptr) < 0) { closeAll(); return false; }
        AVCodec *codec = nullptr;   // FFmpeg 4.4 takes a non-const pointer here
        mStream = av_find_best_stream(mFmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
        if (mStream < 0 || !codec) { closeAll(); return false; }   // a silent video, a still
        AVStream *st = mFmt->streams[mStream];
        mDec = avcodec_alloc_context3(codec);
        if (!mDec || avcodec_parameters_to_context(mDec, st->codecpar) < 0 || avcodec_open2(mDec, codec, nullptr) < 0)
        {
            closeAll();
            return false;
        }
        const int64_t inLayout = mDec->channel_layout ? (int64_t)mDec->channel_layout : av_get_default_channel_layout(mDec->channels);
        mSwr = swr_alloc_set_opts(nullptr, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_FLT, mRate, inLayout, mDec->sample_fmt,
                                  mDec->sample_rate, 0, nullptr);
        // a mono file is a centre channel to libswresample, folded in at -3 dB; here mono is the same
        // signal in both ears at its own level (the mixer's centre is unity too)
        // (libswresample hard-codes sqrt(1/2) for pure mono, so the matrix is set outright)
        const double monoToStereo[2] = {1.0, 1.0};
        if (mSwr && mDec->channels == 1) swr_set_matrix(mSwr, monoToStereo, 1);
        if (!mSwr || swr_init(mSwr) < 0) { closeAll(); return false; }
        mFrame = av_frame_alloc();
        mPkt = av_packet_alloc();
        mStart = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
        double dur = 0;
        if (st->duration != AV_NOPTS_VALUE && st->duration > 0) dur = st->duration * av_q2d(st->time_base);
        else if (mFmt->duration > 0) dur = mFmt->duration / (double)AV_TIME_BASE;
        out.duration = dur;
        out.fileRate = mDec->sample_rate;
        out.fileChannels = mDec->channels;
        mBufStart = 0;
        mPositioned = false;   // every position comes from the decoded frames' timestamps — an encoder's
                               // priming makes "the first sample is frame 0" untrue for AAC
        return out.valid();
    }

    bool AudioSourceFFmpeg::seekTo(long long frame)
    {
        AVStream *st = mFmt->streams[mStream];
        // pre-roll: a lapped codec (AAC, MP3) needs the frame before to decode a frame right, so the
        // seek lands a fifth of a second early and decodes through
        const double t = std::max(0.0, (double)frame / mRate - 0.2);
        const int64_t ts = mStart + (int64_t)std::llround(t / av_q2d(st->time_base));
        av_seek_frame(mFmt, mStream, ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(mDec);
        swr_init(mSwr);   // drop the resampler's history: it belongs to the old position
        mBuf.clear();
        mPositioned = false;
        mEof = false;
        mDraining = false;
        return true;
    }

    bool AudioSourceFFmpeg::decodeMore()
    {
        for (;;)
        {
            const int r = avcodec_receive_frame(mDec, mFrame);
            if (r == 0)
            {
                if (!mPositioned)
                {
                    // the first frame after a seek says where the buffer starts
                    const int64_t pts = mFrame->best_effort_timestamp != AV_NOPTS_VALUE ? mFrame->best_effort_timestamp : mStart;
                    mBufStart = std::llround((pts - mStart) * av_q2d(mFmt->streams[mStream]->time_base) * mRate);
                    mPositioned = true;
                }
                const int cap = swr_get_out_samples(mSwr, mFrame->nb_samples);
                const size_t at = mBuf.size();
                mBuf.resize(at + (size_t)std::max(0, cap) * 2);
                uint8_t *outp = reinterpret_cast<uint8_t *>(mBuf.data() + at);
                const int got = swr_convert(mSwr, &outp, cap, (const uint8_t **)mFrame->extended_data, mFrame->nb_samples);
                mBuf.resize(at + (size_t)std::max(0, got) * 2);
                av_frame_unref(mFrame);
                return true;
            }
            if (r != AVERROR(EAGAIN) || mDraining) { mEof = true; return false; }
            // the decoder wants input: the next packet of our stream, or the drain at the end
            for (;;)
            {
                if (av_read_frame(mFmt, mPkt) < 0)
                {
                    avcodec_send_packet(mDec, nullptr);
                    mDraining = true;
                    break;
                }
                const bool ours = mPkt->stream_index == mStream;
                if (ours) avcodec_send_packet(mDec, mPkt);
                av_packet_unref(mPkt);
                if (ours) break;
            }
        }
    }

    bool AudioSourceFFmpeg::read(long long start, int frames, float *stereo)
    {
        if (!mFmt || frames <= 0) return false;
        std::fill(stereo, stereo + 2 * (size_t)frames, 0.0f);
        const long long have = mPositioned ? mBufStart + (long long)(mBuf.size() / 2) : 0;
        // behind what is buffered, or far beyond it: seek; a little ahead: decode on
        if (!mPositioned || start < mBufStart || start > have + mRate / 2) seekTo(start);
        while (!mEof && (!mPositioned || mBufStart + (long long)(mBuf.size() / 2) < start + frames))
            if (!decodeMore() && mEof) break;
        if (!mPositioned) return true;   // nothing decodable: silence
        const long long bufEnd = mBufStart + (long long)(mBuf.size() / 2);
        const long long a = std::max(start, mBufStart), b = std::min(start + frames, bufEnd);
        if (b > a) std::memcpy(stereo + (a - start) * 2, mBuf.data() + (a - mBufStart) * 2, (size_t)(b - a) * 2 * sizeof(float));
        // keep from `start` on: the next sequential read begins where this one ended
        const long long drop = std::min<long long>(std::max<long long>(0, start - mBufStart), (long long)(mBuf.size() / 2));
        if (drop > 0)
        {
            mBuf.erase(mBuf.begin(), mBuf.begin() + drop * 2);
            mBufStart += drop;
        }
        return true;
    }
}
}
