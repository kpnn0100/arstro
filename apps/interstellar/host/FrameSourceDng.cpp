#include "FrameSourceDng.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#ifdef INTERSTELLAR_HAVE_LIBRAW
#include <libraw/libraw.h>
#endif

namespace arstro
{
namespace interstellar_host
{
    bool readCinemaDngTags(const std::string &file, double &fps, std::string &timecode)
    {
        // TIFF: "II" or "MM", 42, the offset of IFD0; each entry is tag, type, count, value/offset
        std::ifstream f(file, std::ios::binary);
        unsigned char h[8];
        if (!f.read((char *)h, 8)) return false;
        const bool le = h[0] == 'I';
        if (!(le && h[1] == 'I') && !(h[0] == 'M' && h[1] == 'M')) return false;
        auto u16 = [&](const unsigned char *p) { return le ? (unsigned)(p[0] | p[1] << 8) : (unsigned)(p[0] << 8 | p[1]); };
        auto u32 = [&](const unsigned char *p) {
            return le ? (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24
                      : (uint32_t)p[3] | (uint32_t)p[2] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[0] << 24;
        };
        if (u16(h + 2) != 42) return false;
        f.seekg(u32(h + 4));
        unsigned char nb[2];
        if (!f.read((char *)nb, 2)) return false;
        const unsigned n = u16(nb);
        std::vector<unsigned char> e((size_t)n * 12);
        if (n == 0 || n > 4096 || !f.read((char *)e.data(), (std::streamsize)e.size())) return false;
        for (unsigned i = 0; i < n; ++i)
        {
            const unsigned char *p = &e[(size_t)i * 12];
            const unsigned tag = u16(p), type = u16(p + 2);
            const uint32_t count = u32(p + 4);
            if (tag == 51044 && (type == 5 || type == 10) && count >= 1)   // FrameRate: (S)RATIONAL, out of line
            {
                unsigned char r[8];
                f.clear();
                f.seekg(u32(p + 8));
                if (f.read((char *)r, 8))
                {
                    const double num = type == 10 ? (double)(int32_t)u32(r) : (double)u32(r);
                    const double den = type == 10 ? (double)(int32_t)u32(r + 4) : (double)u32(r + 4);
                    if (den != 0 && num / den > 0 && num / den < 1000) fps = num / den;
                }
            }
            else if (tag == 51043 && type == 1 && count >= 4)   // TimeCodes: SMPTE 12M bytes, BCD, out of line
            {
                unsigned char t[8];
                f.clear();
                f.seekg(u32(p + 8));
                if (f.read((char *)t, 8))
                {
                    auto bcd = [](unsigned char b, unsigned char mask) { return ((b & mask) >> 4) * 10 + (b & 0x0F); };
                    char buf[16];
                    std::snprintf(buf, sizeof buf, "%02d:%02d:%02d%c%02d", bcd(t[3], 0x30), bcd(t[2], 0x70), bcd(t[1], 0x70),
                                  (t[0] & 0x40) ? ';' : ':', bcd(t[0], 0x30));
                    timecode = buf;
                }
            }
        }
        return true;
    }

#ifdef INTERSTELLAR_HAVE_LIBRAW
    struct FrameSourceDng::Impl
    {
        LibRaw raw;
    };
#else
    struct FrameSourceDng::Impl {};
#endif

    FrameSourceDng::FrameSourceDng() : mImpl(new Impl()) {}
    FrameSourceDng::~FrameSourceDng() = default;

    bool FrameSourceDng::open(const std::string &pattern, Info &out)
    {
        mHave = -1;
        mDeep = interstellar::Raster{};
        if (!interstellar::seq::parse(pattern, mPattern)) { mWhy = pattern + " is not a numbered .dng sequence"; return false; }
        mNumbers = interstellar::seq::numbers(mPattern);
        if (mNumbers.empty()) { mWhy = "no frame of " + pattern + " is on disk"; return false; }
#ifndef INTERSTELLAR_HAVE_LIBRAW
        mWhy = "this build has no LibRaw — a CinemaDNG sequence cannot be developed";
        return false;
#else
        mFrames = mNumbers.back() - mNumbers.front() + 1;
        if (!develop(0)) return false;
        out.width = mDeep.width;
        out.height = mDeep.height;
        out.frames = mFrames;
        out.bitDepth = 16;
        out.fps = 24.0;
        std::string tc;
        double fps = 0;
        if (readCinemaDngTags(interstellar::seq::fileOf(mPattern, mNumbers.front()), fps, tc))
        {
            if (fps > 0) out.fps = fps;
            out.timecode = tc;
        }
        out.reel = interstellar::seq::stem(pattern);   // CinemaDNG names a clip by its folder; the reel is that name
        return true;
#endif
    }

    bool FrameSourceDng::develop(long long frame)
    {
#ifndef INTERSTELLAR_HAVE_LIBRAW
        (void)frame;
        return false;
#else
        frame = std::clamp<long long>(frame, 0, std::max<long long>(0, mFrames - 1));
        if (frame == mHave) return true;
        // the file numbered first + frame, or the nearest before it (a dropped frame holds)
        const long long want = mNumbers.front() + frame;
        const auto it = std::upper_bound(mNumbers.begin(), mNumbers.end(), want);
        const long long number = *(it == mNumbers.begin() ? it : std::prev(it));
        LibRaw &r = mImpl->raw;
        r.recycle();
        const std::string file = interstellar::seq::fileOf(mPattern, number);
        int e = r.open_file(file.c_str());
        if (e == LIBRAW_SUCCESS) e = r.unpack();
        if (e == LIBRAW_SUCCESS)
        {
            r.imgdata.params.output_bps = 16;
            r.imgdata.params.no_auto_bright = 1;   // the camera's exposure, every frame alike
            r.imgdata.params.use_camera_wb = 1;
            r.imgdata.params.output_color = 1;     // sRGB/Rec.709 primaries; the BT.709 curve is LibRaw's default
            e = r.dcraw_process();
        }
        libraw_processed_image_t *img = e == LIBRAW_SUCCESS ? r.dcraw_make_mem_image(&e) : nullptr;
        if (!img || img->type != LIBRAW_IMAGE_BITMAP || img->colors != 3 || img->bits != 16)
        {
            mWhy = file + ": " + (e != LIBRAW_SUCCESS ? libraw_strerror(e) : "LibRaw developed no 16-bit RGB picture");
            if (img) LibRaw::dcraw_clear_mem(img);
            return false;
        }
        if (mHave >= 0 && (img->width != mDeep.width || img->height != mDeep.height))
        {
            mWhy = file + " is " + std::to_string(img->width) + "×" + std::to_string(img->height) + ", not the sequence's size";
            LibRaw::dcraw_clear_mem(img);
            return false;
        }
        mDeep.allocate16(img->width, img->height, 65535);
        const uint16_t *src = reinterpret_cast<const uint16_t *>(img->data);
        for (size_t i = 0, n = (size_t)img->width * img->height; i < n; ++i)
        {
            mDeep.rgba16[i * 4 + 0] = src[i * 3 + 0];
            mDeep.rgba16[i * 4 + 1] = src[i * 3 + 1];
            mDeep.rgba16[i * 4 + 2] = src[i * 3 + 2];
        }
        LibRaw::dcraw_clear_mem(img);
        mHave = frame;
        return true;
#endif
    }

    bool FrameSourceDng::frameAtDeep(long long frame, interstellar::Raster &out)
    {
        if (!develop(frame)) return false;
        out = mDeep;
        return true;
    }

    bool FrameSourceDng::frameAt(long long frame, interstellar::Raster &out)
    {
        if (!develop(frame)) return false;
        interstellar::toShallow(mDeep, out);
        return true;
    }
}
}
