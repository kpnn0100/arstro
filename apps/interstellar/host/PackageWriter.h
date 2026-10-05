/*
 *  interstellar_host — delivery packages (R-DLV-4): a SMPTE DCP and an IMF Application 2E package, each a
 *  folder of MXF track files (MxfWriter) and the XML that names them.
 *
 *  DCP: JPEG 2000 (DCI 2K/4K profile, OpenJPEG through libavcodec) of the render's DCI X'Y'Z' pictures,
 *  centred in the container (the render is fitted inside it); the master on L/R of a 5.1 sound file,
 *  24-bit 48 kHz; a CPL (ST 429-7), a PKL (ST 429-8, SHA-1 hashes), an ASSETMAP and a VOLINDEX (ST 429-9).
 *  IMF App 2E: JPEG 2000 RGB 12-bit (ISO 15444-1 — OpenJPEG 2.4 here writes no IMF profile), the master as
 *  clip-wrapped stereo PCM; a CPL (ST 2067-3:2016) whose essence descriptors repeat the track files' (RegXML,
 *  as Netflix Photon writes them), a PKL and an ASSETMAP.
 *
 *  Neither can be validated here — no cinema server, no DCP or IMF validator in this build — so the
 *  writer's note says so, on every package (the render's row and its event carry it).
 */
#pragma once
#include "FrameSource.h"
#include "MxfWriter.h"
#include <memory>
#include <string>
#include <vector>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;

namespace arstro
{
namespace interstellar_host
{
    class PackageWriter : public interstellar::IFrameWriter
    {
    public:
        ~PackageWriter() override;
        bool begin(const std::string &path, int w, int h, double fps, long long frames, const interstellar::EncodeSpec &spec) override;
        bool write(const interstellar::Raster &frame) override;
        bool writeAudio(const float *stereo, int frames) override;
        bool end() override;
        std::string note() const override { return mNote; }
        const std::string &error() const { return mError; }

    private:
        bool mImf = false;
        interstellar::EncodeSpec mSpec;
        std::string mDir, mTitle, mNote, mError;
        int mW = 0, mH = 0, mCW = 0, mCH = 0, mRateNum = 24, mRateDen = 1;
        long long mFrames = 0;
        AVCodecContext *mEnc = nullptr;
        AVFrame *mFrame = nullptr;
        AVPacket *mPkt = nullptr;
        MxfWriter mPic, mSnd;
        bool mPicOpen = false, mSound = false;
        Uuid mPicAsset{}, mSndAsset{};
        std::string mPicFile, mSndFile;
        J2kInfo mPicJ2k;           // the first codestream's header: the CPL repeats it
        std::vector<float> mPcm;   // the master not yet a whole DCP sound frame
        long long mSoundFrames = 0;
        bool drain(bool flush);
        bool soundFrame(const float *stereo, int frames);
        bool writeXml();
    };
}
}
