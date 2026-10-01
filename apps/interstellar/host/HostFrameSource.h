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
#include <memory>

namespace arstro
{
namespace interstellar_host
{
    class HostFrameSource : public interstellar::IFrameSource
    {
    public:
        HostFrameSource();
        ~HostFrameSource() override;
        bool open(const std::string &path, Info &out) override;
        bool frameAt(long long frame, interstellar::Raster &out) override;

    private:
        std::unique_ptr<interstellar::IFrameSource> mVideo;
        interstellar::Raster mStill;
        bool mIsStill = false;
    };
}
}
