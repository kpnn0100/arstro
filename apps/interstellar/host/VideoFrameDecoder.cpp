#include "VideoFrameDecoder.h"
#include "FrameSourceFFmpeg.h"
#include "core/decode/NativeImageDecoder.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace arstro
{
namespace interstellar_host
{
    void splitFrameSelector(const std::string &spec, std::string &path, double &seconds)
    {
        seconds = 0.0;
        const auto hash = spec.rfind("#t=");
        if (hash == std::string::npos) { path = spec; return; }
        path = spec.substr(0, hash);
        seconds = std::atof(spec.c_str() + hash + 3);
        if (!std::isfinite(seconds) || seconds < 0) seconds = 0.0;
    }

    std::string joinFrameSelector(const std::string &path, double seconds)
    {
        if (seconds <= 0.0) return path;
        char buf[32];
        std::snprintf(buf, sizeof buf, "#t=%.3f", seconds);
        return path + buf;
    }

    bool looksLikeVideo(const std::string &path)
    {
        std::string file = path;
        const auto hash = file.rfind("#t=");
        if (hash != std::string::npos) file = file.substr(0, hash);
        const auto dot = file.find_last_of('.');
        if (dot == std::string::npos) return false;
        std::string e = file.substr(dot + 1);
        for (char &c : e) c = (char)std::tolower((unsigned char)c);
        static const char *kVideo[] = {"mp4", "mov", "mkv", "m4v", "avi", "mxf", "webm", "mts",
                                       "m2ts", "wmv", "flv", "r3d", "braw"};
        for (const char *v : kVideo)
            if (e == v) return true;
        return false;
    }

    VideoFrameDecoder::VideoFrameDecoder() : mStills(new cosmo::NativeImageDecoder()) {}
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

        FrameSourceFFmpeg src;
        interstellar::IFrameSource::Info info;
        cosmo::DecodedImage out;
        if (!src.open(file, info) || !info.valid()) return out;   // empty = "failed", which Cosmo
                                                                   // reads as missing, not as a stall

        const long long frame = (long long)std::llround(seconds * (info.fps > 0 ? info.fps : 24.0));
        interstellar::Raster r;
        if (!src.frameAt(frame, r) || r.empty()) return out;

        out.rgba = std::move(r.rgba);
        out.width = r.width;
        out.height = r.height;
        // The NAME Cosmo shows. The file's own, without the selector: the selector is an address,
        // not something a person should have to read in a filmstrip cell.
        const auto slash = file.find_last_of("/\\");
        out.name = slash == std::string::npos ? file : file.substr(slash + 1);
        return out;
    }
}
}
