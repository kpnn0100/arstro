#include "AndroidImageDecoder.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_GIF
#include "stb_image.h"

#ifdef COSMO_HAVE_LIBRAW
#include <libraw/libraw.h>
#endif

#include <algorithm>
#include <cctype>

namespace arstro
{
namespace cosmo
{
namespace
{
    std::string lowerExt(const std::string &path)
    {
        auto dot = path.find_last_of('.');
        if (dot == std::string::npos) return "";
        std::string e = path.substr(dot + 1);
        std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return e;
    }

    std::string baseName(const std::string &path)
    {
        auto slash = path.find_last_of("/\\");
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    // Common formats via stb_image, forced to 4-channel straight RGBA8 (top-down).
    DecodedImage decodeStb(const std::string &path)
    {
        DecodedImage out;
        int w = 0, h = 0, n = 0;
        unsigned char *d = stbi_load(path.c_str(), &w, &h, &n, 4);
        if (!d) return out;
        out.width = w;
        out.height = h;
        out.rgba.assign(d, d + (size_t)w * h * 4);
        stbi_image_free(d);
        return out;
    }

#ifdef COSMO_HAVE_LIBRAW
    // Camera RAW via LibRaw — same path as NativeImageDecoder: unpack -> dcraw_process
    // -> 8-bit RGB bitmap -> RGBA.
    DecodedImage decodeRaw(const std::string &path)
    {
        DecodedImage out;
        LibRaw raw;
        if (raw.open_file(path.c_str()) != LIBRAW_SUCCESS) return out;
        if (raw.unpack() != LIBRAW_SUCCESS) return out;
        if (raw.dcraw_process() != LIBRAW_SUCCESS) return out;
        int code = 0;
        libraw_processed_image_t *img = raw.dcraw_make_mem_image(&code);
        if (!img) return out;
        if (img->type == LIBRAW_IMAGE_BITMAP && img->bits == 8 && img->colors == 3)
        {
            out.width = img->width;
            out.height = img->height;
            out.rgba.resize((size_t)out.width * out.height * 4);
            const unsigned char *s = img->data;
            for (size_t i = 0; i < (size_t)out.width * out.height; ++i)
            {
                out.rgba[i * 4 + 0] = s[i * 3 + 0];
                out.rgba[i * 4 + 1] = s[i * 3 + 1];
                out.rgba[i * 4 + 2] = s[i * 3 + 2];
                out.rgba[i * 4 + 3] = 255;
            }
        }
        LibRaw::dcraw_clear_mem(img);
        return out;
    }
#endif
}  // namespace

    bool AndroidImageDecoder::isRawExtension(const std::string &path)
    {
        static const char *kRaw[] = {"rw2", "arw", "cr2", "cr3", "nef", "dng",
                                     "orf", "raf", "pef", "srw", "rwl", "raw"};
        const std::string e = lowerExt(path);
        for (const char *r : kRaw)
            if (e == r) return true;
        return false;
    }

    bool AndroidImageDecoder::rawSupported()
    {
#ifdef COSMO_HAVE_LIBRAW
        return true;
#else
        return false;
#endif
    }

    DecodedImage AndroidImageDecoder::decodeFile(const std::string &path)
    {
        DecodedImage out;
#ifdef COSMO_HAVE_LIBRAW
        if (isRawExtension(path))
            out = decodeRaw(path);
        else
            out = decodeStb(path);
#else
        out = decodeStb(path);  // RAW (without LibRaw) will simply fail to decode
#endif
        out.name = baseName(path);
        return out;
    }
}
}
