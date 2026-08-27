#include "Exif.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

namespace arstro
{
namespace cosmo_v2
{
namespace exif
{
    namespace
    {
        /** The APP1/Exif segment's TIFF payload, or empty. Only the JPEG marker walk lives here;
         *  everything below reads the TIFF block it returns. */
        std::vector<uint8_t> tiffBlockOf(const std::string &path, int &sofW, int &sofH)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in) return {};
            std::vector<uint8_t> head;
            head.reserve(256 * 1024);
            // 256 KB is generous: Exif lives at the very front of a JPEG, and reading the whole
            // file to find it would mean reading a 40 MB image to print its ISO.
            head.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            if (head.size() > 256 * 1024) head.resize(256 * 1024);
            if (head.size() < 4 || head[0] != 0xFF || head[1] != 0xD8) return {};   // not a JPEG

            size_t i = 2, exifStart = 0, exifEnd = 0;
            while (i + 4 <= head.size())
            {
                if (head[i] != 0xFF) { ++i; continue; }        // resync rather than give up
                const uint8_t marker = head[i + 1];
                if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) { i += 2; continue; }
                if (marker == 0xDA) break;                     // start of scan: no more metadata
                const size_t len = ((size_t)head[i + 2] << 8) | head[i + 3];
                if (len < 2 || i + 2 + len > head.size()) break;
                // SOF0..SOF3 / SOF5..SOF7 / SOF9..SOF15: the frame header, which carries the
                // real pixel dimensions. Read here rather than asked of the engine because the
                // engine only knows them once pixels are RESIDENT, and the panel must answer for
                // a photo whose pixels were evicted — and because a size that depends on whether
                // a frame happened to have landed is not a property of the file (R-SVC-9).
                if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 &&
                    marker != 0xCC && len >= 7 && sofW == 0)
                {
                    sofH = (int)(((unsigned)head[i + 5] << 8) | head[i + 6]);
                    sofW = (int)(((unsigned)head[i + 7] << 8) | head[i + 8]);
                }
                if (marker == 0xE1 && len >= 8 &&
                    std::memcmp(&head[i + 4], "Exif\0\0", 6) == 0)
                {
                    const size_t start = i + 4 + 6;
                    // Remembered, not returned: SOF comes AFTER APP1, and returning here is how
                    // the dimensions went missing on the one file that had both.
                    exifStart = start;
                    exifEnd = i + 2 + len;
                }
                i += 2 + len;
            }
            if (exifEnd > exifStart)
                return std::vector<uint8_t>(head.begin() + exifStart, head.begin() + exifEnd);
            return {};
        }

        struct Tiff
        {
            const uint8_t *p = nullptr;
            size_t n = 0;
            bool be = false;   // 'MM' = big-endian

            uint16_t u16(size_t off) const
            {
                if (off + 2 > n) return 0;
                return be ? (uint16_t)((p[off] << 8) | p[off + 1])
                          : (uint16_t)((p[off + 1] << 8) | p[off]);
            }
            uint32_t u32(size_t off) const
            {
                if (off + 4 > n) return 0;
                return be ? ((uint32_t)p[off] << 24 | (uint32_t)p[off + 1] << 16 |
                             (uint32_t)p[off + 2] << 8 | p[off + 3])
                          : ((uint32_t)p[off + 3] << 24 | (uint32_t)p[off + 2] << 16 |
                             (uint32_t)p[off + 1] << 8 | p[off]);
            }
        };

        std::string trimmed(std::string s)
        {
            while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
            size_t b = 0;
            while (b < s.size() && s[b] == ' ') ++b;
            return s.substr(b);
        }

        std::string fmt(const char *spec, double v)
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), spec, v);
            return buf;
        }

        /** One IFD entry, resolved to a display string. Returns false for a type this does not
         *  read — which is most of them, on purpose (see the header). */
        bool valueOf(const Tiff &t, size_t entry, std::string &out)
        {
            const uint16_t type = t.u16(entry + 2);
            const uint32_t count = t.u32(entry + 4);
            const size_t valOff = entry + 8;
            // A value of more than four bytes is stored elsewhere and the field holds an offset.
            const size_t data = (type == 2 && count > 4) || (type == 5) || (type == 10)
                                    ? (size_t)t.u32(valOff)
                                    : valOff;
            switch (type)
            {
            case 2:   // ASCII
            {
                if (data >= t.n) return false;
                size_t len = 0;
                while (len < count && data + len < t.n && t.p[data + len] != '\0') ++len;
                out = trimmed(std::string((const char *)t.p + data, len));
                return !out.empty();
            }
            case 3:   // SHORT
                out = std::to_string((unsigned)t.u16(data));
                return true;
            case 4:   // LONG
                out = std::to_string((unsigned long)t.u32(data));
                return true;
            case 5:   // RATIONAL (unsigned)
            case 10:  // SRATIONAL
            {
                if (data + 8 > t.n) return false;
                const double num = type == 5 ? (double)t.u32(data) : (double)(int32_t)t.u32(data);
                const double den = type == 5 ? (double)t.u32(data + 4) : (double)(int32_t)t.u32(data + 4);
                if (den == 0.0) return false;
                out = fmt("%.6g", num / den);
                return true;
            }
            default:
                return false;
            }
        }

        /** Raw rational, for the tags that need their own formatting (shutter as 1/x). */
        bool rationalOf(const Tiff &t, size_t entry, double &out)
        {
            const uint16_t type = t.u16(entry + 2);
            if (type != 5 && type != 10) return false;
            const size_t data = (size_t)t.u32(entry + 8);
            if (data + 8 > t.n) return false;
            const double num = type == 5 ? (double)t.u32(data) : (double)(int32_t)t.u32(data);
            const double den = type == 5 ? (double)t.u32(data + 4) : (double)(int32_t)t.u32(data + 4);
            if (den == 0.0) return false;
            out = num / den;
            return true;
        }

        const char *orientationName(unsigned v)
        {
            switch (v)
            {
            case 1: return "Normal";
            case 2: return "Mirrored";
            case 3: return "Rotated 180\xC2\xB0";
            case 4: return "Mirrored, rotated 180\xC2\xB0";
            case 5: return "Mirrored, rotated 90\xC2\xB0 CCW";
            case 6: return "Rotated 90\xC2\xB0 CW";
            case 7: return "Mirrored, rotated 90\xC2\xB0 CW";
            case 8: return "Rotated 90\xC2\xB0 CCW";
            default: return nullptr;
            }
        }

        /** Walk one IFD, collecting the tags we know. `exifIfd` receives the Exif sub-IFD offset
         *  when IFD0 points at one, so the caller can walk that too. */
        void walk(const Tiff &t, size_t ifd, std::vector<std::pair<std::string, std::string>> &out,
                  size_t &exifIfd)
        {
            if (ifd + 2 > t.n) return;
            const uint16_t count = t.u16(ifd);
            for (uint16_t e = 0; e < count; ++e)
            {
                const size_t entry = ifd + 2 + (size_t)e * 12;
                if (entry + 12 > t.n) return;
                const uint16_t tag = t.u16(entry);
                std::string v;
                switch (tag)
                {
                case 0x8769: exifIfd = (size_t)t.u32(entry + 8); break;   // Exif sub-IFD pointer
                case 0x010F: if (valueOf(t, entry, v)) out.push_back({"Camera make", v}); break;
                case 0x0110: if (valueOf(t, entry, v)) out.push_back({"Camera model", v}); break;
                case 0xA434: if (valueOf(t, entry, v)) out.push_back({"Lens", v}); break;
                case 0x0131: if (valueOf(t, entry, v)) out.push_back({"Software", v}); break;
                case 0x8298: if (valueOf(t, entry, v)) out.push_back({"Copyright", v}); break;
                case 0x013B: if (valueOf(t, entry, v)) out.push_back({"Artist", v}); break;
                case 0x9003: if (valueOf(t, entry, v)) out.push_back({"Taken", v}); break;
                case 0x0112:
                {
                    const char *name = orientationName(t.u16(entry + 8));
                    if (name) out.push_back({"Orientation", name});
                    break;
                }
                case 0x8827: if (valueOf(t, entry, v)) out.push_back({"ISO", "ISO " + v}); break;
                case 0x829A:   // ExposureTime
                {
                    double s = 0;
                    if (rationalOf(t, entry, s) && s > 0.0)
                        out.push_back({"Shutter", s >= 1.0 ? fmt("%.4g s", s)
                                                           : fmt("1/%.0f s", 1.0 / s)});
                    break;
                }
                case 0x829D:   // FNumber
                {
                    double f = 0;
                    if (rationalOf(t, entry, f) && f > 0.0) out.push_back({"Aperture", fmt("f/%.3g", f)});
                    break;
                }
                case 0x920A:   // FocalLength
                {
                    double mm = 0;
                    if (rationalOf(t, entry, mm) && mm > 0.0) out.push_back({"Focal length", fmt("%.0f mm", mm)});
                    break;
                }
                case 0x9204:   // ExposureBiasValue
                {
                    double ev = 0;
                    if (rationalOf(t, entry, ev)) out.push_back({"Exposure bias", fmt("%+.2g EV", ev)});
                    break;
                }
                default: break;
                }
            }
        }
    }

    std::vector<std::pair<std::string, std::string>> read(const std::string &path)
    {
        std::vector<std::pair<std::string, std::string>> out;
        int sofW = 0, sofH = 0;
        const std::vector<uint8_t> blk = tiffBlockOf(path, sofW, sofH);
        if (sofW > 0 && sofH > 0)
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%d x %d", sofW, sofH);
            out.push_back({"Dimensions", buf});
            std::snprintf(buf, sizeof(buf), "%.1f MP", (double)sofW * (double)sofH / 1e6);
            out.push_back({"Resolution", buf});
        }
        if (blk.size() < 8) return out;
        Tiff t;
        t.p = blk.data();
        t.n = blk.size();
        if (t.p[0] == 'M' && t.p[1] == 'M') t.be = true;
        else if (!(t.p[0] == 'I' && t.p[1] == 'I')) return out;
        if (t.u16(2) != 42) return out;                 // the TIFF magic; a sanity check
        const size_t ifd0 = (size_t)t.u32(4);
        size_t exifIfd = 0;
        walk(t, ifd0, out, exifIfd);
        if (exifIfd > 0 && exifIfd < t.n)
        {
            size_t ignored = 0;
            walk(t, exifIfd, out, ignored);
        }
        return out;
    }
}
}
}
