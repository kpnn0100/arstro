#include "HostFrameSource.h"
#include "FrameSelector.h"
#include "FrameSourceDng.h"
#include "FrameSourceFFmpeg.h"
#include "core/decode/NativeImageDecoder.h"
#include <map>
#include <mutex>

namespace arstro
{
namespace interstellar_host
{
    namespace
    {
        std::mutex gVendorMu;
        std::map<std::string, VendorFactory> &vendors()
        {
            static std::map<std::string, VendorFactory> m;
            return m;
        }
    }

    void registerVendorDecoder(const std::string &ext, VendorFactory make)
    {
        std::lock_guard<std::mutex> l(gVendorMu);
        vendors()[interstellar::seq::lower(ext)] = std::move(make);
    }

    bool vendorDecoderInstalled(const std::string &ext)
    {
        std::lock_guard<std::mutex> l(gVendorMu);
        return vendors().count(interstellar::seq::lower(ext)) > 0;
    }

    std::unique_ptr<interstellar::IFrameSource> makeVideoSource(const std::string &file, std::string *why)
    {
        if (interstellar::seq::isSequence(file)) return std::unique_ptr<interstellar::IFrameSource>(new FrameSourceDng());
        if (const interstellar::VendorRaw *v = interstellar::vendorRawFor(file))
        {
            VendorFactory make;
            {
                std::lock_guard<std::mutex> l(gVendorMu);
                const auto it = vendors().find(v->ext);
                if (it != vendors().end()) make = it->second;
            }
            if (make) return make();
            if (why) *why = std::string(v->format) + " needs " + v->sdk + ", which is licensed per user and not in this build";
            return nullptr;
        }
        return std::unique_ptr<interstellar::IFrameSource>(new FrameSourceFFmpeg());
    }

    HostFrameSource::HostFrameSource() = default;
    HostFrameSource::~HostFrameSource() = default;

    bool HostFrameSource::open(const std::string &path, Info &out)
    {
        mVideo.reset();
        mStill = interstellar::Raster{};
        mIsStill = !interstellar::looksLikeVideo(path);
        if (!mIsStill)
        {
            mVideo = makeVideoSource(path);
            return mVideo && mVideo->open(path, out) && out.valid();
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
