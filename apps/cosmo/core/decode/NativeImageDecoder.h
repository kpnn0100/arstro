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
        /** D-24: LibRaw announces its own phases, so the bar can move inside one image. */
        void setProgress(Progress p) override { mProgress = std::move(p); }

      private:
        Progress mProgress;

      public:

        /** True if this path looks like a camera RAW file (by extension). */
        static bool isRawExtension(const std::string &path);
        /** True if RAW decoding is compiled in (LibRaw present). */
        static bool rawSupported();
    };
}
}
