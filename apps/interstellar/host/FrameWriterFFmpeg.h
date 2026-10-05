/*
 *  interstellar/host — FrameWriterFFmpeg: `IFrameWriter` over libavformat/libavcodec (R-RENDER-2).
 *
 *  Host layer, for the same reason the source is: the core carries no codec.
 *
 *  **What a delivery asks for (R-RENDER-6).** H.264 and H.265 (8- or 10-bit) in MP4/MKV/MOV are what
 *  a person watches and uploads — constant quality (CRF) and an encoder speed; ProRes (Proxy … 4444,
 *  `prores_ks`) and DNxHR (LB … 444) in MOV are what an edit or a finishing house round-trips
 *  through, their quality being their profile. The PNG sequence is not a codec: the service writes
 *  it, and it stays the only output a golden test can compare byte for byte (R-RENDER-3).
 *
 *  The codec comes from the spec, and the container must agree with it: a `.mp4` holding ProRes is
 *  refused, naming the containers that would do, rather than written as a surprise.
 *
 *  Hardware (R-PLAY-3): with `spec.hardware`, H.264/H.265 encode through VA-API (`h264_vaapi`,
 *  `hevc_vaapi`) — frames are converted to NV12/P010 in software and uploaded to a surface pool.
 *  Anything missing (no device, no driver, the encoder refusing) falls back to the software encoder
 *  and says so in `note()`, never failing a render because a device is absent. The device is
 *  INTERSTELLAR_VAAPI_DEVICE, else FFmpeg's default (the first render node).
 *
 *  Colour (D-9): the RGB→YUV conversion is BT.709 at video (limited) range and the stream is TAGGED
 *  BT.709, so a player does not guess — untagged BT.601 conversion shifted every hue slightly in an
 *  app whose subject is colour.
 */
#pragma once
#include "FrameSource.h"
#include <string>
#include <vector>

struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct AVFrame;
struct AVPacket;
struct SwsContext;
struct AVBufferRef;
struct AVCodec;

namespace arstro
{
namespace interstellar_host
{
    class FrameWriterFFmpeg : public interstellar::IFrameWriter
    {
    public:
        FrameWriterFFmpeg() = default;
        ~FrameWriterFFmpeg() override;
        FrameWriterFFmpeg(const FrameWriterFFmpeg &) = delete;
        FrameWriterFFmpeg &operator=(const FrameWriterFFmpeg &) = delete;

        bool begin(const std::string &path, int w, int h, double fps, long long frames, const interstellar::EncodeSpec &spec) override;
        bool write(const interstellar::Raster &frame) override;
        bool writeAudio(const float *stereo, int frames) override;
        bool end() override;

        /** True when this class can write `path`, judged by its extension. The CLI asks first so
         *  it can fall back to its PPM sequence rather than failing a render. */
        static bool handles(const std::string &path);
        /** The last error, for a rejection message that names what went wrong. */
        const std::string &error() const { return mError; }
        std::string note() const override { return mNote; }
        /** True when the last begin() opened a hardware encoder. */
        bool hardware() const { return mHwDevice != nullptr; }

    private:
        bool drain(bool flush);
        bool drainAudio(bool flush);
        bool sendAudioFrame(int frames);   // `frames` from the front of mAudioFifo
        void closeAll();

        AVFormatContext *mFmt = nullptr;
        AVCodecContext *mEnc = nullptr;
        AVStream *mStream = nullptr;
        AVFrame *mFrame = nullptr;
        AVPacket *mPkt = nullptr;
        SwsContext *mSws = nullptr;
        int mSwsMatrix = 1;   // SWS_CS_ITU709; SWS_CS_BT2020 for an HDR output (R-COLOR-4)
        long long mNext = 0;
        bool mOpen = false;
        std::string mError, mNote, mPath;
        double mFps = 24.0;
        // R-PLAY-3: VA-API — the device, its surface pool, and the software frame converted into first
        AVBufferRef *mHwDevice = nullptr, *mHwFrames = nullptr;
        AVFrame *mSwFrame = nullptr;
        // R-AUD-9: the master — its encoder, its stream, the samples not yet a whole encoder frame
        AVCodecContext *mAEnc = nullptr;
        AVStream *mAStream = nullptr;
        AVFrame *mAFrame = nullptr;
        std::vector<float> mAudioFifo;
        long long mAudioNext = 0;
        int mAudioFrameSize = 1024;
        bool openEncoder(const AVCodec *codec, const interstellar::EncodeSpec &spec, int w, int h, const std::string &ext);
    };
}
}
