#include "MxfWriter.h"
#include <cstring>
#include <ctime>
#include <random>

namespace arstro
{
namespace interstellar_host
{
    Uuid randomUuid()
    {
        static std::random_device rd;
        static std::mt19937_64 gen(((uint64_t)rd() << 32) ^ rd() ^ (uint64_t)std::time(nullptr));
        Uuid u;
        for (size_t i = 0; i < 16; i += 8)
        {
            const uint64_t r = gen();
            for (int k = 0; k < 8; ++k) u[i + k] = (uint8_t)(r >> (k * 8));
        }
        u[6] = (uint8_t)((u[6] & 0x0f) | 0x40);   // version 4
        u[8] = (uint8_t)((u[8] & 0x3f) | 0x80);   // RFC 4122 variant
        return u;
    }

    std::string uuidText(const Uuid &u)
    {
        static const char *hex = "0123456789abcdef";
        std::string s;
        for (int i = 0; i < 16; ++i)
        {
            if (i == 4 || i == 6 || i == 8 || i == 10) s += '-';
            s += hex[u[i] >> 4];
            s += hex[u[i] & 15];
        }
        return s;
    }

    UL ulOf(const char *hex)
    {
        UL u{};
        int n = 0;
        for (const char *p = hex; *p && n < 32; ++p)
        {
            const char c = *p;
            const int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (v < 0) continue;
            u[n / 2] = (uint8_t)(u[n / 2] | (n % 2 ? v : v << 4));
            ++n;
        }
        return u;
    }

    bool parseJ2k(const uint8_t *p, size_t n, J2kInfo &o)
    {
        // SOC, then marker segments up to the first tile (SOT): SIZ, COD and QCD are all a sub-descriptor needs
        if (n < 4 || p[0] != 0xff || p[1] != 0x4f) return false;
        size_t i = 2;
        bool siz = false;
        auto u16 = [&](size_t k) { return (uint32_t)(p[k] << 8 | p[k + 1]); };
        auto u32 = [&](size_t k) { return u16(k) << 16 | u16(k + 2); };
        while (i + 4 <= n)
        {
            const uint32_t marker = u16(i), len = u16(i + 2);
            if (marker == 0xff90 || i + 2 + len > n) break;   // SOT: the header is over
            const size_t body = i + 4, bodyLen = len - 2;
            if (marker == 0xff51 && bodyLen >= 36)
            {
                o.rsiz = (uint16_t)u16(body);
                o.xsiz = u32(body + 2), o.ysiz = u32(body + 6), o.xosiz = u32(body + 10), o.yosiz = u32(body + 14);
                o.xtsiz = u32(body + 18), o.ytsiz = u32(body + 22), o.xtosiz = u32(body + 26), o.ytosiz = u32(body + 30);
                o.csiz = (uint16_t)u16(body + 34);
                if (bodyLen < 36 + 3u * o.csiz) return false;
                o.sizing.assign(p + body + 36, p + body + 36 + 3 * o.csiz);
                siz = true;
            }
            else if (marker == 0xff52) o.cod.assign(p + body, p + body + bodyLen);
            else if (marker == 0xff5c) o.qcd.assign(p + body, p + body + bodyLen);
            i += 2 + len;
        }
        return siz && !o.cod.empty() && !o.qcd.empty();
    }

    namespace
    {
        struct Buf
        {
            std::vector<uint8_t> b;
            void u8(uint32_t v) { b.push_back((uint8_t)v); }
            void u16(uint32_t v) { u8(v >> 8); u8(v); }
            void u32(uint32_t v) { u16(v >> 16); u16(v); }
            void u64(uint64_t v) { u32((uint32_t)(v >> 32)); u32((uint32_t)v); }
            void bytes(const uint8_t *p, size_t n) { b.insert(b.end(), p, p + n); }
            void bytes(const std::vector<uint8_t> &v) { b.insert(b.end(), v.begin(), v.end()); }
            template <size_t N> void arr(const std::array<uint8_t, N> &a) { bytes(a.data(), N); }
            void ber4(size_t n) { u8(0x83); u8(n >> 16); u8(n >> 8); u8(n); }
            void klv(const UL &key, const std::vector<uint8_t> &v) { arr(key); ber4(v.size()); bytes(v); }
        };

        // a local set: 2-byte tags, 2-byte lengths
        struct Set
        {
            Buf v;
            void add(uint16_t tag, const std::vector<uint8_t> &x) { v.u16(tag); v.u16((uint32_t)x.size()); v.bytes(x); }
            template <size_t N> void add(uint16_t tag, const std::array<uint8_t, N> &x) { add(tag, std::vector<uint8_t>(x.begin(), x.end())); }
            void u8(uint16_t tag, uint32_t x) { add(tag, std::vector<uint8_t>{(uint8_t)x}); }
            void u16(uint16_t tag, uint32_t x) { Buf b; b.u16(x); add(tag, b.b); }
            void u32(uint16_t tag, uint32_t x) { Buf b; b.u32(x); add(tag, b.b); }
            void u64(uint16_t tag, uint64_t x) { Buf b; b.u64(x); add(tag, b.b); }
            void rational(uint16_t tag, uint32_t n, uint32_t d) { Buf b; b.u32(n); b.u32(d); add(tag, b.b); }
            void refs(uint16_t tag, const std::vector<Uuid> &ids) { Buf b; b.u32((uint32_t)ids.size()); b.u32(16); for (const auto &i : ids) b.arr(i); add(tag, b.b); }
            void uls(uint16_t tag, const std::vector<UL> &ls) { Buf b; b.u32((uint32_t)ls.size()); b.u32(16); for (const auto &l : ls) b.arr(l); add(tag, b.b); }
            void text(uint16_t tag, const std::string &s) { std::vector<uint8_t> o; for (unsigned char c : s) { o.push_back(0); o.push_back(c); } add(tag, o); }
        };

        UL setKey(uint8_t k) { UL u = ulOf("060e2b34025301010d01010101010000"); u[14] = k; return u; }
        std::array<uint8_t, 32> umid(const Uuid &material)
        {
            std::array<uint8_t, 32> u{};
            const UL prefix = ulOf("060a2b340101010501010f2013000000");
            std::memcpy(u.data(), prefix.data(), 16);
            std::memcpy(u.data() + 16, material.data(), 16);
            return u;
        }

        // the primer: every static tag a track file here uses, then the dynamic ones (asdcplib's numbering)
        const std::vector<std::pair<uint16_t, const char *>> &staticTags()
        {
            static const std::vector<std::pair<uint16_t, const char *>> k = {
                {0x0201, "060e2b34010101020407010000000000"}, {0x0202, "060e2b34010101020702020101030000"}, {0x1001, "060e2b34010101020601010406090000"},
                {0x1101, "060e2b34010101020601010301000000"}, {0x1102, "060e2b34010101020601010302000000"}, {0x1201, "060e2b34010101020702010301040000"},
                {0x1501, "060e2b34010101020702010301050000"}, {0x1502, "060e2b34010101020404010102060000"}, {0x1503, "060e2b34010101010404010105000000"},
                {0x1901, "060e2b34010101020601010405010000"}, {0x1902, "060e2b34010101020601010405020000"}, {0x2701, "060e2b34010101020601010601000000"},
                {0x3001, "060e2b34010101010406010100000000"}, {0x3002, "060e2b34010101010406010200000000"}, {0x3004, "060e2b34010101020601010401020000"},
                {0x3006, "060e2b34010101050601010305000000"}, {0x3201, "060e2b34010101020401060100000000"}, {0x3202, "060e2b34010101010401050201000000"},
                {0x3203, "060e2b34010101010401050202000000"}, {0x320c, "060e2b34010101010401030104000000"}, {0x320d, "060e2b34010101020401030205000000"}, {0x320e, "060e2b34010101010401010101000000"},
                {0x3210, "060e2b34010101020401020101010200"}, {0x3219, "060e2b34010101090401020101060100"}, {0x321a, "060e2b34010101020401020101030100"},
                {0x3401, "060e2b34010101020401050306000000"}, {0x3405, "060e2b34010101050401040401000000"}, {0x3406, "060e2b3401010105040105030b000000"},
                {0x3407, "060e2b3401010105040105030c000000"}, {0x3b02, "060e2b34010101020702011002040000"}, {0x3b03, "060e2b34010101020601010402010000"},
                {0x3b05, "060e2b34010101020301020105000000"}, {0x3b06, "060e2b34010101020601010406040000"}, {0x3b07, "060e2b34010101020301020104000000"},
                {0x3b08, "060e2b34010101040601010401080000"}, {0x3b09, "060e2b34010101050102020300000000"}, {0x3b0a, "060e2b34010101050102021002010000"},
                {0x3b0b, "060e2b34010101050102021002020000"}, {0x3c01, "060e2b34010101020520070102010000"}, {0x3c02, "060e2b34010101020520070103010000"},
                {0x3c03, "060e2b34010101020520070104000000"}, {0x3c04, "060e2b34010101020520070105010000"}, {0x3c05, "060e2b34010101020520070107000000"},
                {0x3c06, "060e2b34010101020702011002030000"}, {0x3c07, "060e2b3401010102052007010a000000"}, {0x3c08, "060e2b34010101020520070106010000"},
                {0x3c09, "060e2b34010101020520070101000000"}, {0x3c0a, "060e2b34010101010101150200000000"}, {0x3d01, "060e2b34010101040402030304000000"},
                {0x3d02, "060e2b34010101040402030104000000"}, {0x3d03, "060e2b34010101050402030101010000"}, {0x3d07, "060e2b34010101050402010104000000"},
                {0x3d09, "060e2b34010101050402030305000000"}, {0x3d0a, "060e2b34010101050402030201000000"}, {0x3d32, "060e2b34010101070402010105000000"},
                {0x3f05, "060e2b34010101040406020100000000"}, {0x3f06, "060e2b34010101040103040500000000"}, {0x3f07, "060e2b34010101040103040400000000"},
                {0x3f08, "060e2b34010101040404040101000000"}, {0x3f09, "060e2b34010101050404040106000000"}, {0x3f0a, "060e2b34010101050404040205000000"},
                {0x3f0b, "060e2b34010101050530040600000000"}, {0x3f0c, "060e2b340101010507020103010a0000"}, {0x3f0d, "060e2b34010101050702020101020000"},
                {0x3f0e, "060e2b34010101050404040107000000"}, {0x4401, "060e2b34010101010101151000000000"}, {0x4402, "060e2b34010101010103030201000000"},
                {0x4403, "060e2b34010101020601010406050000"}, {0x4404, "060e2b34010101020702011002050000"}, {0x4405, "060e2b34010101020702011001030000"},
                {0x4701, "060e2b34010101020601010402030000"}, {0x4801, "060e2b34010101020107010100000000"}, {0x4802, "060e2b34010101020107010201000000"},
                {0x4803, "060e2b34010101020601010402040000"}, {0x4804, "060e2b34010101020104010300000000"}, {0x4b01, "060e2b34010101020530040500000000"},
                {0x4b02, "060e2b34010101020702010301030000"},
            };
            return k;
        }
        // dynamic tags: SubDescriptors, then the JPEG 2000 sub-descriptor's (a picture file) or MCA's (a sound file)
        constexpr uint16_t kSubDescriptors = 0xffff;
        constexpr uint16_t kJ2k = 0xfffe;     // Rsiz … QuantizationDefault: fffe down to fff2
        constexpr uint16_t kMcaDict = 0xfffe, kMcaLink = 0xfffd, kMcaSymbol = 0xfffc, kMcaName = 0xfffb, kMcaLang = 0xfffa,
                           kMcaChannel = 0xfff9, kMcaGroupLink = 0xfff8, kMcaTitle = 0xfff7, kMcaTitleVersion = 0xfff6,
                           kMcaContentKind = 0xfff5, kMcaElementKind = 0xfff4;

        struct Mca { const char *dict, *symbol, *name; };
        // SMPTE ST 429-2 5.1 (a DCP), ST 377-4 / 2067-2 standard stereo (an IMF)
        const std::vector<Mca> &mcaDcp()
        {
            static const std::vector<Mca> k = {{"060e2b340401010d0302020100000000", "sg51", "5.1"},
                                               {"060e2b340401010d0302010100000000", "chL", "Left"},
                                               {"060e2b340401010d0302010200000000", "chR", "Right"},
                                               {"060e2b340401010d0302010300000000", "chC", "Center"},
                                               {"060e2b340401010d0302010400000000", "chLFE", "LFE"},
                                               {"060e2b340401010d0302010500000000", "chLs", "Left Surround"},
                                               {"060e2b340401010d0302010600000000", "chRs", "Right Surround"}};
            return k;
        }
        const std::vector<Mca> &mcaImf()
        {
            static const std::vector<Mca> k = {{"060e2b340401010d0302022001000000", "sgST", "Standard Stereo"},
                                               {"060e2b340401010d0302010100000000", "chL", "Left"},
                                               {"060e2b340401010d0302010200000000", "chR", "Right"}};
            return k;
        }

        const UL kGcMulti = ulOf("060e2b34040101030d010301027f0100");
        const UL kFill = ulOf("060e2b34010101020301021001000000");
        const UL kHeaderKey = ulOf("060e2b34020501010d01020101020400");
        const UL kBodyKey = ulOf("060e2b34020501010d01020101030400");
        const UL kFooterKey = ulOf("060e2b34020501010d01020101040400");
        const UL kPictureDef = ulOf("060e2b34040101010103020201000000"), kSoundDef = ulOf("060e2b34040101010103020202000000"),
                 kTimecodeDef = ulOf("060e2b34040101010103020101000000");
        constexpr size_t kHeaderSize = 0x4000;   // the reference implementation's header reservation
        constexpr uint32_t kIndexSid = 0x81, kBodySid = 1;

        // the ids every header write uses, by role
        enum Id
        {
            IPreface, IIdent, IStorage, IEcd, IMaterialPkg, ISourcePkg, IDescriptor, IJ2kSub, IMpTcTrack, IMpTcSeq, IMpTcComp,
            IMpTrack, IMpSeq, IMpClip, IFpTcTrack, IFpTcSeq, IFpTcComp, IFpTrack, IFpSeq, IFpClip, IGeneration, IIndex, IMca0,
            IMcaLink0 = IMca0 + 8, kIds = IMcaLink0 + 8
        };
    }

    MxfWriter::~MxfWriter()
    {
        if (mF) std::fclose(mF);
    }

    long long MxfWriter::duration() const
    {
        if (mSpec.sound && mSpec.imf) return mSoundBytes / (mSpec.channels * 3);
        return mFrames;
    }

    std::vector<uint8_t> MxfWriter::partition(const UL &key, uint64_t self, uint64_t prev, uint64_t footer, uint64_t headerBytes,
                                              uint64_t indexBytes, uint32_t indexSid, uint32_t bodySid) const
    {
        const bool imf = mSpec.imf;
        Buf v;
        v.u16(1);
        v.u16(imf ? 3 : 2);
        v.u32(1);   // KAG
        v.u64(self);
        v.u64(prev);
        v.u64(footer);
        v.u64(headerBytes);
        v.u64(indexBytes);
        v.u32(indexSid);
        v.u64(0);   // body offset
        v.u32(bodySid);
        v.arr(imf ? ulOf("060e2b34040101010d01020101010100") : ulOf("060e2b34040101020d01020110000000"));   // OP1a : OP-Atom
        const UL ec = mSpec.sound ? ulOf(imf ? "060e2b34040101010d01030102060200" : "060e2b34040101010d01030102060100")
                                  : ulOf(imf ? "060e2b340401010d0d010301020c0600" : "060e2b34040101070d010301020c0100");
        v.u32(2);
        v.u32(16);
        if (mSpec.sound) { v.arr(ec); v.arr(kGcMulti); }
        else { v.arr(kGcMulti); v.arr(ec); }
        Buf k;
        k.klv(key, v.b);
        return k.b;
    }

    std::vector<uint8_t> MxfWriter::header(uint64_t footer) const
    {
        const MxfTrackSpec &s = mSpec;
        const bool imf = s.imf, snd = s.sound;
        const long long dur = duration();
        const uint32_t rateN = imf && snd ? (uint32_t)s.sampleRate : (uint32_t)s.rateNum, rateD = imf && snd ? 1u : (uint32_t)s.rateDen;
        const UL ec = snd ? ulOf(imf ? "060e2b34040101010d01030102060200" : "060e2b34040101010d01030102060100")
                          : ulOf(imf ? "060e2b340401010d0d010301020c0600" : "060e2b34040101070d010301020c0100");
        const UL essenceKey = snd ? ulOf(imf ? "060e2b34010201010d01030116010201" : "060e2b34010201010d01030116010101")
                                  : ulOf("060e2b34010201010d01030115010801");
        const uint32_t trackNumber = (uint32_t)essenceKey[12] << 24 | essenceKey[13] << 16 | essenceKey[14] << 8 | essenceKey[15];
        const UL def = snd ? kSoundDef : kPictureDef;
        const auto material = umid(mMaterial), source = umid(s.asset);
        const std::vector<uint8_t> when(mWhen.begin(), mWhen.end());
        const uint32_t essenceTrack = imf ? 1 : 2;
        Buf out;
        auto emit = [&](uint8_t key, Set &set) { out.klv(setKey(key), set.v.b); };

        out.bytes(partition(kHeaderKey, 0, 0, footer, kHeaderSize - 140, 0, 0, 0));
        {
            Buf p;
            const auto &st = staticTags();
            std::vector<std::pair<uint16_t, UL>> tags;
            for (const auto &t : st) tags.emplace_back(t.first, ulOf(t.second));
            tags.emplace_back(kSubDescriptors, ulOf("060e2b34010101090601010406100000"));
            if (snd)
            {
                tags.emplace_back(kMcaDict, ulOf("060e2b340101010e0103070101000000"));
                tags.emplace_back(kMcaLink, ulOf("060e2b340101010e0103070105000000"));
                tags.emplace_back(kMcaSymbol, ulOf("060e2b340101010e0103070102000000"));
                tags.emplace_back(kMcaName, ulOf("060e2b340101010e0103070103000000"));
                tags.emplace_back(kMcaLang, ulOf("060e2b340101010d0301010203150000"));
                tags.emplace_back(kMcaChannel, ulOf("060e2b340101010e0103040a00000000"));
                tags.emplace_back(kMcaGroupLink, ulOf("060e2b340101010e0103070106000000"));
                tags.emplace_back(kMcaTitle, ulOf("060e2b340101010e0105100000000000"));
                tags.emplace_back(kMcaTitleVersion, ulOf("060e2b340101010e0105110000000000"));
                tags.emplace_back(kMcaContentKind, ulOf("060e2b340101010e0302010220000000"));
                tags.emplace_back(kMcaElementKind, ulOf("060e2b340101010e0302010221000000"));
            }
            else
            {
                for (int k = 0; k < 13; ++k)
                {
                    UL u = ulOf("060e2b340101010a0401060300000000");
                    u[12] = (uint8_t)(k + 1);   // Rsiz … QuantizationDefault
                    tags.emplace_back((uint16_t)(kJ2k - k), u);
                }
                tags.emplace_back((uint16_t)(kJ2k - 13), ulOf("060e2b340101010e040106030e000000"));   // J2CLayout
            }
            p.u32((uint32_t)tags.size());
            p.u32(18);
            for (const auto &t : tags) { p.u16(t.first); p.arr(t.second); }
            out.klv(ulOf("060e2b34020501010d01020101050100"), p.b);
        }
        std::vector<UL> containers = snd ? std::vector<UL>{ec, kGcMulti} : std::vector<UL>{kGcMulti, ec};
        {
            Set p;
            p.add(0x3c0a, mIds[IPreface]);
            p.add(0x3b02, when);
            p.u16(0x3b05, imf ? 0x0103 : 0x0102);
            p.u32(0x3b07, 1);
            p.add(0x3b08, mIds[ISourcePkg]);
            p.refs(0x3b06, {mIds[IIdent]});
            p.add(0x3b03, mIds[IStorage]);
            p.add(0x3b09, imf ? ulOf("060e2b34040101010d01020101010100") : ulOf("060e2b34040101020d01020110000000"));
            p.uls(0x3b0a, containers);
            p.uls(0x3b0b, {});
            emit(0x2f, p);
        }
        {
            Set p;
            p.add(0x3c0a, mIds[IIdent]);
            p.add(0x3c09, mIds[IGeneration]);
            p.text(0x3c01, "Arstro");
            p.text(0x3c02, "Interstellar");
            p.add(0x3c03, std::vector<uint8_t>(10, 0));
            p.text(0x3c04, "1.0");
            p.add(0x3c05, ulOf("8c1f5e2a4b7d4e9a9c3f2b6d1e0a7c54"));   // Interstellar's product id
            p.add(0x3c06, when);
            p.add(0x3c07, std::vector<uint8_t>(10, 0));
            p.text(0x3c08, "Linux");
            emit(0x30, p);
        }
        {
            Set p;
            p.add(0x3c0a, mIds[IStorage]);
            p.refs(0x1901, {mIds[ISourcePkg], mIds[IMaterialPkg]});
            p.refs(0x1902, {mIds[IEcd]});
            emit(0x18, p);
        }
        {
            Set p;
            p.add(0x3c0a, mIds[IEcd]);
            p.add(0x2701, source);
            p.u32(0x3f06, kIndexSid);
            p.u32(0x3f07, kBodySid);
            emit(0x23, p);
        }
        // a package: its tracks (a DCP's first carries timecode), each a sequence of one component
        auto package = [&](bool file) {
            Set p;
            p.add(0x3c0a, mIds[file ? ISourcePkg : IMaterialPkg]);
            p.add(0x4401, file ? source : material);
            p.text(0x4402, file ? (snd ? "File Package: PCM sound" : "File Package: JPEG 2000 pictures") : "Material Package");
            p.add(0x4405, when);
            p.add(0x4404, when);
            std::vector<Uuid> tracks;
            if (!imf) tracks.push_back(mIds[file ? IFpTcTrack : IMpTcTrack]);
            tracks.push_back(mIds[file ? IFpTrack : IMpTrack]);
            p.refs(0x4403, tracks);
            if (file) p.add(0x4701, mIds[IDescriptor]);
            emit(file ? 0x37 : 0x36, p);
            if (!imf)
            {
                Set t;
                t.add(0x3c0a, mIds[file ? IFpTcTrack : IMpTcTrack]);
                t.u32(0x4801, 1);
                t.u32(0x4804, 0);
                t.text(0x4802, "Timecode Track");
                t.add(0x4803, mIds[file ? IFpTcSeq : IMpTcSeq]);
                t.rational(0x4b01, rateN, rateD);
                t.u64(0x4b02, 0);
                emit(0x3b, t);
                Set q;
                q.add(0x3c0a, mIds[file ? IFpTcSeq : IMpTcSeq]);
                q.add(0x0201, kTimecodeDef);
                q.u64(0x0202, (uint64_t)dur);
                q.refs(0x1001, {mIds[file ? IFpTcComp : IMpTcComp]});
                emit(0x0f, q);
                Set c;
                c.add(0x3c0a, mIds[file ? IFpTcComp : IMpTcComp]);
                c.add(0x0201, kTimecodeDef);
                c.u64(0x0202, (uint64_t)dur);
                c.u16(0x1502, (uint32_t)((s.rateNum + s.rateDen / 2) / s.rateDen));
                c.u64(0x1501, 0);
                c.u8(0x1503, 0);
                emit(0x14, c);
            }
            Set t;
            t.add(0x3c0a, mIds[file ? IFpTrack : IMpTrack]);
            t.u32(0x4801, essenceTrack);
            t.u32(0x4804, file ? trackNumber : 0);
            t.text(0x4802, snd ? "Sound Track" : imf ? "Image Track" : "Picture Track");
            t.add(0x4803, mIds[file ? IFpSeq : IMpSeq]);
            t.rational(0x4b01, rateN, rateD);
            t.u64(0x4b02, 0);
            emit(0x3b, t);
            Set q;
            q.add(0x3c0a, mIds[file ? IFpSeq : IMpSeq]);
            q.add(0x0201, def);
            q.u64(0x0202, (uint64_t)dur);
            q.refs(0x1001, {mIds[file ? IFpClip : IMpClip]});
            emit(0x0f, q);
            Set c;
            c.add(0x3c0a, mIds[file ? IFpClip : IMpClip]);
            c.add(0x0201, def);
            c.u64(0x0202, (uint64_t)dur);
            c.u64(0x1201, 0);
            c.add(0x1101, file ? std::array<uint8_t, 32>{} : source);
            c.u32(0x1102, file ? 0 : essenceTrack);
            emit(0x11, c);
        };
        package(false);
        package(true);
        if (!snd)
        {
            Set d;
            d.add(0x3c0a, mIds[IDescriptor]);
            d.refs(kSubDescriptors, {mIds[IJ2kSub]});
            d.u32(0x3006, essenceTrack);
            d.rational(0x3001, rateN, rateD);
            d.u64(0x3002, (uint64_t)dur);
            d.add(0x3004, ec);
            d.u8(0x320c, 0);   // full frame
            d.u32(0x3203, (uint32_t)s.width);
            d.u32(0x3202, (uint32_t)s.height);
            d.rational(0x320e, (uint32_t)s.width, (uint32_t)s.height);
            if (imf) d.add(0x3210, s.transfer);
            d.add(0x3201, s.coding);
            if (imf)
            {
                d.add(0x321a, s.equations);
                d.add(0x3219, s.primaries);
                Buf map;   // VideoLineMap: progressive, no lines named (ST 2067-21 asks for the item)
                map.u32(2);
                map.u32(4);
                map.u32(0);
                map.u32(0);
                d.add(0x320d, map.b);
            }
            d.u32(0x3406, 4095);
            d.u32(0x3407, 0);
            if (imf) d.u8(0x3405, 0);
            std::vector<uint8_t> layout(16, 0);
            if (imf) { layout[0] = 'R'; layout[1] = 12; layout[2] = 'G'; layout[3] = 12; layout[4] = 'B'; layout[5] = 12; }
            d.add(0x3401, layout);
            emit(0x29, d);
            const J2kInfo &j = s.j2k;
            Set j2;
            j2.add(0x3c0a, mIds[IJ2kSub]);
            j2.u16(kJ2k, j.rsiz);
            j2.u32(kJ2k - 1, j.xsiz);
            j2.u32(kJ2k - 2, j.ysiz);
            j2.u32(kJ2k - 3, j.xosiz);
            j2.u32(kJ2k - 4, j.yosiz);
            j2.u32(kJ2k - 5, j.xtsiz);
            j2.u32(kJ2k - 6, j.ytsiz);
            j2.u32(kJ2k - 7, j.xtosiz);
            j2.u32(kJ2k - 8, j.ytosiz);
            j2.u16(kJ2k - 9, j.csiz);
            Buf sizing;
            sizing.u32(j.csiz);
            sizing.u32(3);
            sizing.bytes(j.sizing);
            j2.add(kJ2k - 10, sizing.b);
            j2.add(kJ2k - 11, j.cod);
            j2.add(kJ2k - 12, j.qcd);
            if (imf)
            {
                std::vector<uint8_t> layout(16, 0);   // J2CLayout: what the components are, as the pixel layout says
                layout[0] = 'R'; layout[1] = 12; layout[2] = 'G'; layout[3] = 12; layout[4] = 'B'; layout[5] = 12;
                j2.add(kJ2k - 13, layout);
            }
            emit(0x5a, j2);
        }
        else
        {
            const auto &mca = imf ? mcaImf() : mcaDcp();
            std::vector<Uuid> subs;
            for (size_t k = 0; k < mca.size(); ++k) subs.push_back(mIds[IMca0 + k]);
            Set d;
            d.add(0x3c0a, mIds[IDescriptor]);
            d.refs(kSubDescriptors, subs);
            d.u32(0x3006, essenceTrack);
            d.rational(0x3001, rateN, rateD);
            d.u64(0x3002, (uint64_t)dur);
            d.add(0x3004, ec);
            d.rational(0x3d03, (uint32_t)s.sampleRate, 1);
            d.u8(0x3d02, 0);
            d.u32(0x3d07, (uint32_t)s.channels);
            d.u32(0x3d01, 24);
            d.u16(0x3d0a, (uint32_t)(s.channels * 3));
            d.u32(0x3d09, (uint32_t)(s.channels * 3 * s.sampleRate));
            d.add(0x3d32, ulOf(imf ? "060e2b340401010d0402021004010000" : "060e2b340401010d0402021003020000"));
            emit(0x48, d);
            // the soundfield group, then a label per channel linked to it (the language is undetermined)
            for (size_t k = 0; k < mca.size(); ++k)
            {
                Set m;
                m.add(0x3c0a, mIds[IMca0 + k]);
                m.add(kMcaDict, ulOf(mca[k].dict));
                m.add(kMcaLink, mIds[IMcaLink0 + k]);
                m.text(kMcaSymbol, mca[k].symbol);
                m.text(kMcaName, mca[k].name);
                if (k > 0) m.u32(kMcaChannel, (uint32_t)k);
                m.add(kMcaLang, std::vector<uint8_t>{'u', 'n', 'd'});
                if (k > 0) m.add(kMcaGroupLink, mIds[IMcaLink0]);
                if (k == 0)
                {
                    // the soundfield group says what the sound is: the title's primary, complete main mix
                    m.text(kMcaTitle, s.title.empty() ? std::string("Untitled") : s.title);
                    m.text(kMcaTitleVersion, "1");
                    m.text(kMcaContentKind, "PRM");
                    m.text(kMcaElementKind, "FCMP");
                }
                emit(k == 0 ? 0x6c : 0x6b, m);
            }
        }
        // fill to the header's reservation
        const size_t used = out.b.size();
        if (used + 20 > kHeaderSize) return {};
        Buf fill;
        fill.arr(kFill);
        fill.ber4(kHeaderSize - used - 20);
        out.bytes(fill.b);
        out.b.resize(kHeaderSize, 0);
        return out.b;
    }

    std::vector<uint8_t> MxfWriter::indexSegment() const
    {
        const MxfTrackSpec &s = mSpec;
        const bool vbr = !s.sound;
        Set p;
        p.add(0x3c0a, mIds[IIndex]);
        if (s.imf && s.sound) p.rational(0x3f0b, (uint32_t)s.sampleRate, 1);
        else p.rational(0x3f0b, (uint32_t)s.rateNum, (uint32_t)s.rateDen);
        p.u64(0x3f0c, 0);
        p.u64(0x3f0d, (uint64_t)duration());
        const uint32_t frameBytes = s.sound && !s.imf ? (uint32_t)(20 + (uint64_t)s.sampleRate * s.rateDen / s.rateNum * s.channels * 3) : 0;
        p.u32(0x3f05, vbr ? 0 : s.imf ? (uint32_t)(s.channels * 3) : frameBytes);
        p.u32(0x3f06, kIndexSid);
        p.u32(0x3f07, kBodySid);
        p.u8(0x3f08, 0);
        p.u8(0x3f0e, 0);
        if (vbr)
        {
            Buf d;
            d.u32(1);
            d.u32(6);
            d.u8(0);
            d.u8(0);
            d.u32(0);
            p.add(0x3f09, d.b);
        }
        Buf e;
        e.u32(vbr ? (uint32_t)mOffsets.size() : 0);
        e.u32(11);
        if (vbr)
            for (const uint64_t off : mOffsets)
            {
                e.u8(0);
                e.u8(0);
                e.u8(0x80);   // every JPEG 2000 frame is a random access point
                e.u64(off);
            }
        p.add(0x3f0a, e.b);
        Buf k;
        k.klv(ulOf("060e2b34025301010d01020101100100"), p.v.b);
        return k.b;
    }

    bool MxfWriter::begin(const std::string &path, const MxfTrackSpec &spec, std::string &err)
    {
        mSpec = spec;
        mPath = path;
        mFrames = mSoundBytes = 0;
        mOffsets.clear();
        mStream = 0;
        mIds.clear();
        for (int i = 0; i < kIds; ++i) mIds.push_back(randomUuid());
        mMaterial = randomUuid();
        {
            const std::time_t now = std::time(nullptr);
            std::tm g{};
#ifdef _WIN32
            gmtime_s(&g, &now);  // MSVC/MinGW-ucrt param order: (dest, source)
#else
            gmtime_r(&now, &g);
#endif
            const int y = g.tm_year + 1900;
            mWhen = {(uint8_t)(y >> 8), (uint8_t)y, (uint8_t)(g.tm_mon + 1), (uint8_t)g.tm_mday, (uint8_t)g.tm_hour, (uint8_t)g.tm_min, (uint8_t)g.tm_sec, 0};
        }
        if (!spec.sound && (spec.j2k.csiz == 0 || spec.width <= 0)) { err = "an MXF picture file needs its first codestream's parameters"; return false; }
        if (spec.sound && !spec.imf && (spec.sampleRate * (long long)spec.rateDen) % spec.rateNum != 0)
        { err = "a DCP's sound needs a whole number of samples per frame"; return false; }
        mF = std::fopen(path.c_str(), "wb+");
        if (!mF) { err = "cannot write " + path; return false; }
        const auto h = header(0);
        if (h.empty()) { err = "the MXF header does not fit its reservation"; return false; }
        const auto body = partition(kBodyKey, kHeaderSize, 0, 0, 0, 0, 0, kBodySid);
        if (std::fwrite(h.data(), 1, h.size(), mF) != h.size() || std::fwrite(body.data(), 1, body.size(), mF) != body.size())
        { err = "cannot write " + path; return false; }
        mEssenceStart = (long long)(kHeaderSize + body.size());
        if (spec.sound && spec.imf)
        {
            // clip wrapping: one KLV for all the sound; its 8-byte length is filled in at the end
            Buf k;
            k.arr(ulOf("060e2b34010201010d01030116010201"));
            mClipLengthAt = mEssenceStart + 16;
            k.u8(0x87);
            for (int i = 0; i < 7; ++i) k.u8(0);
            if (std::fwrite(k.b.data(), 1, k.b.size(), mF) != k.b.size()) { err = "cannot write " + path; return false; }
        }
        return true;
    }

    bool MxfWriter::write(const uint8_t *data, size_t n, std::string &err)
    {
        if (!mF) { err = "the MXF writer is not open"; return false; }
        if (mSpec.sound && mSpec.imf)
        {
            if (n % (size_t)(mSpec.channels * 3)) { err = "sound comes in whole sample frames"; return false; }
            if (std::fwrite(data, 1, n, mF) != n) { err = "cannot write " + mPath; return false; }
            mSoundBytes += (long long)n;
            return true;
        }
        if (mSpec.sound && n != (size_t)((long long)mSpec.sampleRate * mSpec.rateDen / mSpec.rateNum * mSpec.channels * 3))
        { err = "a DCP sound frame is one picture frame of samples"; return false; }
        Buf k;
        k.arr(mSpec.sound ? ulOf("060e2b34010201010d01030116010101") : ulOf("060e2b34010201010d01030115010801"));
        k.ber4(n);
        if (std::fwrite(k.b.data(), 1, k.b.size(), mF) != k.b.size() || std::fwrite(data, 1, n, mF) != n) { err = "cannot write " + mPath; return false; }
        mOffsets.push_back(mStream);
        mStream += k.b.size() + n;
        ++mFrames;
        return true;
    }

    bool MxfWriter::finish(std::string &err)
    {
        if (!mF) { err = "the MXF writer is not open"; return false; }
        auto put = [&](const std::vector<uint8_t> &v) { return std::fwrite(v.data(), 1, v.size(), mF) == v.size(); };
        if (mSpec.sound && mSpec.imf)
        {
            std::fflush(mF);
            std::fseek(mF, mClipLengthAt + 1, SEEK_SET);
            uint8_t len[7];
            for (int i = 0; i < 7; ++i) len[i] = (uint8_t)((uint64_t)mSoundBytes >> (8 * (6 - i)));
            std::fwrite(len, 1, 7, mF);
            std::fseek(mF, 0, SEEK_END);
        }
        const uint64_t end = (uint64_t)std::ftell(mF);
        const auto index = indexSegment();
        uint64_t footer = 0;
        std::vector<std::pair<uint32_t, uint64_t>> rip = {{0, 0}, {kBodySid, kHeaderSize}};
        bool ok = true;
        if (!mSpec.imf)
        {
            // AS-DCP: the footer partition carries the index table
            footer = end;
            ok = put(partition(kFooterKey, footer, kHeaderSize, footer, 0, index.size(), kIndexSid, 0)) && put(index);
            rip.push_back({0, footer});
        }
        else
        {
            // AS-02: the index in a body partition of its own, then an empty footer
            const uint64_t indexAt = end;
            const auto indexPack = partition(kBodyKey, indexAt, kHeaderSize, 0, 0, index.size(), kIndexSid, 0);
            footer = indexAt + indexPack.size() + index.size();
            const auto indexPack2 = partition(kBodyKey, indexAt, kHeaderSize, footer, 0, index.size(), kIndexSid, 0);
            ok = put(indexPack2) && put(index) && put(partition(kFooterKey, footer, indexAt, footer, 0, 0, 0, 0));
            rip.push_back({0, indexAt});
            rip.push_back({0, footer});
        }
        Buf r;
        for (const auto &e : rip) { r.u32(e.first); r.u64(e.second); }
        r.u32((uint32_t)(16 + 4 + r.b.size() + 4));
        Buf rk;
        rk.klv(ulOf("060e2b34020501010d01020101110100"), r.b);
        ok = ok && put(rk.b);
        // the header again, now that the duration and the footer are known; an AS-02 body names its footer too
        const auto h = header(footer);
        ok = ok && !h.empty() && std::fseek(mF, 0, SEEK_SET) == 0 && put(h);
        if (mSpec.imf) ok = ok && std::fseek(mF, (long)kHeaderSize, SEEK_SET) == 0 && put(partition(kBodyKey, kHeaderSize, 0, footer, 0, 0, 0, kBodySid));
        ok = std::fclose(mF) == 0 && ok;
        mF = nullptr;
        if (!ok) err = "cannot finish " + mPath;
        return ok;
    }
}
}
