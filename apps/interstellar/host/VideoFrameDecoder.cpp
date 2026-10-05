#include "VideoFrameDecoder.h"
#include "FrameSourceFFmpeg.h"
#include "HostFrameSource.h"
#include "core/decode/NativeImageDecoder.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace arstro
{
namespace interstellar_host
{
    // The selector helpers live in the core (FrameSelector.h) — the service binds `.cmp` paths with
    // them and must not need a codec to do it. These names stay for the host's existing callers.
    void splitFrameSelector(const std::string &spec, std::string &path, double &seconds)
    {
        interstellar::splitFrameSelector(spec, path, seconds);
    }
    std::string joinFrameSelector(const std::string &path, double seconds)
    {
        return interstellar::joinFrameSelector(path, seconds);
    }
    bool looksLikeVideo(const std::string &path) { return interstellar::looksLikeVideo(path); }

    VideoFrameDecoder::VideoFrameDecoder(std::shared_ptr<const interstellar::FrameSelector> selector)
        : mStills(new cosmo::NativeImageDecoder()), mSelector(std::move(selector))
    {
    }
    VideoFrameDecoder::~VideoFrameDecoder() = default;

    cosmo::DecodedImage VideoFrameDecoder::decodeFile(const std::string &path)
    {
        return decodeFile(path, cosmo::Fidelity::Full);
    }

    cosmo::DecodedImage VideoFrameDecoder::decodeFile(const std::string &path, cosmo::Fidelity f)
    {
        if (!looksLikeVideo(path)) return mStills->decodeFile(path, f);

        std::string file;
        double seconds = 0;
        splitFrameSelector(path, file, seconds);
        // The .isp's `#rackobj frame=` wins over the selector baked into the stored path: choosing a
        // new reference frame changes neither a parameter nor the Cosmo node (R-RACK-3).
        if (mSelector) seconds = mSelector->frameFor(path, seconds);

        // R-MEDIA-1: the same decoder the timeline uses — FFmpeg, a CinemaDNG sequence, a vendor SDK's
        std::unique_ptr<interstellar::IFrameSource> src = makeVideoSource(file);
        interstellar::IFrameSource::Info info;
        cosmo::DecodedImage out;
        if (!src || !src->open(file, info) || !info.valid()) return out;   // empty = "failed", which Cosmo
                                                                            // reads as missing, not as a stall

        const long long frame = (long long)std::llround(seconds * (info.fps > 0 ? info.fps : 24.0));
        interstellar::Raster r;
        if (!src->frameAt(frame, r) || r.empty()) return out;

        out.rgba = std::move(r.rgba);
        out.width = r.width;
        out.height = r.height;
        // The NAME Cosmo shows. The file's own, without the selector: the selector is an address,
        // not something a person should have to read in a filmstrip cell.
        const auto slash = file.find_last_of("/\\");
        out.name = interstellar::seq::isSequence(file) ? interstellar::seq::stem(file) : slash == std::string::npos ? file : file.substr(slash + 1);
        return out;
    }
}
}
