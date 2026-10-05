/*
 *  interstellar_host — HostFrameSource: the timeline's view of a rack source's pixels.
 *
 *  A video opens through FFmpeg; a STILL opens through Cosmo's own `NativeImageDecoder` as a
 *  one-frame source (R-VOL-6: a photo is the T=1 case). The still path matters for R-RENDER-5: a
 *  still exported from Interstellar must match Cosmo's export byte for byte, and that is only
 *  possible if both decoded the photo with the same decoder.
 */
#pragma once
#include "FrameSource.h"
#include <functional>
#include <memory>
#include <string>

namespace arstro
{
namespace interstellar_host
{
    /** R-MEDIA-1: the decoder for a MOVING source, not yet opened — a CinemaDNG sequence through
     *  LibRaw, a vendor RAW through its SDK's decoder when one is registered, else FFmpeg. Null, with
     *  `why` naming what is missing, when nothing in this build can decode it. */
    std::unique_ptr<interstellar::IFrameSource> makeVideoSource(const std::string &file, std::string *why = nullptr);
    /** R-MEDIA-1: the seam an SDK build fills — `ext` ("ari", "r3d", "braw") → its decoder. */
    using VendorFactory = std::function<std::unique_ptr<interstellar::IFrameSource>()>;
    void registerVendorDecoder(const std::string &ext, VendorFactory make);
    bool vendorDecoderInstalled(const std::string &ext);

    class HostFrameSource : public interstellar::IFrameSource
    {
    public:
        HostFrameSource();
        ~HostFrameSource() override;
        bool open(const std::string &path, Info &out) override;
        bool frameAt(long long frame, interstellar::Raster &out) override;
        /** A video's own deep decode (R-COLOR-1); a still is widened from its 8-bit pixels. */
        bool frameAtDeep(long long frame, interstellar::Raster &out) override;

    private:
        std::unique_ptr<interstellar::IFrameSource> mVideo;
        interstellar::Raster mStill;
        bool mIsStill = false;
    };
}
}
