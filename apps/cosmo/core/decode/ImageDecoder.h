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

    struct IImageDecoder
    {
        virtual ~IImageDecoder() = default;
        virtual DecodedImage decodeFile(const std::string &path) = 0;

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
    };
}
}
