#include "HostFrameSource.h"
#include "FrameSelector.h"
#include "FrameSourceFFmpeg.h"
#include "core/decode/NativeImageDecoder.h"

namespace arstro
{
namespace interstellar_host
{
    HostFrameSource::HostFrameSource() = default;
    HostFrameSource::~HostFrameSource() = default;

    bool HostFrameSource::open(const std::string &path, Info &out)
    {
        mVideo.reset();
        mStill = interstellar::Raster{};
        mIsStill = !interstellar::looksLikeVideo(path);
        if (!mIsStill)
        {
            mVideo.reset(new FrameSourceFFmpeg());
            return mVideo->open(path, out) && out.valid();
        }
        cosmo::NativeImageDecoder dec;
        cosmo::DecodedImage d = dec.decodeFile(path, cosmo::Fidelity::Full);
        if (d.width <= 0 || d.height <= 0 || d.rgba.size() < (size_t)d.width * d.height * 4) return false;
        mStill.rgba = std::move(d.rgba);
        mStill.width = d.width;
        mStill.height = d.height;
        out.width = d.width;
        out.height = d.height;
        out.fps = 0;      // a still has no rate; the timeline holds it for the clip's length
        out.frames = 1;
        return true;
    }

    bool HostFrameSource::frameAt(long long frame, interstellar::Raster &out)
    {
        if (mVideo) return mVideo->frameAt(frame, out);
        if (mStill.empty()) return false;
        (void)frame;      // every frame of a still is frame 0
        out.width = mStill.width;
        out.height = mStill.height;
        out.rgba.assign(mStill.rgba.begin(), mStill.rgba.end());
        out.rgba16.clear();
        return true;
    }

    bool HostFrameSource::frameAtDeep(long long frame, interstellar::Raster &out)
    {
        if (mVideo) return mVideo->frameAtDeep(frame, out);
        return IFrameSource::frameAtDeep(frame, out);
    }
}
}
