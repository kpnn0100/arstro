/*
 *  interstellar_core — Raster: a decoded or composited frame.
 *
 *  Straight (non-premultiplied) RGBA8, matching `cosmo::DecodedImage` and
 *  `RenderService::Frame`, so a frame travels decoder -> rack -> composite -> writer with no
 *  conversion anywhere in between. Interleaved, row-major, top-down, no padding.
 *
 *  A DEEP raster (R-COLOR-1) carries `rgba16` instead — the same layout at 16 bits per component,
 *  full range (0 … 65535, so v8 * 257 is the same value) — and `rgba` is empty. Only a render to a
 *  deep codec makes one, and it never leaves that render: the monitor, the caches and the 8-bit
 *  kernels never see it (the grade and the composite, which check `rgba.size()`, refuse one).
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
        std::vector<uint16_t> rgba16;   // the deep payload (R-COLOR-1); set = this IS the picture
        int width = 0, height = 0;

        void allocate(int w, int h, uint8_t fill = 0)
        {
            width = w;
            height = h;
            rgba.assign((std::size_t)w * h * 4, fill);
            rgba16.clear();
        }
        void allocate16(int w, int h, uint16_t fill = 0)
        {
            width = w;
            height = h;
            rgba16.assign((std::size_t)w * h * 4, fill);
            rgba.clear();
        }
        bool deep() const { return !rgba16.empty(); }
        bool empty() const { return width <= 0 || height <= 0 || (rgba.empty() && rgba16.empty()); }
        std::size_t bytes() const { return rgba.size() + rgba16.size() * sizeof(uint16_t); }
    };

    /** `in` at 16 bits (v * 257: 255 is 65535 exactly). A deep `in` is copied. */
    inline void toDeep(const Raster &in, Raster &out)
    {
        if (in.deep()) { if (&in != &out) out = in; return; }
        Raster r;
        r.allocate16(in.width, in.height);
        for (std::size_t i = 0; i < in.rgba.size() && i < r.rgba16.size(); ++i) r.rgba16[i] = (uint16_t)(in.rgba[i] * 257);
        out = std::move(r);
    }

    /** `in` at 8 bits, rounded (v16 / 257). A shallow `in` is copied. */
    inline void toShallow(const Raster &in, Raster &out)
    {
        if (!in.deep()) { if (&in != &out) out = in; return; }
        Raster r;
        r.allocate(in.width, in.height);
        for (std::size_t i = 0; i < in.rgba16.size() && i < r.rgba.size(); ++i) r.rgba[i] = (uint8_t)((in.rgba16[i] + 128) / 257);
        out = std::move(r);
    }
}
}
