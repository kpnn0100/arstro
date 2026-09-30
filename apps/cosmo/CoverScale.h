/*
 *  Cosmo by arstro — CoverScale: the home screen's project covers, shrunk once in the host.
 *
 *  Box-average downscale of a decoded image to a long edge of at most `maxEdge`. The GTK host
 *  uses it for the home cards and the loading-screen centre image (so covers cost little
 *  memory), and `cosmo-cc ntwb serve` uses it for the web home's covers (R-NTWB-2, the `cover`
 *  method) - ONE function, so a cover in the browser and a cover in the window are the same
 *  pixels. Host layer: pixels never enter cosmo_core's model (R-SVC-3).
 */
#pragma once
#include "core/decode/ImageDecoder.h"
#include <algorithm>

namespace arstro
{
namespace cosmo_v2
{
    inline arstro::cosmo::DecodedImage downscaleCover(const arstro::cosmo::DecodedImage &src, int maxEdge)
    {
        using arstro::cosmo::DecodedImage;
        if (!src.ok() || (src.width <= maxEdge && src.height <= maxEdge)) return src;
        const double sc = (double)maxEdge / std::max(src.width, src.height);
        const int nw = std::max(1, (int)(src.width * sc)), nh = std::max(1, (int)(src.height * sc));
        DecodedImage out; out.width = nw; out.height = nh; out.rgba.resize((size_t)nw * nh * 4);
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x)
            {
                const int sx0 = (int)(x / sc), sx1 = std::min(src.width, (int)((x + 1) / sc) + 1);
                const int sy0 = (int)(y / sc), sy1 = std::min(src.height, (int)((y + 1) / sc) + 1);
                unsigned long r = 0, g = 0, b = 0, a = 0, n = 0;
                for (int sy = sy0; sy < sy1; ++sy)
                    for (int sx = sx0; sx < sx1; ++sx)
                    {
                        const uint8_t *p = &src.rgba[((size_t)sy * src.width + sx) * 4];
                        r += p[0]; g += p[1]; b += p[2]; a += p[3]; ++n;
                    }
                uint8_t *o = &out.rgba[((size_t)y * nw + x) * 4];
                if (n) { o[0] = (uint8_t)(r / n); o[1] = (uint8_t)(g / n); o[2] = (uint8_t)(b / n); o[3] = (uint8_t)(a / n); }
                else { o[0] = o[1] = o[2] = 0; o[3] = 255; }
            }
        return out;
    }
}
}
