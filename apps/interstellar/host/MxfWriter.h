/*
 *  interstellar_host — an MXF track file writer for packages: a DCP's (AS-DCP: OP-Atom, SMPTE ST 429-3
 *  and 429-4) and an IMF's (AS-02: OP1a, SMPTE ST 2067-5), each holding ONE essence — JPEG 2000 pictures
 *  frame by frame, or 24-bit PCM sound (frame-wrapped in a DCP, clip-wrapped in an IMF, MCA-labelled).
 *
 *  Its layout is the reference implementation's (asdcplib's asdcp-wrap / as-02-wrap) set for set: a
 *  16 KiB header (partition, primer, preface, identification, content storage, the material and file
 *  packages, the descriptors), the essence in a body partition, then the index table, the footer and the
 *  random index pack. The header is written first with a zero duration and rewritten in place at the end
 *  (its size does not depend on the numbers in it). FFmpeg 4.4's MXF muxer cannot do this: it writes no
 *  RGBA or JPEG 2000 sub-descriptor and refuses a multichannel OP-Atom sound file.
 */
#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar_host
{
    using Uuid = std::array<uint8_t, 16>;
    using UL = std::array<uint8_t, 16>;
    Uuid randomUuid();
    std::string uuidText(const Uuid &u);   // 8-4-4-4-12, lowercase
    UL ulOf(const char *hex);              // 32 hex digits, dots ignored

    /** What a JPEG 2000 codestream says about itself — an MXF JPEG 2000 sub-descriptor is exactly this. */
    struct J2kInfo
    {
        uint16_t rsiz = 0, csiz = 0;
        uint32_t xsiz = 0, ysiz = 0, xosiz = 0, yosiz = 0, xtsiz = 0, ytsiz = 0, xtosiz = 0, ytosiz = 0;
        std::vector<uint8_t> sizing;   // Ssiz XRsiz YRsiz per component
        std::vector<uint8_t> cod, qcd; // the COD and QCD marker segments' bodies
    };
    bool parseJ2k(const uint8_t *p, size_t n, J2kInfo &out);

    struct MxfTrackSpec
    {
        bool imf = false;              // AS-02 (OP1a) or AS-DCP (OP-Atom)
        bool sound = false;
        int rateNum = 24, rateDen = 1; // the picture rate (an IMF sound file's edit rate is its sample rate)
        Uuid asset{};                  // the track file's asset id: its file package's material number
        // picture
        int width = 0, height = 0;
        J2kInfo j2k;
        UL coding{};                   // PictureEssenceCoding
        UL transfer{}, primaries{}, equations{};   // IMF only
        // sound
        int channels = 2, sampleRate = 48000;      // 24-bit; a DCP's is labelled 5.1, an IMF's stereo
        std::string title;             // the soundfield group's MCATitle (ST 2067-2 asks for it)
    };

    class MxfWriter
    {
    public:
        ~MxfWriter();
        bool begin(const std::string &path, const MxfTrackSpec &spec, std::string &err);
        /** A codestream; a frame's interleaved 24-bit samples (DCP); or samples to append (IMF clip). */
        bool write(const uint8_t *data, size_t n, std::string &err);
        bool finish(std::string &err);
        long long duration() const;    // in edit units
        /** The descriptor's instance id, its sub-descriptors' (the JPEG 2000 one; the MCA labels, the
         *  soundfield group first) and the MCA link ids — an IMF CPL's essence descriptor repeats them. */
        Uuid descriptorId() const { return mIds.empty() ? Uuid{} : mIds[6]; }
        Uuid subDescriptorId(int k) const { return mIds.size() > 30 ? (mSpec.sound ? mIds[22 + k] : mIds[7]) : Uuid{}; }
        Uuid mcaLinkId(int k) const { return mIds.size() > 30 ? mIds[30 + k] : Uuid{}; }

    private:
        MxfTrackSpec mSpec;
        std::FILE *mF = nullptr;
        std::string mPath;
        long long mFrames = 0, mSoundBytes = 0, mEssenceStart = 0, mClipLengthAt = 0;
        std::vector<uint64_t> mOffsets;  // each frame's KLV, from the essence container's start
        uint64_t mStream = 0;
        // every instance UID and the two packages' UMIDs, fixed at begin so the rewrite is the same size
        std::vector<Uuid> mIds;
        Uuid mMaterial{};
        std::array<uint8_t, 8> mWhen{};
        std::vector<uint8_t> header(uint64_t footer) const;
        std::vector<uint8_t> partition(const UL &key, uint64_t self, uint64_t prev, uint64_t footer, uint64_t headerBytes,
                                       uint64_t indexBytes, uint32_t indexSid, uint32_t bodySid) const;
        std::vector<uint8_t> indexSegment() const;
    };
}
}
