/*
 *  interstellar/host — AudioSourceFFmpeg: `IAudioSource` over libavformat/libavcodec/libswresample
 *  (R-AUD-5 amended). The file's best audio stream, decoded, folded to stereo and resampled to the
 *  mix rate, read in frames of that rate.
 *
 *  It keeps what it decoded from the last read onward, so the sequential reads of playback and of a
 *  render decode each packet once; a read behind that, or more than half a second ahead, seeks to
 *  a fifth of a second before it (a lapped codec needs the frame before) and decodes forward. Every
 *  position comes from the decoded frames' timestamps, the first read's too. Frame 0 is the
 *  stream's first timestamp, as for pictures (D-3).
 */
#pragma once
#include "../core/AudioSource.h"
#include <cstdint>
#include <string>
#include <vector>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwrContext;

namespace arstro
{
namespace interstellar_host
{
    class AudioSourceFFmpeg : public interstellar::IAudioSource
    {
    public:
        AudioSourceFFmpeg() = default;
        ~AudioSourceFFmpeg() override;
        AudioSourceFFmpeg(const AudioSourceFFmpeg &) = delete;
        AudioSourceFFmpeg &operator=(const AudioSourceFFmpeg &) = delete;

        bool open(const std::string &path, int rate, Info &out) override;
        bool read(long long start, int frames, float *stereo) override;

    private:
        bool seekTo(long long frame);
        bool decodeMore();          // false at the end of the stream
        void closeAll();

        AVFormatContext *mFmt = nullptr;
        AVCodecContext *mDec = nullptr;
        SwrContext *mSwr = nullptr;
        AVFrame *mFrame = nullptr;
        AVPacket *mPkt = nullptr;
        int mStream = -1, mRate = 48000;
        int64_t mStart = 0;                 // the stream's first timestamp: frame 0
        std::vector<float> mBuf;            // decoded stereo frames from mBufStart on
        long long mBufStart = 0;
        bool mPositioned = false;           // mBufStart is known (false right after a seek)
        bool mEof = false;
        bool mDraining = false;             // the decoder was sent the end; what it holds is the rest
    };
}
}
