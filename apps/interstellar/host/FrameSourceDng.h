/*
 *  interstellar_host — FrameSourceDng: a CinemaDNG sequence as a video source (R-MEDIA-1).
 *
 *  `dir/name_%06d.dng` (core/Sequence.h) opens as one source: frame i is the file numbered
 *  first + i (a missing number holds the frame before it). Each frame is developed by LibRaw — the
 *  same library Cosmo develops a RAW still with — at 16 bits, the camera's white balance, its colour
 *  matrix to Rec.709 primaries and the BT.709 curve, and NO auto-brightening: an exposure that moved
 *  from frame to frame would be a flicker the camera never recorded. A log or wide-gamut working
 *  space is the source's input transform's business (R-COLOR-2), as for any other footage.
 *
 *  The rate and the start timecode are CinemaDNG's own tags in the first frame (FrameRate 51044,
 *  TimeCodes 51043); a sequence without them is 24 fps with no timecode (said in DR-MEDIA-1).
 *
 *  Without LibRaw in the build, open() fails and `why()` says so.
 */
#pragma once
#include "FrameSource.h"
#include "Sequence.h"
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar_host
{
    /** FrameRate and TimeCodes from a DNG's IFD0; false when it is not a readable TIFF. */
    bool readCinemaDngTags(const std::string &file, double &fps, std::string &timecode);

    class FrameSourceDng : public interstellar::IFrameSource
    {
    public:
        FrameSourceDng();
        ~FrameSourceDng() override;
        bool open(const std::string &pattern, Info &out) override;
        bool frameAt(long long frame, interstellar::Raster &out) override;
        bool frameAtDeep(long long frame, interstellar::Raster &out) override;
        const std::string &why() const { return mWhy; }

    private:
        bool develop(long long frame);   // into mDeep, once per frame
        struct Impl;
        std::unique_ptr<Impl> mImpl;
        interstellar::seq::Pattern mPattern;
        std::vector<long long> mNumbers;
        long long mFrames = 0;
        long long mHave = -1;
        interstellar::Raster mDeep;
        std::string mWhy;
    };
}
}
