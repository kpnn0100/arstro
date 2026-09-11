#include "Composite.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace interstellar
{
    double Composite::blendChannel(Blend mode, double b, double o)
    {
        switch (mode)
        {
            case Blend::Normal: return o;
            case Blend::Multiply: return b * o;
            case Blend::Screen: return 1.0 - (1.0 - b) * (1.0 - o);
            case Blend::Overlay: return b < 0.5 ? 2 * b * o : 1.0 - 2 * (1.0 - b) * (1.0 - o);
            case Blend::Add: return std::min(1.0, b + o);
            case Blend::Subtract: return std::max(0.0, b - o);
            case Blend::Difference: return std::fabs(b - o);
        }
        return o;
    }

    void Composite::blendPixel(Blend mode, double opacity, const uint8_t *src, uint8_t *dst)
    {
        const double a = (src[3] / 255.0) * opacity;
        if (a <= 0.0) return;
        for (int c = 0; c < 3; ++c)
        {
            const double base = dst[c] / 255.0;
            const double over = src[c] / 255.0;
            const double mixed = blendChannel(mode, base, over);
            const double v = base + (mixed - base) * a;
            dst[c] = (uint8_t)std::lround(std::min(1.0, std::max(0.0, v)) * 255.0);
        }
        const double da = dst[3] / 255.0;
        dst[3] = (uint8_t)std::lround(std::min(1.0, da + a * (1.0 - da)) * 255.0);
    }

    void Composite::placeLayer(const Layer &l, Raster &out)
    {
        if (!l.source || l.source->empty() || out.empty()) return;
        const Raster &s = *l.source;
        const Geom &g = l.geom;

        // The crop is normalised on the graded frame; `fit` then decides how the cropped
        // rectangle meets the output raster, and the policy is EXPLICIT rather than implicit —
        // a user must be able to see which rule applied (R-COMP-5).
        const double cx = std::max(0.0, std::min(1.0, g.cropX));
        const double cy = std::max(0.0, std::min(1.0, g.cropY));
        const double cw = std::max(1e-6, std::min(1.0 - cx, g.cropW));
        const double ch = std::max(1e-6, std::min(1.0 - cy, g.cropH));
        const double srcW = s.width * cw, srcH = s.height * ch;

        double fitScale = 1.0;
        switch (l.fit)
        {
            case Fit::Contain: fitScale = std::min(out.width / srcW, out.height / srcH); break;
            case Fit::Cover: fitScale = std::max(out.width / srcW, out.height / srcH); break;
            case Fit::None: fitScale = 1.0; break;
            case Fit::Stretch: fitScale = 0.0; break;   // handled per-axis below
        }
        const double sx = l.fit == Fit::Stretch ? out.width / srcW : fitScale * g.scale;
        const double sy = l.fit == Fit::Stretch ? out.height / srcH : fitScale * g.scale;
        const double drawW = srcW * sx, drawH = srcH * sy;

        // Anchor, then translate. The anchor is normalised on the DRAWN rectangle, so rotating
        // about the centre is anchor 0.5,0.5 whatever the scale.
        const double ax = g.anchorX * drawW, ay = g.anchorY * drawH;
        const double originX = out.width * 0.5 - ax + g.x;
        const double originY = out.height * 0.5 - ay + g.y;
        const double rot = g.rotation * 3.14159265358979324 / 180.0;
        const double cosR = std::cos(-rot), sinR = std::sin(-rot);

        for (int y = 0; y < out.height; ++y)
            for (int x = 0; x < out.width; ++x)
            {
                // Inverse-map the destination pixel, so every output pixel is written once and
                // rotation leaves no holes.
                double dx = x + 0.5 - (originX + ax);
                double dy = y + 0.5 - (originY + ay);
                double ux = dx * cosR - dy * sinR + ax;
                double uy = dx * sinR + dy * cosR + ay;
                if (ux < 0 || uy < 0 || ux >= drawW || uy >= drawH) continue;
                const int spx = (int)(cx * s.width + (ux / sx));
                const int spy = (int)(cy * s.height + (uy / sy));
                if (spx < 0 || spy < 0 || spx >= s.width || spy >= s.height) continue;
                const uint8_t *src = &s.rgba[((size_t)spy * s.width + spx) * 4];
                uint8_t *dst = &out.rgba[((size_t)y * out.width + x) * 4];
                blendPixel(l.blend, l.opacity, src, dst);
            }
    }

    void Composite::compose(const std::vector<Layer> &bottomToTop, int width, int height, Raster &out)
    {
        out.allocate(width, height, 0);
        for (const auto &l : bottomToTop) placeLayer(l, out);
    }

    void Composite::mix(const Raster &a, const Raster &b, double tB, Raster &out)
    {
        // A dissolve is a LINEAR alpha ramp — see Timeline::addTransition for why.
        const double t = std::max(0.0, std::min(1.0, tB));
        const int w = a.width ? a.width : b.width;
        const int h = a.height ? a.height : b.height;
        out.allocate(w, h, 0);
        for (size_t i = 0; i < out.rgba.size(); ++i)
        {
            const double av = i < a.rgba.size() ? a.rgba[i] : 0;
            const double bv = i < b.rgba.size() ? b.rgba[i] : 0;
            out.rgba[i] = (uint8_t)std::lround(av + (bv - av) * t);
        }
    }
}
}
