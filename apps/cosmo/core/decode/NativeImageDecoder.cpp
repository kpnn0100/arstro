#include "NativeImageDecoder.h"
#include "Exif.h"   // R-INFO: the JPEG tag reader
#include <cstring>
#include <ctime>
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
        /** GdkPixbuf -> straight RGBA8, shared by every path below. */
        DecodedImage fromPixbuf(GdkPixbuf *pb)
        {
            DecodedImage out;
            if (!pb) return out;
            const int w = gdk_pixbuf_get_width(pb);
            const int h = gdk_pixbuf_get_height(pb);
            const int nch = gdk_pixbuf_get_n_channels(pb);
            const int stride = gdk_pixbuf_get_rowstride(pb);
            const guchar *src = gdk_pixbuf_get_pixels(pb);
            out.width = w; out.height = h;
            out.rgba.resize((size_t)w * h * 4);
            for (int y = 0; y < h; ++y)
            {
                const guchar *sp = src + (size_t)y * stride;
                uint8_t *dp = out.rgba.data() + (size_t)y * w * 4;
                for (int x = 0; x < w; ++x)
                {
                    dp[x * 4 + 0] = sp[x * nch + 0];
                    dp[x * 4 + 1] = sp[x * nch + 1];
                    dp[x * 4 + 2] = sp[x * nch + 2];
                    dp[x * 4 + 3] = nch >= 4 ? sp[x * nch + 3] : 255;
                }
            }
            return out;
        }

        /** Decode JPEG/PNG/… bytes already in memory, scaled down DURING the decode so a
         *  4.7 MB embedded preview never becomes a 26-megapixel buffer we immediately shrink. */
        DecodedImage fromMemoryScaled(const unsigned char *data, size_t bytes, int maxEdge)
        {
            DecodedImage out;
            GdkPixbufLoader *ld = gdk_pixbuf_loader_new();
            if (!ld) return out;
            if (maxEdge > 0)
            {
                // size-prepared fires once the header is read: ask the loader for a smaller
                // image up front. This is the difference between a fast cover and a slow one.
                g_signal_connect(ld, "size-prepared", G_CALLBACK(+[](GdkPixbufLoader *l, gint w, gint h,
                                                                    gpointer user) {
                    const int cap = GPOINTER_TO_INT(user);
                    if (w <= 0 || h <= 0) return;
                    const int longEdge = w > h ? w : h;
                    if (longEdge <= cap) return;
                    const double k = (double)cap / (double)longEdge;
                    gdk_pixbuf_loader_set_size(l, (int)(w * k + 0.5), (int)(h * k + 0.5));
                }), GINT_TO_POINTER(maxEdge));
            }
            GError *err = nullptr;
            if (gdk_pixbuf_loader_write(ld, data, bytes, &err))
            {
                gdk_pixbuf_loader_close(ld, nullptr);
                out = fromPixbuf(gdk_pixbuf_loader_get_pixbuf(ld));
            }
            else
            {
                if (err) g_error_free(err);
                gdk_pixbuf_loader_close(ld, nullptr);
            }
            g_object_unref(ld);
            return out;
        }

        DecodedImage decodePixbufScaled(const std::string &path, int maxEdge)
        {
            GError *err = nullptr;
            GdkPixbuf *pb = maxEdge > 0
                                ? gdk_pixbuf_new_from_file_at_scale(path.c_str(), maxEdge, maxEdge, TRUE, &err)
                                : gdk_pixbuf_new_from_file(path.c_str(), &err);
            if (!pb) { if (err) g_error_free(err); return DecodedImage{}; }
            DecodedImage out = fromPixbuf(pb);
            g_object_unref(pb);
            return out;
        }

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

        DecodedImage decodeRaw(const std::string &path, IImageDecoder::Progress *sink,
                               Fidelity fidelity = Fidelity::Full)
        {
            DecodedImage out;
            LibRaw raw;
            RawProgressCtx ctx{sink, 0.0};
            if (sink && *sink) raw.set_progress_handler(rawProgressCb, &ctx);
            if (raw.open_file(path.c_str()) != LIBRAW_SUCCESS) return out;
            // D-24: the demosaic is the decode. Measured per phase on a 4170x6246 X-Trans
            // RAF: open 0 ms, unpack 713 ms, **process 7688 ms**, make_mem 107 ms. Asking for
            // bilinear (`user_qual = 0`) takes that 7688 down to 337 — and leaves the
            // DIMENSIONS untouched, which `half_size` would not, so the pixels drop straight
            // into the same slot and every crop rect and mask coordinate stays valid.
            //
            // A load only ever produces pixels that are downscaled to previewEdge before
            // anyone sees them, so this costs nothing anyone can look at. Export asks for
            // Fidelity::Full and re-decodes properly, which the R-MEM-2 architecture already
            // does anyway: the load keeps no full-resolution source, so `renderFull` was
            // going back to the file regardless.
            if (fidelity == Fidelity::Preview) raw.imgdata.params.user_qual = 0;
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

    void NativeImageDecoder::applyFlip(DecodedImage &img, int flip)
    {
        if (flip <= 0 || !img.ok()) return;
        // Same index math as LibRaw's flip_index (src/write/file_write.cpp), so a preview turned
        // here lands in exactly the orientation dcraw_process would have produced.
        const int sw = img.width, sh = img.height;
        const bool quarter = (flip & 4) != 0;      // transpose -> the dimensions swap
        const int dw = quarter ? sh : sw, dh = quarter ? sw : sh;
        std::vector<uint8_t> out((size_t)dw * dh * 4);
        for (int r = 0; r < dh; ++r)
            for (int c = 0; c < dw; ++c)
            {
                int sr = r, sc = c;
                if (quarter) std::swap(sr, sc);
                if (flip & 2) sr = sh - 1 - sr;
                if (flip & 1) sc = sw - 1 - sc;
                const uint8_t *sp = img.rgba.data() + ((size_t)sr * sw + sc) * 4;
                uint8_t *dp = out.data() + ((size_t)r * dw + c) * 4;
                dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3];
            }
        img.rgba.swap(out);
        img.width = dw;
        img.height = dh;
    }

    DecodedImage NativeImageDecoder::decodeThumb(const std::string &path, int maxEdge)
    {
        DecodedImage out;
#ifdef COSMO_HAVE_LIBRAW
        if (isRawExtension(path))
        {
            // A RAW file already contains a JPEG preview the camera wrote. Reading it costs
            // 6.6 ms against 8072 ms for the full decode on a 26 MB X-Trans RAF — 1200x — and
            // a cover is drawn at 480 px, so the full path's 26 megapixels were being demosaiced
            // and then thrown away. This is what froze the splash for the length of a startup.
            LibRaw raw;
            if (raw.open_file(path.c_str()) == LIBRAW_SUCCESS && raw.unpack_thumb() == LIBRAW_SUCCESS)
            {
                // The RAW's orientation, and the sensor frame it is measured against. The full
                // decode gets this applied inside dcraw_process; the preview below does not, which
                // is why a portrait shot's cover used to lie on its side next to its own photo.
                const int flip = raw.imgdata.sizes.flip;
                const bool sensorLandscape = raw.imgdata.sizes.width >= raw.imgdata.sizes.height;
                int code = 0;
                if (libraw_processed_image_t *th = raw.dcraw_make_mem_thumb(&code))
                {
                    if (th->type == LIBRAW_IMAGE_JPEG)
                        out = fromMemoryScaled(th->data, th->data_size, maxEdge);
                    else if (th->type == LIBRAW_IMAGE_BITMAP && th->bits == 8 && th->colors == 3)
                    {
                        out.width = th->width; out.height = th->height;
                        out.rgba.resize((size_t)out.width * out.height * 4);
                        for (size_t i = 0; i < (size_t)out.width * out.height; ++i)
                        {
                            out.rgba[i * 4 + 0] = th->data[i * 3 + 0];
                            out.rgba[i * 4 + 1] = th->data[i * 3 + 1];
                            out.rgba[i * 4 + 2] = th->data[i * 3 + 2];
                            out.rgba[i * 4 + 3] = 255;
                        }
                    }
                    LibRaw::dcraw_clear_mem(th);
                }
                // R-THUMB-1: turn the preview the way dcraw_process would have turned the photo.
                // Some makers already store an upright preview, and turning that one again would
                // be just as wrong — a quarter turn swaps the aspect, so a preview whose aspect
                // still matches the SENSOR frame is the one that has not been turned yet. (A 180
                // degree flip swaps nothing, so there is no such signal; it is applied, which is
                // what the camera's own flag asks for.)
                if (out.ok() && flip > 0)
                {
                    const bool previewLandscape = out.width >= out.height;
                    if ((flip & 4) == 0 || previewLandscape == sensorLandscape)
                        applyFlip(out, flip);
                }
            }
            // No embedded preview (rare, but some RAWs have none): fall back to the real thing
            // rather than showing nothing. Slow, and correct — and already oriented by
            // dcraw_process, so it must NOT be flipped again.
            if (!out.ok()) out = decodeRaw(path, nullptr);
        }
        else
            out = decodePixbufScaled(path, maxEdge);
#else
        out = decodePixbufScaled(path, maxEdge);
#endif
        out.name = baseName(path);
        return out;
    }

    ImageMetadata NativeImageDecoder::readMetadata(const std::string &path)
    {
        ImageMetadata m;
#ifdef COSMO_HAVE_LIBRAW
        if (isRawExtension(path))
        {
            // `open_file` parses the maker notes and stops — no unpack, no demosaic. This is why
            // metadata is its own call: the panel must work on a photo whose pixels are evicted,
            // and a 26 MB decode to print an ISO would be absurd.
            LibRaw raw;
            if (raw.open_file(path.c_str()) == LIBRAW_SUCCESS)
            {
                const libraw_image_sizes_t &sz = raw.imgdata.sizes;
                const libraw_iparams_t &id = raw.imgdata.idata;
                const libraw_imgother_t &o = raw.imgdata.other;
                char buf[128];
                m.add("Camera make", id.make);
                m.add("Camera model", id.model);
                m.add("Lens", raw.imgdata.lens.Lens);
                if (sz.width > 0 && sz.height > 0)
                {
                    std::snprintf(buf, sizeof(buf), "%u x %u", sz.width, sz.height);
                    m.add("Dimensions", buf);
                    std::snprintf(buf, sizeof(buf), "%.1f MP",
                                  (double)sz.width * (double)sz.height / 1e6);
                    m.add("Resolution", buf);
                }
                if (o.iso_speed > 0) { std::snprintf(buf, sizeof(buf), "ISO %.0f", (double)o.iso_speed); m.add("ISO", buf); }
                if (o.shutter > 0)
                {
                    if (o.shutter >= 1.0f) std::snprintf(buf, sizeof(buf), "%.4g s", (double)o.shutter);
                    else                   std::snprintf(buf, sizeof(buf), "1/%.0f s", 1.0 / (double)o.shutter);
                    m.add("Shutter", buf);
                }
                if (o.aperture > 0) { std::snprintf(buf, sizeof(buf), "f/%.3g", (double)o.aperture); m.add("Aperture", buf); }
                if (o.focal_len > 0) { std::snprintf(buf, sizeof(buf), "%.0f mm", (double)o.focal_len); m.add("Focal length", buf); }
                if (o.timestamp > 0)
                {
                    const std::time_t tt = (std::time_t)o.timestamp;
                    std::tm tmv{};
#ifdef _WIN32
                    localtime_s(&tmv, &tt);
#else
                    localtime_r(&tt, &tmv);
#endif
                    if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv) > 0)
                        m.add("Taken", buf);
                }
                m.add("Artist", o.artist);
                if (o.desc[0]) m.add("Description", o.desc);
                // The as-shot white balance, which is the one piece of RAW metadata the engine
                // does NOT currently apply — worth showing, because a photographer comparing
                // cosmo's colour with the camera's will want to know the number exists.
                if (raw.imgdata.color.cam_mul[0] > 0 && raw.imgdata.color.cam_mul[1] > 0)
                {
                    std::snprintf(buf, sizeof(buf), "R %.3f  G %.3f  B %.3f",
                                  (double)raw.imgdata.color.cam_mul[0] / (double)raw.imgdata.color.cam_mul[1],
                                  1.0,
                                  (double)raw.imgdata.color.cam_mul[2] / (double)raw.imgdata.color.cam_mul[1]);
                    m.add("As-shot WB", buf);
                }
                raw.recycle();
                return m;
            }
        }
#endif
        // Not a RAW (or LibRaw could not open it): the Exif reader handles JPEG, and anything
        // else simply reports fewer rows — which is the honest outcome for a PNG that carries no
        // shooting data at all.
        for (auto &kv : cosmo_v2::exif::read(path)) m.add(kv.first, kv.second);
        return m;
    }

    DecodedImage NativeImageDecoder::decodeFile(const std::string &path)
    {
        return decodeFile(path, Fidelity::Full);
    }

    DecodedImage NativeImageDecoder::decodeFile(const std::string &path, Fidelity fidelity)
    {
        DecodedImage out;
#ifdef COSMO_HAVE_LIBRAW
        if (isRawExtension(path))
            out = decodeRaw(path, &mProgress, fidelity);
        else
            out = decodePixbuf(path);   // a JPEG/PNG decode has no quality knob to trade
#else
        (void)fidelity;
        out = decodePixbuf(path);  // RAW (without LibRaw) will simply fail to decode
#endif
        out.name = baseName(path);
        return out;
    }
}
}
