#include "Matte.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace interstellar
{
namespace render
{
    namespace
    {
        // 1 inside [lo, hi], falling to 0 over `soft` outside it
        inline float band(float v, float lo, float hi, float soft)
        {
            if (v >= lo && v <= hi) return 1.0f;
            const float d = v < lo ? lo - v : v - hi;
            return soft > 1e-6f ? std::max(0.0f, 1.0f - d / soft) : 0.0f;
        }

        template <class T>
        void rgbAt(const Raster &in, size_t i, float &r, float &g, float &b);
        template <>
        inline void rgbAt<uint8_t>(const Raster &in, size_t i, float &r, float &g, float &b)
        {
            const uint8_t *p = &in.rgba[i * 4];
            r = p[0] / 255.0f; g = p[1] / 255.0f; b = p[2] / 255.0f;
        }
        template <>
        inline void rgbAt<uint16_t>(const Raster &in, size_t i, float &r, float &g, float &b)
        {
            const uint16_t *p = &in.rgba16[i * 4];
            r = p[0] / 65535.0f; g = p[1] / 65535.0f; b = p[2] / 65535.0f;
        }

        template <class T>
        void qualify(const EffectRun &m, const Raster &in, std::vector<float> &key)
        {
            const float hue = (float)m.get("hue"), width = (float)m.get("hueWidth"), hsoft = (float)m.get("hueSoft");
            const float sLo = (float)m.get("satLow"), sHi = (float)m.get("satHigh"), sSoft = (float)m.get("satSoft");
            const float lLo = (float)m.get("lumLow"), lHi = (float)m.get("lumHigh"), lSoft = (float)m.get("lumSoft");
            const bool inv = m.get("invert") >= 0.5, allHues = width >= 180.0f;
            const float mix = (float)std::clamp(m.mix, 0.0, 1.0);
            const size_t n = (size_t)in.width * in.height;
            for (size_t i = 0; i < n; ++i)
            {
                float r, g, b;
                rgbAt<T>(in, i, r, g, b);
                const float mx = std::max({r, g, b}), mn = std::min({r, g, b}), c = mx - mn;
                const float sat = mx > 1e-6f ? c / mx : 0.0f;
                const float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                float k = band(sat, sLo, sHi, sSoft) * band(lum, lLo, lHi, lSoft);
                if (!allHues && k > 0.0f)
                {
                    float h = 0.0f;
                    if (c > 1e-6f)
                    {
                        if (mx == r) h = 60.0f * std::fmod((g - b) / c + 6.0f, 6.0f);
                        else if (mx == g) h = 60.0f * ((b - r) / c + 2.0f);
                        else h = 60.0f * ((r - g) / c + 4.0f);
                    }
                    float d = std::fabs(h - hue);
                    d = std::min(d, 360.0f - d);
                    // a grey has no hue: it is selected only when every hue is
                    k *= c > 1e-6f ? band(d, 0.0f, width, hsoft) : 0.0f;
                }
                if (inv) k = 1.0f - k;
                key[i] *= 1.0f - mix * (1.0f - k);
            }
        }

        void window(const EffectRun &m, int w, int h, std::vector<float> &key)
        {
            const bool rect = m.get("shape") >= 0.5, inv = m.get("invert") >= 0.5;
            const double cx = m.get("centerX"), cy = m.get("centerY");
            const double hw = std::max(1e-6, m.get("width") * 0.5), hh = std::max(1e-6, m.get("height") * 0.5);
            const double soft = std::max(0.0, m.get("feather"));
            const double aspect = (double)h / std::max(1, w);   // y in units of the width
            const float mix = (float)std::clamp(m.mix, 0.0, 1.0);
            for (int y = 0; y < h; ++y)
            {
                const double py = (y + 0.5) / h;
                for (int x = 0; x < w; ++x)
                {
                    const double px = (x + 0.5) / w;
                    double d;   // distance outside the shape, in widths
                    if (rect)
                    {
                        const double dx = std::max(0.0, std::fabs(px - cx) - hw), dy = std::max(0.0, std::fabs(py - cy) - hh) * aspect;
                        d = std::sqrt(dx * dx + dy * dy);
                    }
                    else
                    {
                        const double nx = (px - cx) / hw, ny = (py - cy) / hh, r = std::sqrt(nx * nx + ny * ny);
                        // outside the ellipse: how far along its own radius, in widths
                        d = r <= 1.0 ? 0.0 : (r - 1.0) * std::sqrt(nx * nx * hw * hw + ny * ny * hh * hh * aspect * aspect) / r;
                    }
                    float k = d <= 0.0 ? 1.0f : soft > 1e-9 ? (float)std::max(0.0, 1.0 - d / soft) : 0.0f;
                    if (inv) k = 1.0f - k;
                    key[(size_t)y * w + x] *= 1.0f - mix * (1.0f - k);
                }
            }
        }

        template <class T>
        void mixT(const Raster &a, Raster &b, const std::vector<float> &key);
        template <>
        void mixT<uint8_t>(const Raster &a, Raster &b, const std::vector<float> &key)
        {
            for (size_t i = 0; i < key.size(); ++i)
                for (int c = 0; c < 3; ++c)
                {
                    uint8_t &o = b.rgba[i * 4 + c];
                    o = (uint8_t)std::lround(a.rgba[i * 4 + c] + (o - a.rgba[i * 4 + c]) * key[i]);
                }
        }
        template <>
        void mixT<uint16_t>(const Raster &a, Raster &b, const std::vector<float> &key)
        {
            for (size_t i = 0; i < key.size(); ++i)
                for (int c = 0; c < 3; ++c)
                {
                    uint16_t &o = b.rgba16[i * 4 + c];
                    o = (uint16_t)std::lround(a.rgba16[i * 4 + c] + ((double)o - a.rgba16[i * 4 + c]) * key[i]);
                }
        }
    }

    bool isMatte(const std::string &type) { return type == "qualifier.hsl" || type == "window.shape"; }

    void applyMatte(const EffectRun &m, const Raster &input, std::vector<float> &key)
    {
        const size_t n = (size_t)input.width * input.height;
        if (input.empty()) return;
        if (key.size() != n) key.assign(n, 1.0f);
        if (m.type == "qualifier.hsl")
        {
            if (input.deep()) qualify<uint16_t>(m, input, key);
            else qualify<uint8_t>(m, input, key);
        }
        else if (m.type == "window.shape") window(m, input.width, input.height, key);
    }

    void mixByKey(const Raster &a, Raster &b, const std::vector<float> &key)
    {
        if (a.width != b.width || a.height != b.height || key.size() != (size_t)a.width * a.height) return;
        if (a.deep() != b.deep())
        {
            Raster wa;
            if (b.deep()) toDeep(a, wa);
            else toShallow(a, wa);
            mixByKey(wa, b, key);
            return;
        }
        if (b.deep()) mixT<uint16_t>(a, b, key);
        else mixT<uint8_t>(a, b, key);
    }

    void keyPicture(const std::vector<float> &key, int width, int height, Raster &out)
    {
        out.allocate(width, height, 255);
        for (size_t i = 0; i < key.size() && i < (size_t)width * height; ++i)
        {
            const uint8_t v = (uint8_t)std::lround(std::clamp(key[i], 0.0f, 1.0f) * 255.0f);
            out.rgba[i * 4] = out.rgba[i * 4 + 1] = out.rgba[i * 4 + 2] = v;
        }
    }
}
}
}
