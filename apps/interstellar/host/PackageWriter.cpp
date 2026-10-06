#include "PackageWriter.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/base64.h>
#include <libavutil/opt.h>
#include <libavutil/sha.h>
}
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace arstro
{
namespace interstellar_host
{
    namespace
    {
        // 24 → 24/1; 23.976 → 24000/1001 (an NTSC rate is a fraction, not a decimal)
        void rateOf(double fps, int &n, int &d)
        {
            const double ntsc = fps * 1001.0 / 1000.0;
            if (std::fabs(fps - std::round(fps)) > 1e-3 && std::fabs(ntsc - std::round(ntsc)) < 1e-3) { n = (int)std::lround(ntsc) * 1000; d = 1001; }
            else { n = (int)std::lround(fps); d = 1; }
        }

        std::string esc(const std::string &s)
        {
            std::string o;
            for (const char c : s)
                o += c == '&' ? "&amp;" : c == '<' ? "&lt;" : c == '>' ? "&gt;" : c == '"' ? "&quot;" : std::string(1, c);
            return o;
        }

        std::string isoNow()
        {
            const std::time_t now = std::time(nullptr);
            std::tm g{};
#ifdef _WIN32
            gmtime_s(&g, &now);  // MSVC/MinGW-ucrt param order: (dest, source)
#else
            gmtime_r(&now, &g);
#endif
            char b[40];
            std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%S+00:00", &g);
            return b;
        }

        std::string urn(const Uuid &u) { return "urn:uuid:" + uuidText(u); }
        std::string ulUrn(const UL &u)
        {
            static const char *hex = "0123456789abcdef";
            std::string s = "urn:smpte:ul:";
            for (int i = 0; i < 16; ++i)
            {
                if (i && i % 4 == 0) s += '.';
                s += hex[u[i] >> 4];
                s += hex[u[i] & 15];
            }
            return s;
        }
        std::string hexOf(const std::vector<uint8_t> &v)
        {
            static const char *hex = "0123456789abcdef";
            std::string s;
            for (const uint8_t b : v) { s += hex[b >> 4]; s += hex[b & 15]; }
            return s;
        }

        // SHA-1, base64 — what a PKL and a CPL say of each file
        bool sha1(const std::string &path, std::string &b64, long long &size)
        {
            std::ifstream f(path, std::ios::binary);
            if (!f) return false;
            struct AVSHA *ctx = av_sha_alloc();
            if (!ctx) return false;
            av_sha_init(ctx, 160);
            std::vector<char> buf(1 << 20);
            size = 0;
            while (f)
            {
                f.read(buf.data(), (std::streamsize)buf.size());
                const std::streamsize got = f.gcount();
                if (got <= 0) break;
                av_sha_update(ctx, reinterpret_cast<const uint8_t *>(buf.data()), (unsigned)got);
                size += got;
            }
            uint8_t digest[20];
            av_sha_final(ctx, digest);
            av_free(ctx);
            char out[64];
            av_base64_encode(out, sizeof out, digest, 20);
            b64 = out;
            return true;
        }

        bool writeFile(const std::string &path, const std::string &text)
        {
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            return f && (f << text) && f.flush();
        }

        // the colour an IMF picture says it is: its transfer, primaries and coding equations
        void imfColour(const std::string &out, MxfTrackSpec &s)
        {
            const bool wide = out == "pq" || out == "hlg";
            s.transfer = ulOf(out == "pq" ? "060e2b340401010d04010101010a0000" : out == "hlg" ? "060e2b340401010d04010101010b0000"
                              : out == "srgb" ? "060e2b340401010d04010101010d0000" : out == "p3d65" ? "060e2b340401010d04010101010c0000"
                                                : "060e2b34040101010401010101020000");
            s.primaries = ulOf(wide ? "060e2b340401010d0401010103040000" : out == "p3d65" ? "060e2b340401010d0401010103060000" : "060e2b34040101060401010103030000");
            s.equations = ulOf(wide ? "060e2b340401010d0401010102060000" : "060e2b34040101010401010102020000");
        }
    }

    PackageWriter::~PackageWriter()
    {
        if (mEnc) avcodec_free_context(&mEnc);
        if (mFrame) av_frame_free(&mFrame);
        if (mPkt) av_packet_free(&mPkt);
    }

    bool PackageWriter::begin(const std::string &path, int w, int h, double fps, long long, const interstellar::EncodeSpec &spec)
    {
        mImf = spec.codec == "imf";
        if (!mImf && spec.codec != "dcp") { mError = "a package is a DCP or an IMF, not " + spec.codec; return false; }
        mSpec = spec;
        mDir = path;
        mW = w;
        mH = h;
        mCW = mImf || spec.containerW <= 0 ? w : spec.containerW;
        mCH = mImf || spec.containerH <= 0 ? h : spec.containerH;
        rateOf(fps, mRateNum, mRateDen);
        mFrames = mSoundFrames = 0;
        mPicOpen = false;
        mPcm.clear();
        std::string base = path;
        while (base.size() > 1 && base.back() == '/') base.pop_back();
        mTitle = spec.title.empty() ? fs::path(base).filename().string() : spec.title;
        std::error_code ec;
        fs::create_directories(path, ec);
        if (ec) { mError = "cannot create " + path; return false; }

        const AVCodec *codec = avcodec_find_encoder_by_name("libopenjpeg");
        if (!codec) { mError = "this FFmpeg build has no libopenjpeg (JPEG 2000) encoder"; return false; }
        mEnc = avcodec_alloc_context3(codec);
        if (!mEnc) { mError = "cannot allocate the JPEG 2000 encoder"; return false; }
        mEnc->width = mCW;
        mEnc->height = mCH;
        mEnc->time_base = AVRational{mRateDen, mRateNum};
        mEnc->pix_fmt = mImf ? AV_PIX_FMT_GBRP12LE : AV_PIX_FMT_XYZ12LE;
        mEnc->thread_count = 0;
        av_opt_set(mEnc->priv_data, "format", "j2k", 0);   // a raw codestream: what MXF wraps
        av_opt_set(mEnc->priv_data, "prog_order", "cprl", 0);
        if (!mImf)
        {
            // the DCI profile: 2K or 4K, its bit-rate ceiling from the rate (a 2K above 24 fps takes the 48 fps one)
            const bool k4 = mCW > 2048;
            av_opt_set(mEnc->priv_data, "profile", k4 ? "cinema4k" : "cinema2k", 0);
            av_opt_set(mEnc->priv_data, "cinema_mode", k4 ? "4k_24" : (mRateNum == 24 && mRateDen == 1 ? "2k_24" : "2k_48"), 0);
        }
        else
        {
            // a 9/7 mezzanine at 10:1, as many decompositions as the frame allows (six at most)
            av_opt_set_int(mEnc->priv_data, "irreversible", 1, 0);
            const int levels = std::clamp((int)std::floor(std::log2((double)std::min(mCW, mCH))) - 2, 1, 6);
            av_opt_set_int(mEnc->priv_data, "numresolution", levels, 0);
            mEnc->compression_level = 5;
        }
        if (avcodec_open2(mEnc, codec, nullptr) < 0) { mError = "the JPEG 2000 encoder refused " + std::to_string(mCW) + "x" + std::to_string(mCH); return false; }
        mFrame = av_frame_alloc();
        mPkt = av_packet_alloc();
        if (!mFrame || !mPkt) { mError = "cannot allocate a frame"; return false; }
        mFrame->format = mEnc->pix_fmt;
        mFrame->width = mCW;
        mFrame->height = mCH;
        if (av_frame_get_buffer(mFrame, 32) < 0) { mError = "cannot allocate a frame"; return false; }

        mPicAsset = randomUuid();
        mSound = spec.audioRate > 0;
        if (mSound)
        {
            if (spec.audioRate != 48000) { mError = "a package's sound is 48 kHz, not " + std::to_string(spec.audioRate); return false; }
            MxfTrackSpec s;
            s.imf = mImf;
            s.sound = true;
            s.rateNum = mRateNum;
            s.rateDen = mRateDen;
            s.asset = mSndAsset = randomUuid();
            s.channels = mImf ? 2 : 6;
            s.sampleRate = 48000;
            s.title = mTitle;
            mSndFile = (mImf ? "AUD_" : "pcm_") + uuidText(mSndAsset) + ".mxf";
            std::string err;
            if (!mSnd.begin((fs::path(path) / mSndFile).string(), s, err)) { mError = err; return false; }
        }
        mNote = mImf ? "an IMF App 2E package, NOT validated here (no IMF validator in this build) \xE2\x80\x94 check it with Photon before delivery; "
                       "its JPEG 2000 is ISO 15444-1, not an IMF profile (OpenJPEG 2.4 here writes none)"
                     : "a SMPTE DCP, NOT validated here (no cinema server or DCP validator in this build) \xE2\x80\x94 check it "
                       "(DCP-o-matic's verifier, ClairMeta) before it goes to a cinema";
        return true;
    }

    bool PackageWriter::drain(bool flush)
    {
        for (;;)
        {
            const int r = avcodec_receive_packet(mEnc, mPkt);
            if (r == AVERROR(EAGAIN) || (flush && r == AVERROR_EOF)) return true;
            if (r < 0) { mError = "the JPEG 2000 encoder failed"; return false; }
            std::string err;
            if (!mPicOpen)
            {
                // the first codestream says what every frame will be: the sub-descriptor is its header
                MxfTrackSpec s;
                s.imf = mImf;
                s.rateNum = mRateNum;
                s.rateDen = mRateDen;
                s.asset = mPicAsset;
                s.width = mCW;
                s.height = mCH;
                if (!parseJ2k(mPkt->data, (size_t)mPkt->size, s.j2k)) { av_packet_unref(mPkt); mError = "the encoder wrote no JPEG 2000 codestream"; return false; }
                if (mImf)
                {
                    s.coding = ulOf("060e2b34040101070401020203010100");   // ISO/IEC 15444-1: what the codestream is
                    imfColour(mSpec.output, s);
                }
                else s.coding = ulOf(mCW > 2048 ? "060e2b34040101090401020203010104" : "060e2b34040101090401020203010103");
                mPicJ2k = s.j2k;
                mPicFile = (mImf ? "IMG_" : "j2c_") + uuidText(mPicAsset) + ".mxf";
                if (!mPic.begin((fs::path(mDir) / mPicFile).string(), s, err)) { av_packet_unref(mPkt); mError = err; return false; }
                mPicOpen = true;
            }
            const bool ok = mPic.write(mPkt->data, (size_t)mPkt->size, err);
            av_packet_unref(mPkt);
            if (!ok) { mError = err; return false; }
        }
    }

    bool PackageWriter::write(const interstellar::Raster &frame)
    {
        if (!mEnc || frame.width != mW || frame.height != mH) { mError = "a frame of the wrong size"; return false; }
        if (av_frame_make_writable(mFrame) < 0) { mError = "cannot write a frame"; return false; }
        const bool deep = frame.deep();
        auto at = [&](int x, int y, int c) -> uint32_t {
            const size_t i = ((size_t)y * frame.width + x) * 4 + c;
            return deep ? frame.rgba16[i] : (uint32_t)frame.rgba[i] * 257u;
        };
        auto to12 = [](uint32_t v16) { return (uint16_t)((v16 * 4095u + 32767u) / 65535u); };
        if (!mImf)
        {
            // DCI X'Y'Z' 12-bit (the render's dcdm output), the picture centred in the container, black around it
            const int x0 = (mCW - mW) / 2, y0 = (mCH - mH) / 2;
            for (int y = 0; y < mCH; ++y)
            {
                uint16_t *row = reinterpret_cast<uint16_t *>(mFrame->data[0] + (size_t)y * mFrame->linesize[0]);
                std::memset(row, 0, (size_t)mCW * 6);
                const int sy = y - y0;
                if (sy < 0 || sy >= mH) continue;
                for (int x = 0; x < mW; ++x)
                    for (int c = 0; c < 3; ++c) row[(size_t)(x + x0) * 3 + c] = (uint16_t)(to12(at(x, sy, c)) << 4);   // the low 4 bits are zero
            }
        }
        else
        {
            // RGB 12-bit, planar G, B, R
            static const int plane[3] = {2, 0, 1};
            for (int y = 0; y < mH; ++y)
                for (int c = 0; c < 3; ++c)
                {
                    uint16_t *row = reinterpret_cast<uint16_t *>(mFrame->data[plane[c]] + (size_t)y * mFrame->linesize[plane[c]]);
                    for (int x = 0; x < mW; ++x) row[x] = to12(at(x, y, c));
                }
        }
        mFrame->pts = mFrames++;
        if (avcodec_send_frame(mEnc, mFrame) < 0) { mError = "the JPEG 2000 encoder refused a frame"; return false; }
        return drain(false);
    }

    bool PackageWriter::soundFrame(const float *stereo, int frames)
    {
        // a DCP sound frame: L and R carry the master, C, LFE, Ls and Rs are silent
        std::vector<uint8_t> b((size_t)frames * 6 * 3, 0);
        for (int i = 0; i < frames; ++i)
            for (int c = 0; c < 2; ++c)
            {
                const int32_t v = (int32_t)std::lround(std::clamp(stereo ? stereo[i * 2 + c] : 0.0f, -1.0f, 1.0f) * 8388607.0f);
                uint8_t *p = &b[((size_t)i * 6 + c) * 3];
                p[0] = (uint8_t)v;
                p[1] = (uint8_t)(v >> 8);
                p[2] = (uint8_t)(v >> 16);
            }
        std::string err;
        if (!mSnd.write(b.data(), b.size(), err)) { mError = err; return false; }
        ++mSoundFrames;
        return true;
    }

    bool PackageWriter::writeAudio(const float *stereo, int frames)
    {
        if (!mSound || frames <= 0) return true;
        if (mImf)
        {
            std::vector<uint8_t> b((size_t)frames * 2 * 3);
            for (int i = 0; i < frames * 2; ++i)
            {
                const int32_t v = (int32_t)std::lround(std::clamp(stereo[i], -1.0f, 1.0f) * 8388607.0f);
                b[(size_t)i * 3] = (uint8_t)v;
                b[(size_t)i * 3 + 1] = (uint8_t)(v >> 8);
                b[(size_t)i * 3 + 2] = (uint8_t)(v >> 16);
            }
            std::string err;
            if (!mSnd.write(b.data(), b.size(), err)) { mError = err; return false; }
            return true;
        }
        mPcm.insert(mPcm.end(), stereo, stereo + (size_t)frames * 2);
        const int spf = (int)(48000LL * mRateDen / mRateNum);
        size_t used = 0;
        while ((mPcm.size() - used) / 2 >= (size_t)spf)
        {
            if (!soundFrame(mPcm.data() + used, spf)) return false;
            used += (size_t)spf * 2;
        }
        mPcm.erase(mPcm.begin(), mPcm.begin() + (long)used);
        return true;
    }

    bool PackageWriter::end()
    {
        if (!mEnc) return false;
        avcodec_send_frame(mEnc, nullptr);
        if (!drain(true)) return false;
        if (!mPicOpen) { mError = "no pictures to package"; return false; }
        std::string err;
        if (mSound && !mImf)
        {
            // as many sound frames as pictures: the rest of a short master is silence
            const int spf = (int)(48000LL * mRateDen / mRateNum);
            while (mSoundFrames < mPic.duration())
                if (!soundFrame(nullptr, spf)) return false;
        }
        if (!mPic.finish(err) || (mSound && !mSnd.finish(err))) { mError = err; return false; }
        return writeXml();
    }

    bool PackageWriter::writeXml()
    {
        struct Asset { Uuid id; std::string file, hash, type; long long size = 0; };
        std::vector<Asset> assets;
        auto add = [&](const Uuid &id, const std::string &file, const char *type) {
            Asset a{id, file, std::string(), type};
            if (!sha1((fs::path(mDir) / file).string(), a.hash, a.size)) { mError = "cannot read back " + file; return false; }
            assets.push_back(a);
            return true;
        };
        if (!add(mPicAsset, mPicFile, "application/mxf") || (mSound && !add(mSndAsset, mSndFile, "application/mxf"))) return false;
        const std::string when = isoNow(), title = esc(mTitle);
        const long long frames = mPic.duration();
        const std::string rate = std::to_string(mRateNum) + " " + std::to_string(mRateDen);
        const Uuid cplId = randomUuid(), pklId = randomUuid(), amId = randomUuid();
        std::ostringstream c;
        c << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        if (!mImf)
        {
            c << "<CompositionPlaylist xmlns=\"http://www.smpte-ra.org/schemas/429-7/2006/CPL\">\n"
              << "  <Id>" << urn(cplId) << "</Id>\n  <AnnotationText>" << title << "</AnnotationText>\n"
              << "  <IssueDate>" << when << "</IssueDate>\n  <Issuer>Interstellar</Issuer>\n  <Creator>Interstellar (Arstro)</Creator>\n"
              << "  <ContentTitleText>" << title << "</ContentTitleText>\n  <ContentKind>feature</ContentKind>\n"
              << "  <ContentVersion>\n    <Id>" << urn(randomUuid()) << "</Id>\n    <LabelText>" << title << "</LabelText>\n  </ContentVersion>\n"
              << "  <RatingList/>\n  <ReelList>\n    <Reel>\n      <Id>" << urn(randomUuid()) << "</Id>\n      <AssetList>\n"
              << "        <MainPicture>\n          <Id>" << urn(mPicAsset) << "</Id>\n          <EditRate>" << rate << "</EditRate>\n"
              << "          <IntrinsicDuration>" << frames << "</IntrinsicDuration>\n          <EntryPoint>0</EntryPoint>\n"
              << "          <Duration>" << frames << "</Duration>\n          <Hash>" << assets[0].hash << "</Hash>\n"
              << "          <FrameRate>" << rate << "</FrameRate>\n          <ScreenAspectRatio>" << mCW << " " << mCH << "</ScreenAspectRatio>\n"
              << "        </MainPicture>\n";
            if (mSound)
                c << "        <MainSound>\n          <Id>" << urn(mSndAsset) << "</Id>\n          <EditRate>" << rate << "</EditRate>\n"
                  << "          <IntrinsicDuration>" << mSnd.duration() << "</IntrinsicDuration>\n          <EntryPoint>0</EntryPoint>\n"
                  << "          <Duration>" << mSnd.duration() << "</Duration>\n          <Hash>" << assets[1].hash << "</Hash>\n        </MainSound>\n";
            c << "      </AssetList>\n    </Reel>\n  </ReelList>\n</CompositionPlaylist>\n";
        }
        else
        {
            // ST 2067-3:2016 for Application 2E: the essence descriptors repeat the track files' (RegXML)
            const Uuid picDesc = randomUuid(), sndDesc = randomUuid();
            MxfTrackSpec ps;
            ps.imf = true;
            imfColour(mSpec.output, ps);
            const char *r0 = "xmlns:r0=\"http://www.smpte-ra.org/reg/395/2014/13/1/aaf\" xmlns:r1=\"http://www.smpte-ra.org/reg/335/2012\" "
                             "xmlns:r2=\"http://www.smpte-ra.org/reg/2003/2012\"";
            const long long secs = frames * mRateDen / mRateNum;
            char trt[16];
            std::snprintf(trt, sizeof trt, "%02lld:%02lld:%02lld", secs / 3600, secs / 60 % 60, secs % 60);
            c << "<CompositionPlaylist xmlns=\"http://www.smpte-ra.org/schemas/2067-3/2016\" xmlns:cc=\"http://www.smpte-ra.org/ns/2067-2/2020\" "
                 "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\n"
              << "  <Id>" << urn(cplId) << "</Id>\n  <Annotation>" << title << "</Annotation>\n  <IssueDate>" << when << "</IssueDate>\n"
              << "  <Issuer>Interstellar</Issuer>\n  <Creator>Interstellar (Arstro)</Creator>\n  <ContentTitle>" << title << "</ContentTitle>\n"
              << "  <ContentKind>feature</ContentKind>\n  <ContentVersionList>\n    <ContentVersion>\n      <Id>" << urn(randomUuid())
              << "</Id>\n      <LabelText>" << title << "</LabelText>\n    </ContentVersion>\n  </ContentVersionList>\n  <EssenceDescriptorList>\n";
            // the picture's, as the MXF holds it
            {
                const J2kInfo &k = mPicJ2k;
                c << "    <EssenceDescriptor>\n      <Id>" << urn(picDesc) << "</Id>\n      <r0:RGBADescriptor " << r0 << ">\n"
                  << "        <r1:InstanceID>" << urn(mPic.descriptorId()) << "</r1:InstanceID>\n        <r1:SubDescriptors>\n"
                  << "          <r0:JPEG2000SubDescriptor>\n            <r1:InstanceID>" << urn(mPic.subDescriptorId(0)) << "</r1:InstanceID>\n"
                  << "            <r1:Rsiz>" << k.rsiz << "</r1:Rsiz>\n            <r1:Xsiz>" << k.xsiz << "</r1:Xsiz>\n            <r1:Ysiz>" << k.ysiz << "</r1:Ysiz>\n"
                  << "            <r1:XOsiz>" << k.xosiz << "</r1:XOsiz>\n            <r1:YOsiz>" << k.yosiz << "</r1:YOsiz>\n"
                  << "            <r1:XTsiz>" << k.xtsiz << "</r1:XTsiz>\n            <r1:YTsiz>" << k.ytsiz << "</r1:YTsiz>\n"
                  << "            <r1:XTOsiz>" << k.xtosiz << "</r1:XTOsiz>\n            <r1:YTOsiz>" << k.ytosiz << "</r1:YTOsiz>\n"
                  << "            <r1:Csiz>" << k.csiz << "</r1:Csiz>\n            <r1:PictureComponentSizing>\n";
                for (size_t i = 0; i + 2 < k.sizing.size(); i += 3)
                    c << "              <r2:J2KComponentSizing>\n                <r2:Ssiz>" << (int)k.sizing[i] << "</r2:Ssiz>\n                <r2:XRSiz>"
                      << (int)k.sizing[i + 1] << "</r2:XRSiz>\n                <r2:YRSiz>" << (int)k.sizing[i + 2] << "</r2:YRSiz>\n              </r2:J2KComponentSizing>\n";
                c << "            </r1:PictureComponentSizing>\n            <r1:CodingStyleDefault>" << hexOf(k.cod) << "</r1:CodingStyleDefault>\n"
                  << "            <r1:QuantizationDefault>" << hexOf(k.qcd) << "</r1:QuantizationDefault>\n            <r1:J2CLayout>\n";
                auto rgba = [&](const char *indent) {
                    for (const char *code : {"CompRed", "CompGreen", "CompBlue", "CompNull", "CompNull", "CompNull", "CompNull", "CompNull"})
                        c << indent << "<r2:RGBAComponent>\n" << indent << "  <r2:Code>" << code << "</r2:Code>\n" << indent << "  <r2:ComponentSize>"
                          << (std::strcmp(code, "CompNull") ? 12 : 0) << "</r2:ComponentSize>\n" << indent << "</r2:RGBAComponent>\n";
                };
                rgba("              ");
                c << "            </r1:J2CLayout>\n          </r0:JPEG2000SubDescriptor>\n"
                  << "        </r1:SubDescriptors>\n        <r1:LinkedTrackID>1</r1:LinkedTrackID>\n"
                  << "        <r1:SampleRate>" << mRateNum << "/" << mRateDen << "</r1:SampleRate>\n        <r1:EssenceLength>" << frames << "</r1:EssenceLength>\n"
                  << "        <r1:ContainerFormat>" << ulUrn(ulOf("060e2b340401010d0d010301020c0600")) << "</r1:ContainerFormat>\n"
                  << "        <r1:FrameLayout>FullFrame</r1:FrameLayout>\n        <r1:StoredWidth>" << mCW << "</r1:StoredWidth>\n"
                  << "        <r1:StoredHeight>" << mCH << "</r1:StoredHeight>\n        <r1:ImageAspectRatio>" << mCW << "/" << mCH << "</r1:ImageAspectRatio>\n"
                  << "        <r1:TransferCharacteristic>" << ulUrn(ps.transfer) << "</r1:TransferCharacteristic>\n"
                  << "        <r1:PictureCompression>" << ulUrn(ulOf("060e2b34040101070401020203010100")) << "</r1:PictureCompression>\n"
                  << "        <r1:CodingEquations>" << ulUrn(ps.equations) << "</r1:CodingEquations>\n"
                  << "        <r1:ColorPrimaries>" << ulUrn(ps.primaries) << "</r1:ColorPrimaries>\n"
                  << "        <r1:VideoLineMap>\n          <r2:Int32>0</r2:Int32>\n          <r2:Int32>0</r2:Int32>\n        </r1:VideoLineMap>\n"
                  << "        <r1:ComponentMaxRef>4095</r1:ComponentMaxRef>\n        <r1:ComponentMinRef>0</r1:ComponentMinRef>\n"
                  << "        <r1:ScanningDirection>ScanningDirection_LeftToRightTopToBottom</r1:ScanningDirection>\n        <r1:PixelLayout>\n";
                rgba("          ");
                c << "        </r1:PixelLayout>\n      </r0:RGBADescriptor>\n    </EssenceDescriptor>\n";
            }
            if (mSound)
            {
                static const char *labels[3][3] = {{"060e2b340401010d0302022001000000", "sgST", "Standard Stereo"},
                                                   {"060e2b340401010d0302010100000000", "chL", "Left"},
                                                   {"060e2b340401010d0302010200000000", "chR", "Right"}};
                c << "    <EssenceDescriptor>\n      <Id>" << urn(sndDesc) << "</Id>\n      <r0:WAVEPCMDescriptor " << r0 << ">\n"
                  << "        <r1:InstanceID>" << urn(mSnd.descriptorId()) << "</r1:InstanceID>\n        <r1:SubDescriptors>\n";
                for (int k = 0; k < 3; ++k)
                {
                    const char *cls = k == 0 ? "SoundfieldGroupLabelSubDescriptor" : "AudioChannelLabelSubDescriptor";
                    c << "          <r0:" << cls << ">\n            <r1:InstanceID>" << urn(mSnd.subDescriptorId(k)) << "</r1:InstanceID>\n"
                      << "            <r1:MCALabelDictionaryID>" << ulUrn(ulOf(labels[k][0])) << "</r1:MCALabelDictionaryID>\n"
                      << "            <r1:MCALinkID>" << urn(mSnd.mcaLinkId(k)) << "</r1:MCALinkID>\n"
                      << "            <r1:MCATagSymbol>" << labels[k][1] << "</r1:MCATagSymbol>\n            <r1:MCATagName>" << labels[k][2] << "</r1:MCATagName>\n";
                    if (k > 0) c << "            <r1:MCAChannelID>" << k << "</r1:MCAChannelID>\n";
                    c << "            <r1:RFC5646SpokenLanguage>und</r1:RFC5646SpokenLanguage>\n";
                    if (k == 0)
                        c << "            <r1:MCATitle>" << title << "</r1:MCATitle>\n            <r1:MCATitleVersion>1</r1:MCATitleVersion>\n"
                          << "            <r1:MCAAudioContentKind>PRM</r1:MCAAudioContentKind>\n            <r1:MCAAudioElementKind>FCMP</r1:MCAAudioElementKind>\n";
                    if (k > 0) c << "            <r1:SoundfieldGroupLinkID>" << urn(mSnd.mcaLinkId(0)) << "</r1:SoundfieldGroupLinkID>\n";
                    c << "          </r0:" << cls << ">\n";
                }
                c << "        </r1:SubDescriptors>\n        <r1:LinkedTrackID>1</r1:LinkedTrackID>\n        <r1:SampleRate>48000/1</r1:SampleRate>\n"
                  << "        <r1:EssenceLength>" << mSnd.duration() << "</r1:EssenceLength>\n"
                  << "        <r1:ContainerFormat>" << ulUrn(ulOf("060e2b34040101010d01030102060200")) << "</r1:ContainerFormat>\n"
                  << "        <r1:AudioSampleRate>48000/1</r1:AudioSampleRate>\n        <r1:Locked>False</r1:Locked>\n        <r1:ChannelCount>2</r1:ChannelCount>\n"
                  << "        <r1:QuantizationBits>24</r1:QuantizationBits>\n        <r1:BlockAlign>6</r1:BlockAlign>\n"
                  << "        <r1:AverageBytesPerSecond>288000</r1:AverageBytesPerSecond>\n"
                  << "        <r1:ChannelAssignment>" << ulUrn(ulOf("060e2b340401010d0402021004010000")) << "</r1:ChannelAssignment>\n"
                  << "      </r0:WAVEPCMDescriptor>\n    </EssenceDescriptor>\n";
            }
            c << "  </EssenceDescriptorList>\n  <EditRate>" << rate << "</EditRate>\n  <TotalRunningTime>" << trt << "</TotalRunningTime>\n"
              << "  <ExtensionProperties>\n    <cc:ApplicationIdentification>http://www.smpte-ra.org/ns/2067-21/2020</cc:ApplicationIdentification>\n"
              << "  </ExtensionProperties>\n  <SegmentList>\n    <Segment>\n      <Id>" << urn(randomUuid()) << "</Id>\n      <SequenceList>\n";
            auto sequence = [&](const char *el, const Uuid &asset, const Uuid &desc, const std::string &erate, long long dur, const std::string &hash) {
                c << "        <cc:" << el << ">\n          <Id>" << urn(randomUuid()) << "</Id>\n          <TrackId>" << urn(randomUuid()) << "</TrackId>\n"
                  << "          <ResourceList>\n            <Resource xsi:type=\"TrackFileResourceType\">\n              <Id>" << urn(randomUuid()) << "</Id>\n"
                  << "              <EditRate>" << erate << "</EditRate>\n              <IntrinsicDuration>" << dur << "</IntrinsicDuration>\n"
                  << "              <EntryPoint>0</EntryPoint>\n              <SourceDuration>" << dur << "</SourceDuration>\n"
                  << "              <RepeatCount>1</RepeatCount>\n              <SourceEncoding>" << urn(desc) << "</SourceEncoding>\n"
                  << "              <TrackFileId>" << urn(asset) << "</TrackFileId>\n              <Hash>" << hash << "</Hash>\n"
                  << "            </Resource>\n          </ResourceList>\n        </cc:" << el << ">\n";
            };
            sequence("MainImageSequence", mPicAsset, picDesc, rate, frames, assets[0].hash);
            if (mSound) sequence("MainAudioSequence", mSndAsset, sndDesc, "48000 1", mSnd.duration(), assets[1].hash);
            c << "      </SequenceList>\n    </Segment>\n  </SegmentList>\n</CompositionPlaylist>\n";
        }
        const std::string cplFile = (mImf ? "CPL_" : "cpl_") + uuidText(cplId) + ".xml";
        if (!writeFile((fs::path(mDir) / cplFile).string(), c.str()) || !add(cplId, cplFile, "text/xml")) { mError = "cannot write " + cplFile; return false; }

        std::ostringstream p;
        p << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<PackingList xmlns=\"http://www.smpte-ra.org/schemas/429-8/2007/PKL\">\n"
          << "  <Id>" << urn(pklId) << "</Id>\n  <AnnotationText>" << title << "</AnnotationText>\n  <IssueDate>" << when << "</IssueDate>\n"
          << "  <Issuer>Interstellar</Issuer>\n  <Creator>Interstellar (Arstro)</Creator>\n  <AssetList>\n";
        for (const auto &a : assets)
            p << "    <Asset>\n      <Id>" << urn(a.id) << "</Id>\n      <AnnotationText>" << esc(a.file) << "</AnnotationText>\n"
              << "      <Hash>" << a.hash << "</Hash>\n      <Size>" << a.size << "</Size>\n      <Type>" << a.type << "</Type>\n"
              << "      <OriginalFileName>" << esc(a.file) << "</OriginalFileName>\n    </Asset>\n";
        p << "  </AssetList>\n</PackingList>\n";
        const std::string pklFile = (mImf ? "PKL_" : "pkl_") + uuidText(pklId) + ".xml";
        if (!writeFile((fs::path(mDir) / pklFile).string(), p.str())) { mError = "cannot write " + pklFile; return false; }
        long long pklSize = 0;
        {
            std::string unused;
            sha1((fs::path(mDir) / pklFile).string(), unused, pklSize);
        }
        std::ostringstream m;
        m << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<AssetMap xmlns=\"http://www.smpte-ra.org/schemas/429-9/2007/AM\">\n"
          << "  <Id>" << urn(amId) << "</Id>\n  <AnnotationText>" << title << "</AnnotationText>\n  <Creator>Interstellar (Arstro)</Creator>\n"
          << "  <VolumeCount>1</VolumeCount>\n  <IssueDate>" << when << "</IssueDate>\n  <Issuer>Interstellar</Issuer>\n  <AssetList>\n";
        auto chunk = [&](const Uuid &id, const std::string &file, long long size, bool pkl) {
            m << "    <Asset>\n      <Id>" << urn(id) << "</Id>\n" << (pkl ? "      <PackingList>true</PackingList>\n" : "")
              << "      <ChunkList>\n        <Chunk>\n          <Path>" << esc(file) << "</Path>\n          <VolumeIndex>1</VolumeIndex>\n"
              << "          <Offset>0</Offset>\n          <Length>" << size << "</Length>\n        </Chunk>\n      </ChunkList>\n    </Asset>\n";
        };
        chunk(pklId, pklFile, pklSize, true);
        for (const auto &a : assets) chunk(a.id, a.file, a.size, false);
        m << "  </AssetList>\n</AssetMap>\n";
        if (!writeFile((fs::path(mDir) / "ASSETMAP.xml").string(), m.str())) { mError = "cannot write the ASSETMAP"; return false; }
        if (!mImf && !writeFile((fs::path(mDir) / "VOLINDEX.xml").string(),
                                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<VolumeIndex xmlns=\"http://www.smpte-ra.org/schemas/429-9/2007/AM\">\n  <Index>1</Index>\n</VolumeIndex>\n"))
        { mError = "cannot write the VOLINDEX"; return false; }
        return true;
    }
}
}
