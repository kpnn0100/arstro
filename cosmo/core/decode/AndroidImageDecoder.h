/*
 *  Cosmo by arstro — Android image decoder. JPEG/PNG/TGA/BMP/etc. via stb_image
 *  (vendored, dependency-free, API-level-agnostic); RAW (.rw2/.arw/.cr2/.nef/.dng…)
 *  via LibRaw when built with COSMO_HAVE_LIBRAW. Always yields straight RGBA8,
 *  top-down — the same DecodedImage contract as NativeImageDecoder, so everything
 *  downstream (openImage -> addImage -> fromEncodedBytes) is unchanged.
 */
#pragma once
#include "ImageDecoder.h"

namespace arstro
{
namespace cosmo
{
    class AndroidImageDecoder : public IImageDecoder
    {
    public:
        DecodedImage decodeFile(const std::string &path) override;

        /** True if this path looks like a camera RAW file (by extension). */
        static bool isRawExtension(const std::string &path);
        /** True if RAW decoding is compiled in (LibRaw present). */
        static bool rawSupported();
    };
}
}
