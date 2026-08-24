/*
 *  Cosmo by arstro — image decode SEAM. Decoding lives in the app layer (not the
 *  engine core, which stays codec-free / WASM-friendly). A decoder turns a file
 *  into straight RGBA8 bytes that both the EditEngine and an ImageView consume.
 *
 *  Native: NativeImageDecoder (GdkPixbuf for JPEG/PNG/TIFF, LibRaw for RAW).
 *  Android: AndroidImageDecoder (stb_image for JPEG/PNG/…, LibRaw for RAW).
 *  Web: the browser decodes in JS and feeds bytes through CosmoApp::openImage —
 *  no IImageDecoder is used on the web.
 */
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo
{
    struct DecodedImage
    {
        std::vector<uint8_t> rgba;  // straight RGBA8, row-major top-down
        int width = 0;
        int height = 0;
        std::string name;
        bool ok() const { return width > 0 && height > 0 && (int)rgba.size() == width * height * 4; }
    };

    /** How good the pixels have to be (D-24 / R-LOADPERF).
     *
     *  `Preview` is for pixels that will be downscaled to `previewEdge` before anyone sees
     *  them — which is every pixel a project LOAD produces. `Full` is for pixels that will
     *  be written to a file. The difference is worth ~7x on a RAW: 90% of an 8.5 s decode is
     *  one `dcraw_process()` call running the highest-quality demosaic over 26 megapixels
     *  that are then thrown away to draw a 1600 px preview.
     *
     *  The dimensions are IDENTICAL either way — that is the whole reason this is a demosaic
     *  quality switch and not `half_size`. Crop rectangles, normalised mask geometry and
     *  every slot's coordinates stay valid, so a `Full` re-decode drops straight into the
     *  same slot, which is what makes export-time re-decoding a contained change rather than
     *  a coordinate migration. */
    enum class Fidelity
    {
        Preview,
        Full
    };

    struct IImageDecoder
    {
        virtual ~IImageDecoder() = default;
        virtual DecodedImage decodeFile(const std::string &path) = 0;

        /** As `decodeFile`, at the requested fidelity. The default forwards to the
         *  full-quality path, so a decoder that has no cheaper mode costs nothing to keep
         *  and no implementor is broken by this existing. */
        virtual DecodedImage decodeFile(const std::string &path, Fidelity f)
        {
            (void)f;
            return decodeFile(path);
        }

        /** Sub-image progress: `fraction` in 0..1 and a short stage name. **Called on the
         *  decoding thread**, so an implementation of this must not touch shared state
         *  without its own protection — ProjectLoader buffers it under a mutex.
         *
         *  This exists because one RAF decode is ~8.3 s and 90% of that is a single
         *  `dcraw_process()` call (D-24): reporting only per-entry left a photographer with
         *  nine seconds of a bar that could not move, and there is nothing wrong with the
         *  bar. Optional by design — a decoder with no progress to give overrides nothing
         *  and the caller sees entry-level progress as before. */
        using Progress = std::function<void(double fraction, const char *stage)>;
        virtual void setProgress(Progress) {}

        /** A SMALL image for a thumbnail or cover, no larger than `maxEdge` on its long side.
         *
         *  This is a different job from `decodeFile`, not a convenience wrapper on it. A RAW
         *  file carries an embedded JPEG preview, and reading it costs **6.6 ms against 8072 ms**
         *  for the full decode on a 26 MB X-Trans RAF — 1200x — because the full path runs a
         *  demosaic whose 26 megapixels are then thrown away to draw a 480 px card.
         *
         *  The default implementation is the honest fallback: decode normally. A decoder that
         *  has a cheap preview overrides this; one that does not costs nothing to keep. */
        virtual DecodedImage decodeThumb(const std::string &path, int maxEdge)
        {
            (void)maxEdge;
            return decodeFile(path);
        }
    };
}
}
