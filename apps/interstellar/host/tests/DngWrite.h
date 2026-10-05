/*
 *  A minimal CinemaDNG frame for the tests (R-MEDIA-1): uncompressed LinearRaw RGB, 16-bit, little-endian,
 *  the camera = linear sRGB (D65), with the FrameRate and TimeCodes tags a cinema camera writes.
 *  LibRaw refuses anything under 22 px a side (dcraw's floor), so frames are at least that.
 */
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
namespace dngtest
{
    struct W
    {
        std::vector<uint8_t> b;
        void u16(uint16_t v) { b.push_back(v & 255); b.push_back(v >> 8); }
        void u32(uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 255); }
        void at32(size_t o, uint32_t v) { for (int i = 0; i < 4; ++i) b[o + i] = (v >> (8 * i)) & 255; }
    };
    struct Tag { uint16_t tag, type; uint32_t count; std::vector<uint8_t> data; };
    inline std::vector<uint8_t> le16(std::initializer_list<uint16_t> v) { std::vector<uint8_t> o; for (auto x : v) { o.push_back(x & 255); o.push_back(x >> 8); } return o; }
    inline std::vector<uint8_t> le32(std::initializer_list<uint32_t> v) { std::vector<uint8_t> o; for (auto x : v) for (int i = 0; i < 4; ++i) o.push_back((x >> (8 * i)) & 255); return o; }
    inline std::vector<uint8_t> srat(std::initializer_list<double> v) { std::vector<uint8_t> o; for (double x : v) { int32_t n = (int32_t)(x * 10000.0); auto a = le32({(uint32_t)n, 10000u}); o.insert(o.end(), a.begin(), a.end()); } return o; }
    /** rgb: w*h*3 linear 16-bit values. tc: 8 TimeCodes bytes (BCD frames, secs, mins, hours, …) or empty. */
    inline bool write(const std::string &path, int w, int h, const std::vector<uint16_t> &rgb, double fps, const std::vector<uint8_t> &tc)
    {
        std::vector<Tag> t;
        const uint32_t dataBytes = (uint32_t)rgb.size() * 2;
        t.push_back({254, 4, 1, le32({0})});                       // NewSubFileType: main image
        t.push_back({256, 4, 1, le32({(uint32_t)w})});
        t.push_back({257, 4, 1, le32({(uint32_t)h})});
        t.push_back({258, 3, 3, le16({16, 16, 16})});
        t.push_back({259, 3, 1, le16({1, 0})});                    // no compression
        t.push_back({262, 3, 1, le16({34892, 0})});                // LinearRaw
        t.push_back({271, 2, 5, {'T', 'e', 's', 't', 0}});
        t.push_back({272, 2, 5, {'C', 'a', 'm', '1', 0}});
        t.push_back({273, 4, 1, le32({0})});                       // StripOffsets — patched
        t.push_back({274, 3, 1, le16({1, 0})});
        t.push_back({277, 3, 1, le16({3, 0})});
        t.push_back({278, 4, 1, le32({(uint32_t)h})});
        t.push_back({279, 4, 1, le32({dataBytes})});
        t.push_back({284, 3, 1, le16({1, 0})});
        t.push_back({50706, 1, 4, {1, 4, 0, 0}});                  // DNGVersion
        t.push_back({50707, 1, 4, {1, 1, 0, 0}});
        t.push_back({50708, 2, 9, {'T', 'e', 's', 't', ' ', 'C', 'a', 'm', 0}});
        // ColorMatrix1: XYZ → camera = XYZ → linear sRGB, so the camera IS linear sRGB (D65)
        t.push_back({50721, 10, 9, srat({3.2406, -1.5372, -0.4986, -0.9689, 1.8758, 0.0415, 0.0557, -0.2040, 1.0570})});
        t.push_back({50728, 5, 3, {}});                            // AsShotNeutral 1,1,1
        { auto a = le32({1, 1, 1, 1, 1, 1}); t.back().data = a; }
        t.push_back({50778, 3, 1, le16({21, 0})});                 // CalibrationIlluminant1: D65
        if (!tc.empty()) t.push_back({51043, 1, 8, tc});           // TimeCodes
        if (fps > 0) { auto a = le32({(uint32_t)(fps * 1000), 1000u}); t.push_back({51044, 10, 1, a}); }   // FrameRate
        // layout: header(8) | IFD | out-of-line values | pixels
        W o;
        o.b = {'I', 'I', 42, 0};
        o.u32(8);
        const size_t ifdSize = 2 + t.size() * 12 + 4;
        size_t extra = 8 + ifdSize;
        std::vector<std::pair<size_t, size_t>> patches;   // (entry value offset, extra offset)
        o.u16((uint16_t)t.size());
        std::vector<uint8_t> tail;
        size_t stripEntry = 0;
        for (const auto &x : t)
        {
            o.u16(x.tag); o.u16(x.type); o.u32(x.count);
            if (x.tag == 273) stripEntry = o.b.size();
            if (x.data.size() <= 4) { std::vector<uint8_t> d = x.data; d.resize(4, 0); o.b.insert(o.b.end(), d.begin(), d.end()); }
            else { o.u32((uint32_t)(extra + tail.size())); tail.insert(tail.end(), x.data.begin(), x.data.end()); if (tail.size() & 1) tail.push_back(0); }
        }
        o.u32(0);
        o.b.insert(o.b.end(), tail.begin(), tail.end());
        o.at32(stripEntry, (uint32_t)o.b.size());
        for (uint16_t v : rgb) o.u16(v);
        FILE *f = std::fopen(path.c_str(), "wb");
        if (!f) return false;
        const bool ok = std::fwrite(o.b.data(), 1, o.b.size(), f) == o.b.size();
        std::fclose(f);
        return ok;
    }
}
