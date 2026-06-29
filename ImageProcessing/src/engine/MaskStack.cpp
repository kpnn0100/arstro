#include "MaskStack.h"
#include "../base/Parallel.h"
#include "../tone/Exposure.h"
#include "../tone/Contrast.h"
#include "../tone/ToneRegions.h"
#include "../color/WhiteBalance.h"
#include "../color/Vibrance.h"
#include "../effect/Texture.h"
#include "../effect/Clarity.h"
#include "../effect/Dehaze.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

    static inline float smoothstep(float e0, float e1, float x)
    {
        if (e1 <= e0) return x >= e1 ? 1.f : 0.f;
        float t = clampf((x - e0) / (e1 - e0), 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    }

    float maskCoverage(const MaskParams &m, float nx, float ny)
    {
        float cov = 0.f;
        const float feather = clampf(m.feather, 0.f, 1.f);
        switch (m.type)
        {
        case MaskParams::Radial:
        {
            float dx = (nx - m.cx) / std::max(m.rx, 1e-4f);
            float dy = (ny - m.cy) / std::max(m.ry, 1e-4f);
            float d = std::sqrt(dx * dx + dy * dy);
            cov = 1.f - smoothstep(1.f - feather, 1.f, d);  // 1 at centre, 0 past the edge
            break;
        }
        case MaskParams::Linear:
        {
            float ax = m.x1 - m.x0, ay = m.y1 - m.y0;
            float len2 = ax * ax + ay * ay; if (len2 < 1e-8f) len2 = 1e-8f;
            float t = ((nx - m.x0) * ax + (ny - m.y0) * ay) / len2;
            cov = smoothstep(0.f, 1.f, t);
            break;
        }
        case MaskParams::Brush:
        {
            for (const auto &dab : m.dabs)
            {
                float ddx = nx - dab.x, ddy = ny - dab.y;
                float dist = std::sqrt(ddx * ddx + ddy * ddy);
                float r = std::max(dab.radius, 1e-4f);
                float c = (1.f - smoothstep(r * (1.f - feather), r, dist)) * clampf(dab.flow, 0.f, 1.f);
                if (c > cov) cov = c;
            }
            break;
        }
        }
        if (m.inverted) cov = 1.f - cov;
        return clampf(cov, 0.f, 1.f);
    }

    static bool isIdentity(const LocalAdjust &a)
    {
        return a.exposure == 0 && a.contrast == 0 && a.highlights == 0 && a.shadows == 0 &&
               a.whites == 0 && a.blacks == 0 && a.temp == 0 && a.tint == 0 &&
               a.saturation == 0 && a.texture == 0 && a.clarity == 0 && a.dehaze == 0;
    }

    void applyMaskStack(Image &img, const std::vector<MaskParams> &masks)
    {
        const int w = img.width(), h = img.height(), ch = img.channels();
        const int colorCh = ch >= 3 ? 3 : ch;
        if (w <= 0 || h <= 0) return;

        for (const auto &m : masks)
        {
            const LocalAdjust &a = m.adjust;
            if (isIdentity(a)) continue;

            // Adjusted copy through the same processors the global pipeline uses.
            Image adj = img.clone();
            if (a.exposure != 0) { Exposure e; e.setExposureEv(a.exposure); adj = e.apply(adj); }
            if (a.contrast != 0) { Contrast c; c.setContrast(a.contrast); adj = c.apply(adj); }
            if (a.highlights || a.shadows || a.whites || a.blacks)
            {
                ToneRegions tr; tr.setHighlights(a.highlights); tr.setShadows(a.shadows);
                tr.setWhites(a.whites); tr.setBlacks(a.blacks); adj = tr.apply(adj);
            }
            if (a.temp != 0 || a.tint != 0)
            {
                WhiteBalance wb; wb.setTemperature(6500.f + a.temp / 100.f * 3500.f);
                wb.setTint(a.tint); adj = wb.apply(adj);
            }
            if (a.saturation != 0) { Vibrance v; v.setSaturation(a.saturation); adj = v.apply(adj); }
            if (a.texture != 0) { Texture t; t.setAmount(a.texture); adj = t.apply(adj); }
            if (a.clarity != 0) { Clarity cl; cl.setAmount(a.clarity); adj = cl.apply(adj); }
            if (a.dehaze != 0) { Dehaze dh; dh.setAmount(a.dehaze); adj = dh.apply(adj); }

            Pixel *base = img.data();
            const Pixel *over = adj.data();
            par::parallelFor(h, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y)
                {
                    float ny = (y + 0.5f) / h;
                    for (int x = 0; x < w; ++x)
                    {
                        float cov = maskCoverage(m, (x + 0.5f) / w, ny);
                        if (cov <= 0.f) continue;
                        Pixel *q = base + ((size_t)y * w + x) * ch;
                        const Pixel *o = over + ((size_t)y * w + x) * ch;
                        for (int c = 0; c < colorCh; ++c) q[c] = q[c] + (o[c] - q[c]) * (Pixel)cov;
                    }
                }
            });
        }
    }
}
