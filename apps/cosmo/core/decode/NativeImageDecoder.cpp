#include "NativeImageDecoder.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <algorithm>
#include <cctype>

#ifdef COSMO_HAVE_LIBRAW
#include <libraw/libraw.h>
#endif

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
            std::transform(e.begin(), e.end(), e.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            return e;
        }

        std::string baseName(const std::string &path)
        {
            auto slash = path.find_last_of("/\\");
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }

        // GdkPixbuf path: JPEG/PNG/TIFF/BMP/GIF/... -> straight RGBA8.
        DecodedImage decodePixbuf(const std::string &path)
        {
            DecodedImage out;
            GError *err = nullptr;
            GdkPixbuf *pb = gdk_pixbuf_new_from_file(path.c_str(), &err);
            if (!pb)
            {
                if (err) g_error_free(err);
                return out;
            }
            const int w = gdk_pixbuf_get_width(pb);
            const int h = gdk_pixbuf_get_height(pb);
            const int nch = gdk_pixbuf_get_n_channels(pb);
            const int stride = gdk_pixbuf_get_rowstride(pb);
            const guchar *src = gdk_pixbuf_get_pixels(pb);
            out.width = w; out.height = h;
            out.rgba.resize((size_t)w * h * 4);
            for (int y = 0; y < h; ++y)
            {
                const guchar *s = src + (size_t)y * stride;
                uint8_t *d = out.rgba.data() + (size_t)y * w * 4;
                for (int x = 0; x < w; ++x)
                {
                    const guchar *sp = s + x * nch;
                    uint8_t *dp = d + x * 4;
                    dp[0] = sp[0];
                    dp[1] = nch >= 2 ? sp[1] : sp[0];
                    dp[2] = nch >= 3 ? sp[2] : sp[0];
                    dp[3] = nch >= 4 ? sp[3] : 255;
                }
            }
            g_object_unref(pb);
            return out;
        }

#ifdef COSMO_HAVE_LIBRAW
        /** LibRaw's own phase flags, mapped onto a monotonic 0..1 with the weights MEASURED on
         *  this project's files rather than guessed (D-24): on a 26 MB X-Trans RAF, unpack is
         *  ~0.7 s and `dcraw_process` ~7.5 s of an 8.3 s decode, so interpolation owns most of
         *  the bar. A stage cosmo does not recognise moves nothing, which keeps it monotonic. */
        double rawStageFraction(enum LibRaw_progress stage, const char *&nameOut)
        {
            switch (stage)
            {
                case LIBRAW_PROGRESS_OPEN:            nameOut = "opening";       return 0.01;
                case LIBRAW_PROGRESS_IDENTIFY:        nameOut = "identifying";   return 0.03;
                case LIBRAW_PROGRESS_LOAD_RAW:        nameOut = "reading raw";   return 0.10;
                case LIBRAW_PROGRESS_RAW2_IMAGE:      nameOut = "unpacking";     return 0.14;
                case LIBRAW_PROGRESS_PRE_INTERPOLATE: nameOut = "preparing";     return 0.18;
                case LIBRAW_PROGRESS_INTERPOLATE:     nameOut = "demosaicing";   return 0.22;
                case LIBRAW_PROGRESS_CONVERT_RGB:     nameOut = "colour";        return 0.92;
                case LIBRAW_PROGRESS_STRETCH:         nameOut = "finishing";     return 0.96;
                default:                              nameOut = nullptr;         return -1.0;
            }
        }

        struct RawProgressCtx
        {
            IImageDecoder::Progress *sink = nullptr;
            double last = 0.0;
        };

        /** LibRaw calls this from inside the decode, and for INTERPOLATE it calls it REPEATEDLY
         *  with iteration/expected — which is the only genuinely fine-grained signal in the
         *  whole pipeline, and it covers the 90% of the time that used to be invisible. */
        int rawProgressCb(void *data, enum LibRaw_progress stage, int done, int expected)
        {
            auto *ctx = static_cast<RawProgressCtx *>(data);
            if (!ctx || !ctx->sink || !*ctx->sink) return 0;
            const char *name = nullptr;
            const double base = rawStageFraction(stage, name);
            if (base < 0) return 0;   // a stage we do not model moves nothing
            double f = base;
            if (stage == LIBRAW_PROGRESS_INTERPOLATE && expected > 0)
            {
                // 0.22 -> 0.92 spread across the demosaic's own iterations: this is the part
                // that took 7.5 of 8.3 seconds with nothing to show for it.
                const double t = (double)done / (double)expected;
                f = 0.22 + 0.70 * (t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t));
            }
            if (f < ctx->last) f = ctx->last;   // monotonic, whatever order LibRaw reports in
            ctx->last = f;
            (*ctx->sink)(f, name);
            return 0;   // non-zero would ask LibRaw to abort
        }

        DecodedImage decodeRaw(const std::string &path, IImageDecoder::Progress *sink)
        {
            DecodedImage out;
            LibRaw raw;
            RawProgressCtx ctx{sink, 0.0};
            if (sink && *sink) raw.set_progress_handler(rawProgressCb, &ctx);
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
    }

    bool NativeImageDecoder::isRawExtension(const std::string &path)
    {
        static const char *kRaw[] = {"rw2", "arw", "cr2", "cr3", "nef", "dng",
                                     "orf", "raf", "pef", "srw", "rwl", "raw"};
        const std::string e = lowerExt(path);
        for (const char *r : kRaw)
            if (e == r) return true;
        return false;
    }

    bool NativeImageDecoder::rawSupported()
    {
#ifdef COSMO_HAVE_LIBRAW
        return true;
#else
        return false;
#endif
    }

    DecodedImage NativeImageDecoder::decodeFile(const std::string &path)
    {
        DecodedImage out;
#ifdef COSMO_HAVE_LIBRAW
        if (isRawExtension(path))
            out = decodeRaw(path, &mProgress);
        else
            out = decodePixbuf(path);
#else
        out = decodePixbuf(path);  // RAW (without LibRaw) will simply fail to decode
#endif
        out.name = baseName(path);
        return out;
    }
}
}
