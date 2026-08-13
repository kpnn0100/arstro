#include "ExportWriter.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
namespace exporter
{
    namespace
    {
        // A compact sRGB IEC61966-2.1 v2 matrix/shaper profile (D50-adapted primaries,
        // gamma 2.2 TRCs). Generated once and embedded so an export never depends on
        // a system colour-profile package being installed.
        const unsigned char kSrgbIcc[] = {
        0x00, 0x00, 0x01, 0xD4, 0x00, 0x00, 0x00, 0x00, 0x02, 0x10, 0x00, 0x00,
        0x6D, 0x6E, 0x74, 0x72, 0x52, 0x47, 0x42, 0x20, 0x58, 0x59, 0x5A, 0x20,
        0x07, 0xE8, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x61, 0x63, 0x73, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF6, 0xD6,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0xD3, 0x2D, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09,
        0x64, 0x65, 0x73, 0x63, 0x00, 0x00, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x6C,
        0x77, 0x74, 0x70, 0x74, 0x00, 0x00, 0x01, 0x5C, 0x00, 0x00, 0x00, 0x14,
        0x72, 0x58, 0x59, 0x5A, 0x00, 0x00, 0x01, 0x70, 0x00, 0x00, 0x00, 0x14,
        0x67, 0x58, 0x59, 0x5A, 0x00, 0x00, 0x01, 0x84, 0x00, 0x00, 0x00, 0x14,
        0x62, 0x58, 0x59, 0x5A, 0x00, 0x00, 0x01, 0x98, 0x00, 0x00, 0x00, 0x14,
        0x72, 0x54, 0x52, 0x43, 0x00, 0x00, 0x01, 0xAC, 0x00, 0x00, 0x00, 0x0E,
        0x67, 0x54, 0x52, 0x43, 0x00, 0x00, 0x01, 0xAC, 0x00, 0x00, 0x00, 0x0E,
        0x62, 0x54, 0x52, 0x43, 0x00, 0x00, 0x01, 0xAC, 0x00, 0x00, 0x00, 0x0E,
        0x63, 0x70, 0x72, 0x74, 0x00, 0x00, 0x01, 0xBC, 0x00, 0x00, 0x00, 0x16,
        0x64, 0x65, 0x73, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12,
        0x73, 0x52, 0x47, 0x42, 0x20, 0x49, 0x45, 0x43, 0x36, 0x31, 0x39, 0x36,
        0x36, 0x2D, 0x32, 0x2E, 0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x58, 0x59, 0x5A, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF6, 0xD6,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0xD3, 0x2D, 0x58, 0x59, 0x5A, 0x20,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6F, 0x9E, 0x00, 0x00, 0x38, 0xF6,
        0x00, 0x00, 0x03, 0x8F, 0x58, 0x59, 0x5A, 0x20, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x62, 0x96, 0x00, 0x00, 0xB7, 0x87, 0x00, 0x00, 0x18, 0xDC,
        0x58, 0x59, 0x5A, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x24, 0xA2,
        0x00, 0x00, 0x0F, 0x83, 0x00, 0x00, 0xB6, 0xCF, 0x63, 0x75, 0x72, 0x76,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x33, 0x00, 0x00,
        0x74, 0x65, 0x78, 0x74, 0x00, 0x00, 0x00, 0x00, 0x50, 0x75, 0x62, 0x6C,
        0x69, 0x63, 0x20, 0x44, 0x6F, 0x6D, 0x61, 0x69, 0x6E, 0x00, 0x00, 0x00,
        };

        std::string folderOf(const std::string &p)
        {
            const auto s = p.find_last_of('/');
            return s == std::string::npos ? std::string() : p.substr(0, s);
        }
        std::string fileOf(const std::string &p)
        {
            const auto s = p.find_last_of('/');
            return s == std::string::npos ? p : p.substr(s + 1);
        }
        std::string stemOf(const std::string &name)
        {
            const auto d = name.find_last_of('.');
            return d == std::string::npos ? name : name.substr(0, d);
        }
        bool isJpegPath(const std::string &p)
        {
            std::string lower = p;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            return lower.size() > 4 && (lower.rfind(".jpg") == lower.size() - 4 ||
                                        lower.rfind(".jpeg") == lower.size() - 5);
        }

        // ── EXIF (R-EXPORT-5) ──────────────────────────────────────────────────
        //
        // GdkPixbuf's JPEG writer emits no EXIF, so "Embed EXIF data" means: lift the
        // source JPEG's APP1/Exif segment verbatim and splice it into the file we just
        // wrote, right after SOI. Only JPEG sources have one (a RAW/PNG source simply
        // yields nothing and the toggle no-ops, per R-EXPORT-5).

        /** The full APP1 segment (marker + length + payload) of `path`, or empty. */
        std::vector<uint8_t> readJpegApp1(const std::string &path)
        {
            std::ifstream f(path, std::ios::binary);
            if (!f) return {};
            uint8_t soi[2];
            if (!f.read((char *)soi, 2) || soi[0] != 0xFF || soi[1] != 0xD8) return {};
            for (;;)
            {
                int c = f.get();
                if (c == EOF) return {};
                if (c != 0xFF) continue;                 // resync to the next marker
                int marker = f.get();
                while (marker == 0xFF) marker = f.get(); // fill bytes
                if (marker == EOF || marker == 0xD8) continue;
                if (marker == 0xD9 || marker == 0xDA) return {};   // EOI / start of scan
                uint8_t lenb[2];
                if (!f.read((char *)lenb, 2)) return {};
                const int len = (lenb[0] << 8) | lenb[1];
                if (len < 2) return {};
                std::vector<uint8_t> payload((size_t)len - 2);
                if (len > 2 && !f.read((char *)payload.data(), len - 2)) return {};
                if (marker == 0xE1 && payload.size() >= 6 && std::memcmp(payload.data(), "Exif\0\0", 6) == 0)
                {
                    std::vector<uint8_t> seg;
                    seg.push_back(0xFF); seg.push_back(0xE1);
                    seg.push_back(lenb[0]); seg.push_back(lenb[1]);
                    seg.insert(seg.end(), payload.begin(), payload.end());
                    return seg;
                }
            }
        }

        inline uint16_t rd16(const uint8_t *p, bool be) { return be ? (uint16_t)((p[0] << 8) | p[1]) : (uint16_t)((p[1] << 8) | p[0]); }
        inline void wr16(uint8_t *p, uint16_t v, bool be)
        { if (be) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; } else { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); } }
        inline uint32_t rd32(const uint8_t *p, bool be)
        { return be ? ((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3])
                    : ((uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]); }

        /** Remove IFD0's GPS-IFD-pointer entry (tag 0x8825) in place, so the written
         *  file carries no location. Entries are fixed 12-byte records, so dropping one
         *  and decrementing the count keeps every other offset valid (offsets in the
         *  IFD are absolute from the TIFF header, and we move no data). */
        void stripGps(std::vector<uint8_t> &app1)
        {
            // Layout: FF E1 | len(2) | "Exif\0\0"(6) | TIFF header ...
            constexpr size_t kTiff = 4 + 6;
            if (app1.size() < kTiff + 8) return;
            uint8_t *tiff = app1.data() + kTiff;
            const size_t tiffLen = app1.size() - kTiff;
            const bool be = tiff[0] == 'M' && tiff[1] == 'M';
            if (!be && !(tiff[0] == 'I' && tiff[1] == 'I')) return;
            if (rd16(tiff + 2, be) != 42) return;
            const uint32_t ifd0 = rd32(tiff + 4, be);
            if (ifd0 + 2 > tiffLen) return;
            uint8_t *ifd = tiff + ifd0;
            const uint16_t count = rd16(ifd, be);
            if (ifd0 + 2 + (size_t)count * 12 > tiffLen) return;
            for (uint16_t i = 0; i < count; ++i)
            {
                uint8_t *entry = ifd + 2 + (size_t)i * 12;
                if (rd16(entry, be) != 0x8825) continue;   // GPSInfoIFDPointer
                const size_t entryOff = (size_t)(entry - app1.data());
                app1.erase(app1.begin() + entryOff, app1.begin() + entryOff + 12);
                wr16(app1.data() + kTiff + ifd0, (uint16_t)(count - 1), be);
                // The APP1 length field counts itself + the payload.
                const uint16_t newLen = (uint16_t)(app1.size() - 2);
                app1[2] = (uint8_t)(newLen >> 8);
                app1[3] = (uint8_t)newLen;
                return;
            }
        }

        /** Rewrite `path` with `app1` spliced in directly after SOI (replacing any APP1
         *  already present). Returns false and leaves the file untouched on any error. */
        bool injectApp1(const std::string &path, const std::vector<uint8_t> &app1)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in) return false;
            std::vector<uint8_t> jpeg((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            in.close();
            if (jpeg.size() < 4 || jpeg[0] != 0xFF || jpeg[1] != 0xD8) return false;

            // Skip an existing APP1/Exif (GdkPixbuf writes none, but be defensive so we
            // can never end up with two).
            size_t insertAt = 2;
            if (jpeg.size() > 4 && jpeg[2] == 0xFF && jpeg[3] == 0xE1)
            {
                const size_t len = ((size_t)jpeg[4] << 8) | jpeg[5];
                if (2 + 2 + len <= jpeg.size()) jpeg.erase(jpeg.begin() + 2, jpeg.begin() + 2 + 2 + len);
            }
            jpeg.insert(jpeg.begin() + insertAt, app1.begin(), app1.end());

            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out) return false;
            out.write((const char *)jpeg.data(), (std::streamsize)jpeg.size());
            return (bool)out;
        }

        // ── PNG sRGB declaration (R-EXPORT-5) ──────────────────────────────────
        //
        // GdkPixbuf's PNG saver rejects the icc-profile option (it sets iCCP before the
        // colour type, so libpng sees a "grayscale" image and refuses an RGB profile).
        // The PNG spec's own sRGB chunk says the same thing with no profile blob and no
        // compression, so that is what we write, plus the gAMA the spec pairs with it.

        uint32_t crc32Png(const uint8_t *data, size_t len)
        {
            static uint32_t table[256];
            static bool init = false;
            if (!init)
            {
                for (uint32_t n = 0; n < 256; ++n)
                {
                    uint32_t c = n;
                    for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
                    table[n] = c;
                }
                init = true;
            }
            uint32_t c = 0xFFFFFFFFu;
            for (size_t i = 0; i < len; ++i) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
            return c ^ 0xFFFFFFFFu;
        }

        void appendPngChunk(std::vector<uint8_t> &out, const char type[4], const uint8_t *data, size_t len)
        {
            const uint32_t l = (uint32_t)len;
            out.push_back((uint8_t)(l >> 24)); out.push_back((uint8_t)(l >> 16));
            out.push_back((uint8_t)(l >> 8));  out.push_back((uint8_t)l);
            std::vector<uint8_t> crcBuf;
            crcBuf.insert(crcBuf.end(), type, type + 4);
            crcBuf.insert(crcBuf.end(), data, data + len);
            out.insert(out.end(), crcBuf.begin(), crcBuf.end());
            const uint32_t crc = crc32Png(crcBuf.data(), crcBuf.size());
            out.push_back((uint8_t)(crc >> 24)); out.push_back((uint8_t)(crc >> 16));
            out.push_back((uint8_t)(crc >> 8));  out.push_back((uint8_t)crc);
        }

        /** Insert sRGB + gAMA chunks right after IHDR, where the spec requires them. */
        bool addPngSrgbChunks(const std::string &path)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in) return false;
            std::vector<uint8_t> png((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            in.close();
            static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
            if (png.size() < 8 + 25 || std::memcmp(png.data(), sig, 8) != 0) return false;
            // IHDR is always the first chunk: 4 len + 4 type + 13 data + 4 crc = 25 bytes.
            const size_t insertAt = 8 + 25;

            std::vector<uint8_t> extra;
            const uint8_t intent = 0;                       // 0 = perceptual
            appendPngChunk(extra, "sRGB", &intent, 1);
            const uint32_t gamma = 45455;                   // 1/2.2 * 100000, per the spec
            const uint8_t g[4] = {(uint8_t)(gamma >> 24), (uint8_t)(gamma >> 16),
                                  (uint8_t)(gamma >> 8), (uint8_t)gamma};
            appendPngChunk(extra, "gAMA", g, 4);

            png.insert(png.begin() + insertAt, extra.begin(), extra.end());
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out) return false;
            out.write((const char *)png.data(), (std::streamsize)png.size());
            return (bool)out;
        }
    }

    // ── path resolution (R-EXPORT-3) ───────────────────────────────────────────

    std::string resolvePath(const ExportDialog::Request &req, const std::string &sourcePath,
                            const std::string &fallbackName)
    {
        std::string dir = req.sameAsSource ? folderOf(sourcePath) : req.destination;
        if (dir.empty()) dir = folderOf(sourcePath);
        if (dir.empty()) dir = ".";
        if (req.useSubfolder && !req.subfolder.empty()) dir += "/" + req.subfolder;
        g_mkdir_with_parents(dir.c_str(), 0755);

        std::string base = sourcePath.empty() ? fallbackName : fileOf(sourcePath);
        if (base.empty()) base = "export";
        base = stemOf(base);
        const std::string ext = req.format == "PNG" ? ".png" : req.format == "TIFF" ? ".tif" : ".jpg";
        return dir + "/" + (req.usePrefix ? req.prefix : std::string()) + base + ext;
    }

    // ── encode ─────────────────────────────────────────────────────────────────

    bool write(const ExportDialog::Request &req, const uint8_t *rgba, int w, int h,
               const std::string &path, const std::string &sourcePath, std::string &error)
    {
        if (!rgba || w <= 0 || h <= 0) { error = "nothing rendered"; return false; }

        const bool jpeg = req.format == "JPEG";
        const bool png = req.format == "PNG";

        // JPEG has no alpha channel, so that path packs a tight RGB copy rather than
        // handing the encoder the RGBA buffer PNG/TIFF can take directly.
        std::vector<uint8_t> rgb;
        GdkPixbuf *pb = nullptr;
        if (jpeg)
        {
            rgb.resize((size_t)w * h * 3);
            for (size_t i = 0, n = (size_t)w * h; i < n; ++i)
            {
                rgb[i * 3 + 0] = rgba[i * 4 + 0];
                rgb[i * 3 + 1] = rgba[i * 4 + 1];
                rgb[i * 3 + 2] = rgba[i * 4 + 2];
            }
            pb = gdk_pixbuf_new_from_data(rgb.data(), GDK_COLORSPACE_RGB, FALSE, 8, w, h, w * 3, nullptr, nullptr);
        }
        else
        {
            pb = gdk_pixbuf_new_from_data(rgba, GDK_COLORSPACE_RGB, TRUE, 8, w, h, w * 4, nullptr, nullptr);
        }
        if (!pb) { error = "could not wrap the rendered frame"; return false; }

        // R-EXPORT-4 size: cap the LONG edge, downscale only (never upscale a photo).
        GdkPixbuf *scaled = nullptr;
        if (req.longEdge > 0 && std::max(w, h) > req.longEdge)
        {
            const double k = (double)req.longEdge / (double)std::max(w, h);
            const int nw = std::max(1, (int)(w * k + 0.5)), nh = std::max(1, (int)(h * k + 0.5));
            scaled = gdk_pixbuf_scale_simple(pb, nw, nh, GDK_INTERP_BILINEAR);
        }
        GdkPixbuf *out = scaled ? scaled : pb;

        std::vector<const char *> keys;
        std::vector<const char *> vals;
        char qbuf[8];
        std::string iccB64;
        if (jpeg)
        {
            std::snprintf(qbuf, sizeof(qbuf), "%d", req.quality);
            keys.push_back("quality"); vals.push_back(qbuf);
        }
        if (req.embedProfile && !png)   // JPEG/TIFF carry the profile; PNG gets chunks below
        {
            gchar *b64 = g_base64_encode(kSrgbIcc, sizeof(kSrgbIcc));
            iccB64 = b64 ? b64 : "";
            g_free(b64);
            if (!iccB64.empty()) { keys.push_back("icc-profile"); vals.push_back(iccB64.c_str()); }
        }
        keys.push_back(nullptr); vals.push_back(nullptr);

        GError *err = nullptr;
        const char *type = jpeg ? "jpeg" : png ? "png" : "tiff";
        const gboolean ok = gdk_pixbuf_savev(out, path.c_str(), type,
                                             const_cast<char **>(keys.data()),
                                             const_cast<char **>(vals.data()), &err);
        if (scaled) g_object_unref(scaled);
        g_object_unref(pb);
        if (!ok)
        {
            error = err ? err->message : "encode failed";
            if (err) g_error_free(err);
            return false;
        }
        if (err) g_error_free(err);

        // ── post-encode metadata (R-EXPORT-5) ──
        if (png && req.embedProfile) addPngSrgbChunks(path);
        if (jpeg && req.embedExif && isJpegPath(sourcePath))
        {
            std::vector<uint8_t> app1 = readJpegApp1(sourcePath);
            if (!app1.empty())
            {
                if (req.stripGps) stripGps(app1);
                injectApp1(path, app1);
            }
        }
        return true;
    }
}
}
}
