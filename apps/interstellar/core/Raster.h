/*
 *  interstellar_core — Raster: a decoded or composited frame.
 *
 *  Straight (non-premultiplied) RGBA8, matching `cosmo::DecodedImage` and
 *  `RenderService::Frame`, so a frame travels decoder -> rack -> composite -> writer with no
 *  conversion anywhere in between. Interleaved, row-major, top-down, no padding.
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace arstro
{
namespace interstellar
{
    struct Raster
    {
        std::vector<uint8_t> rgba;
        int width = 0, height = 0;

        void allocate(int w, int h, uint8_t fill = 0)
        {
            width = w;
            height = h;
            rgba.assign((std::size_t)w * h * 4, fill);
        }
        bool empty() const { return width <= 0 || height <= 0 || rgba.empty(); }
        std::size_t bytes() const { return rgba.size(); }
    };
}
}
