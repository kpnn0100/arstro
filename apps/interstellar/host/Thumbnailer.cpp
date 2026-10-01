#include "Thumbnailer.h"
#include "HostFrameSource.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace interstellar_host
{
    void boxDownscale(const interstellar::Raster &in, int edge, interstellar::Raster &out)
    {
        const int longEdge = std::max(in.width, in.height);
        if (edge <= 0 || longEdge <= edge) { out = in; return; }
        const double s = (double)edge / longEdge;
        const int ow = std::max(1, (int)std::lround(in.width * s));
        const int oh = std::max(1, (int)std::lround(in.height * s));
        out.allocate(ow, oh);
        for (int y = 0; y < oh; ++y)
        {
            const int y0 = y * in.height / oh, y1 = std::max(y0 + 1, (y + 1) * in.height / oh);
            for (int x = 0; x < ow; ++x)
            {
                const int x0 = x * in.width / ow, x1 = std::max(x0 + 1, (x + 1) * in.width / ow);
                unsigned sum[4] = {0, 0, 0, 0};
                for (int yy = y0; yy < y1; ++yy)
                {
                    const uint8_t *p = in.rgba.data() + ((size_t)yy * in.width + x0) * 4;
                    for (int xx = x0; xx < x1; ++xx, p += 4)
                        for (int c = 0; c < 4; ++c) sum[c] += p[c];
                }
                const unsigned n = (unsigned)((y1 - y0) * (x1 - x0));
                uint8_t *o = out.rgba.data() + ((size_t)y * ow + x) * 4;
                for (int c = 0; c < 4; ++c) o[c] = (uint8_t)((sum[c] + n / 2) / n);
            }
        }
    }

    bool Thumbnailer::get(const std::string &mediaPath, double t, int edge, interstellar::Raster &out)
    {
        const auto key = std::make_tuple(mediaPath, (long long)std::llround(t * 1000.0), edge);
        const auto it = mCache.find(key);
        if (it != mCache.end()) { out = it->second; return !out.empty(); }
        interstellar::Raster full, small;
        HostFrameSource src;
        interstellar::IFrameSource::Info info;
        bool ok = src.open(mediaPath, info) && info.valid();
        if (ok)
        {
            const long long frame = info.frames <= 1 ? 0 : (long long)std::llround(t * (info.fps > 0 ? info.fps : 24.0));
            ok = src.frameAt(std::clamp<long long>(frame, 0, std::max<long long>(0, info.frames - 1)), full);
        }
        if (ok) boxDownscale(full, edge, small);
        mCache[key] = small;   // a miss is cached too: a missing file is not re-probed every frame
        out = small;
        return ok && !small.empty();
    }
}
}
