/*
 *  Cosmo by arstro — native image decoder. JPEG/PNG/TIFF/etc. via GdkPixbuf
 *  (ships with GTK3); RAW (.rw2/.arw/.cr2/.nef/.dng) via LibRaw when built with
 *  COSMO_HAVE_LIBRAW. Always yields straight RGBA8.
 */
#pragma once
#include "ImageDecoder.h"

namespace arstro
{
namespace cosmo
{
    class NativeImageDecoder : public IImageDecoder
    {
    public:
        DecodedImage decodeFile(const std::string &path) override;
        /** The embedded preview for RAW, a size-limited GdkPixbuf load otherwise. */
        DecodedImage decodeThumb(const std::string &path, int maxEdge) override;
        /** D-24: LibRaw announces its own phases, so the bar can move inside one image. */
        void setProgress(Progress p) override { mProgress = std::move(p); }

      private:
        Progress mProgress;

      public:

        /** True if this path looks like a camera RAW file (by extension). */
        static bool isRawExtension(const std::string &path);
        /** True if RAW decoding is compiled in (LibRaw present). */
        static bool rawSupported();

        /** Rotate/mirror decoded pixels by a LibRaw/dcraw `flip` code (0..7) — the SAME
         *  transform `dcraw_process` applies to a full decode (`flip & 4` transposes,
         *  `& 2` mirrors rows, `& 1` mirrors columns), so a camera's embedded preview can be
         *  brought into the orientation the photo itself has (R-THUMB-1). Public because it is
         *  a pure pixel transform with nothing to do with files, and it is unit-tested as one. */
        static void applyFlip(DecodedImage &img, int flip);
    };
}
}
