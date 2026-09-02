#include "MaskStack.h"
#include "../base/Parallel.h"
#include "../base/Spatial.h"
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

    /** A path mask's feather, at 1.0, as a fraction of the image's short edge. 8% is soft
     *  enough to blend a hand-drawn light into a sky and tight enough that the default 0.5
     *  still reads as a defined shape. */
    static constexpr float kPathFeatherFraction = 0.08f;

    static inline float smoothstep(float e0, float e1, float x)
    {
        if (e1 <= e0) return x >= e1 ? 1.f : 0.f;
        float t = clampf((x - e0) / (e1 - e0), 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    }

    namespace
    {
        /** Even-odd (crossing) test against a closed polygon. Even-odd rather than nonzero
         *  winding on purpose: a user who draws a figure-of-eight gets a hole where the loops
         *  overlap, which is the behaviour every vector editor has and the only one that lets
         *  someone cut a hole in a mask without a second mask. */
        bool insidePolygon(const std::vector<std::pair<float, float>> &poly, float x, float y)
        {
            bool in = false;
            const std::size_t n = poly.size();
            for (std::size_t i = 0, j = n - 1; i < n; j = i++)
            {
                const float xi = poly[i].first, yi = poly[i].second;
                const float xj = poly[j].first, yj = poly[j].second;
                if ((yi > y) != (yj > y) &&
                    x < (xj - xi) * (y - yi) / ((yj - yi) != 0.f ? (yj - yi) : 1e-8f) + xi)
                    in = !in;
            }
            return in;
        }
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
        case MaskParams::Path:
        {
            // Hard-edged here — see the header. The polygon is rebuilt per call, which is why
            // no render uses this path: `buildMaskCoverage` flattens once for the whole plane.
            const auto poly = maskPathPolygon(m.path);
            if (poly.size() >= 3) cov = insidePolygon(poly, nx, ny) ? 1.f : 0.f;
            break;
        }
        case MaskParams::Semantic:
            // 0, and see the header: this function is handed a point, and whether that point is
            // sky is a question about the picture. Answering anything here would be a guess
            // dressed as an answer.
            break;
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

    void buildMaskCoverage(const MaskParams &m, int w, int h, std::vector<Pixel> &out)
    {
        if (w <= 0 || h <= 0) { out.clear(); return; }
        out.assign((std::size_t)w * h, (Pixel)0);
        if (m.type == MaskParams::Semantic)
        {
            // Reachable only by a caller that has no image — the overlay asking what a mask
            // covers, say. Empty is the same answer `maskCoverage` gives, for the same reason.
            if (m.inverted) std::fill(out.begin(), out.end(), (Pixel)1);
            return;
        }
        if (m.type != MaskParams::Path)
        {
            // Closed-form types: the plane is just the per-pixel answer, materialised. Nothing
            // in the render asks for this, but a caller that wants a plane for any mask should
            // get one rather than a special case.
            par::parallelFor(h, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y)
                {
                    const float ny = (y + 0.5f) / h;
                    for (int x = 0; x < w; ++x)
                        out[(std::size_t)y * w + x] = (Pixel)maskCoverage(m, (x + 0.5f) / w, ny);
                }
            });
            return;
        }

        const auto poly = maskPathPolygon(m.path);
        if (poly.size() < 3)
        {
            // No area. An INVERTED empty path covers everything, which is the consistent
            // answer (`maskCoverage` says the same) even though it is a strange thing to ask.
            if (m.inverted) std::fill(out.begin(), out.end(), (Pixel)1);
            return;
        }

        // Fill the outline, one row at a time: gather the x where each edge crosses the row's
        // centre, sort them, and fill between alternate pairs. O(rows x edges), against the
        // O(pixels x edges) a per-pixel test would cost.
        par::parallelFor(h, [&](int yy0, int yy1) {
            std::vector<float> xs;
            xs.reserve(poly.size());
            for (int y = yy0; y < yy1; ++y)
            {
                const float ny = (y + 0.5f) / h;
                xs.clear();
                const std::size_t n = poly.size();
                for (std::size_t i = 0, j = n - 1; i < n; j = i++)
                {
                    const float yi = poly[i].second, yj = poly[j].second;
                    if ((yi > ny) != (yj > ny))
                    {
                        const float dy = (yj - yi) != 0.f ? (yj - yi) : 1e-8f;
                        xs.push_back(poly[i].first + (poly[j].first - poly[i].first) * (ny - yi) / dy);
                    }
                }
                if (xs.size() < 2) continue;
                std::sort(xs.begin(), xs.end());
                Pixel *row = out.data() + (std::size_t)y * w;
                for (std::size_t k = 0; k + 1 < xs.size(); k += 2)
                {
                    int a = (int)std::floor(xs[k] * w);
                    int b = (int)std::ceil(xs[k + 1] * w);
                    if (a < 0) a = 0;
                    if (b > w) b = w;
                    for (int x = a; x < b; ++x) row[x] = (Pixel)1;
                }
            }
        });

        // Feather = blur the filled shape. A distance transform would give the same 0.5 contour
        // and cost more; blurring is what makes the edge fall off SMOOTHLY on both sides of the
        // outline, which is what a photographer means by feathering a shape. feather == 0 leaves
        // the fill alone, because a hard-edged path is a legitimate request.
        const float feather = clampf(m.feather, 0.f, 1.f);
        if (feather > 0.f)
        {
            // Fraction of the SHORT edge, so the softness a user sets does not change when the
            // preview resolution does — the same reason every mask coordinate is normalised.
            const float sigma = feather * kPathFeatherFraction * (float)std::min(w, h);
            if (sigma >= 0.5f) spatial::gaussianBlurPlane(out, out, w, h, sigma);
        }
        if (m.inverted)
            for (Pixel &v : out) v = (Pixel)1 - v;
    }

    void buildMaskCoverage(const MaskParams &m, const Image &img, std::vector<Pixel> &out,
                           ISegmenter *seg)
    {
        const int w = img.width(), h = img.height();
        if (m.type != MaskParams::Semantic) { buildMaskCoverage(m, w, h, out); return; }
        if (w <= 0 || h <= 0) { out.clear(); return; }

        const SemanticSubject subject =
            (m.subject >= 0 && m.subject < (int)SemanticSubject::Count) ? (SemanticSubject)m.subject
                                                                       : SemanticSubject::Sky;
        const float sensitivity = clampf(m.sensitivity, 0.f, 1.f);

        // The seam first, the built-in second (R-AISEG-6). A model that declines is answering
        // normally — it was not trained on this subject, or cannot take this image — so this is
        // an `if`, not an error path. The size is re-checked because a foreign implementation
        // returning the wrong number of values would otherwise be read out of bounds by the
        // blend loop, and "the host's model was wrong" must not become "cosmo crashed".
        bool answered = false;
        if (seg) answered = seg->segment(img, subject, sensitivity, out) &&
                            out.size() == (std::size_t)w * h;
        if (!answered) segment::builtinCoverage(img, subject, sensitivity, out);

        // Feather is the same idea it is for a path — soften the boundary — and it is applied
        // the same way, on top of whatever decided the region. A classifier's edge is already
        // soft (R-AISEG-4 sees to that), so 0 is a perfectly ordinary setting here.
        const float feather = clampf(m.feather, 0.f, 1.f);
        if (feather > 0.f)
        {
            const float sigma = feather * kPathFeatherFraction * (float)std::min(w, h);
            if (sigma >= 0.5f) spatial::fastBlurPlane(out, out, w, h, sigma);
        }
        if (m.inverted)
            for (Pixel &v : out) v = (Pixel)1 - v;
    }

    static bool isIdentity(const LocalAdjust &a)
    {
        return a.exposure == 0 && a.contrast == 0 && a.highlights == 0 && a.shadows == 0 &&
               a.whites == 0 && a.blacks == 0 && a.temp == 0 && a.tint == 0 &&
               a.saturation == 0 && a.texture == 0 && a.clarity == 0 && a.dehaze == 0;
    }

    void applyMaskStack(Image &img, const std::vector<MaskParams> &masks, ISegmenter *seg)
    {
        const int w = img.width(), h = img.height(), ch = img.channels();
        const int colorCh = ch >= 3 ? 3 : ch;
        if (w <= 0 || h <= 0) return;

        for (const auto &m : masks)
        {
            const LocalAdjust &a = m.adjust;
            if (isIdentity(a)) continue;
            // A path with fewer than three points has no area, and building a plane to
            // discover that would mean cloning the image first.
            if (m.type == MaskParams::Path && m.path.size() < 3 && !m.inverted) continue;

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

            // A path's feather is a distance from its boundary, so its coverage is built once
            // for the whole plane rather than evaluated per pixel (see the header). Every other
            // type is closed-form and allocates nothing.
            std::vector<Pixel> plane;
            if (m.type == MaskParams::Path) buildMaskCoverage(m, w, h, plane);
            else if (m.type == MaskParams::Semantic) buildMaskCoverage(m, img, plane, seg);
            const Pixel *cov0 = plane.empty() ? nullptr : plane.data();

            Pixel *base = img.data();
            const Pixel *over = adj.data();
            par::parallelFor(h, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y)
                {
                    float ny = (y + 0.5f) / h;
                    for (int x = 0; x < w; ++x)
                    {
                        float cov = cov0 ? (float)cov0[(std::size_t)y * w + x]
                                         : maskCoverage(m, (x + 0.5f) / w, ny);
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
