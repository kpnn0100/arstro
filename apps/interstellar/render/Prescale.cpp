#include "Prescale.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

namespace arstro
{
namespace interstellar
{
namespace render
{
    namespace
    {
        /** sRGB code value → linear, 16-bit. */
        const std::vector<uint16_t> &toLinear()
        {
            static const std::vector<uint16_t> t = [] {
                std::vector<uint16_t> v(256);
                for (int i = 0; i < 256; ++i)
                {
                    const double c = i / 255.0;
                    const double l = c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
                    v[(size_t)i] = (uint16_t)std::lround(l * 65535.0);
                }
                return v;
            }();
            return t;
        }
        /** linear (12-bit index) → sRGB code value. */
        const std::vector<uint8_t> &toEncoded()
        {
            static const std::vector<uint8_t> t = [] {
                std::vector<uint8_t> v(4096);
                for (int i = 0; i < 4096; ++i)
                {
                    const double l = i / 4095.0;
                    const double c = l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1.0 / 2.4) - 0.055;
                    v[(size_t)i] = (uint8_t)std::clamp((int)std::lround(c * 255.0), 0, 255);
                }
                return v;
            }();
            return t;
        }
    }

    int prescaleFactor(int w, int h, int longEdge)
    {
        if (longEdge <= 0 || w <= 0 || h <= 0) return 1;
        return std::max(1, std::max(w, h) / (2 * longEdge));
    }

    bool prescale(const Raster &in, int longEdge, Raster &out)
    {
        const int k = prescaleFactor(in.width, in.height, longEdge);
        if (k <= 1 || in.empty()) return false;
        const int ow = in.width / k, oh = in.height / k;   // whole blocks: a partial edge block is dropped
        if (ow <= 0 || oh <= 0) return false;
        out.allocate(ow, oh);
        const auto &lin = toLinear();
        const auto &enc = toEncoded();
        const int n = k * k;
        const int hw = (int)std::max(1u, std::thread::hardware_concurrency());
        const int parts = std::clamp(std::min(hw, 8), 1, std::max(1, oh / 8));
        auto rows = [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y)
                for (int x = 0; x < ow; ++x)
                {
                    uint32_t acc[3] = {0, 0, 0}, a = 0;
                    for (int dy = 0; dy < k; ++dy)
                    {
                        const uint8_t *p = &in.rgba[(((size_t)(y * k + dy)) * in.width + (size_t)x * k) * 4];
                        for (int dx = 0; dx < k; ++dx, p += 4)
                        {
                            acc[0] += lin[p[0]]; acc[1] += lin[p[1]]; acc[2] += lin[p[2]];
                            a += p[3];
                        }
                    }
                    uint8_t *o = &out.rgba[((size_t)y * ow + x) * 4];
                    for (int c = 0; c < 3; ++c) o[c] = enc[(size_t)((acc[c] / n) * 4095u / 65535u)];
                    o[3] = (uint8_t)(a / n);
                }
        };
        if (parts <= 1) rows(0, oh);
        else
        {
            std::vector<std::thread> ts;
            for (int i = 0; i < parts; ++i) ts.emplace_back(rows, oh * i / parts, oh * (i + 1) / parts);
            for (auto &t : ts) t.join();
        }
        return true;
    }
}
}
}
