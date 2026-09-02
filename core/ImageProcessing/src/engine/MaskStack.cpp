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
#include <unordered_map>

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

    // ── Tracing a computed mask's boundary (marching squares) ────────────────────────────
    //
    // A mask whose region is DECIDED rather than described — a semantic mask, a feathered path —
    // has no control points a view could draw. What it has is a coverage plane, and the honest
    // outline of that plane is its 0.5 contour. Marching squares is the whole algorithm: classify
    // each cell's four corners against the threshold, emit the one or two segments that separate
    // inside from outside, interpolate where they cross, then stitch the segments into loops.
    //
    // The stitch is the part worth reading. Two neighbouring cells compute the crossing on their
    // SHARED edge from the same two corner values, so the point comes out bit-identical on both
    // sides; the endpoints are still quantised into the key rather than compared as floats,
    // because "bit-identical in practice" is not a property to build a data structure on.
    namespace
    {
        struct OutlinePoint { float x, y; };

        /** The 16 marching-squares cases as pairs of EDGE indices — 0 top, 1 right, 2 bottom,
         *  3 left. Unordered pairs: the stitcher walks chains by endpoint, so which way round a
         *  segment was emitted never matters, and not pretending to track winding removes the
         *  one place this algorithm is usually got wrong. Cases 5 and 10 are the ambiguous
         *  saddles and are resolved from the cell's centre value, below. */
        const int kCaseEdges[16][4] = {
            {-1, -1, -1, -1},  // 0000
            { 3,  0, -1, -1},  // 0001  a
            { 0,  1, -1, -1},  // 0010  b
            { 3,  1, -1, -1},  // 0011  a b
            { 1,  2, -1, -1},  // 0100  c
            {-1, -1, -1, -1},  // 0101  a c   -> saddle, filled in at runtime
            { 0,  2, -1, -1},  // 0110  b c
            { 3,  2, -1, -1},  // 0111  a b c
            { 2,  3, -1, -1},  // 1000  d
            { 2,  0, -1, -1},  // 1001  a d
            {-1, -1, -1, -1},  // 1010  b d   -> saddle
            { 2,  1, -1, -1},  // 1011  a b d
            { 1,  3, -1, -1},  // 1100  c d
            { 1,  0, -1, -1},  // 1101  a c d
            { 0,  3, -1, -1},  // 1110  b c d
            {-1, -1, -1, -1},  // 1111
        };

        inline float crossAt(float v0, float v1, float t)
        {
            const float d = v1 - v0;
            if (d > -1e-12f && d < 1e-12f) return 0.5f;
            const float u = (t - v0) / d;
            return u < 0.f ? 0.f : (u > 1.f ? 1.f : u);
        }
    }

    std::vector<std::vector<std::pair<float, float>>>
    traceCoverageOutline(const std::vector<Pixel> &cov, int w, int h, float threshold,
                         int maxEdge, int minLoopPoints)
    {
        std::vector<std::vector<std::pair<float, float>>> out;
        if (w <= 1 || h <= 1 || cov.size() < (std::size_t)w * h) return out;
        if (maxEdge < 8) maxEdge = 8;

        // Walk a COARSER grid than the plane. The plane is smooth by construction, so the extra
        // samples describe the same curve with more points — points a view then has to transform
        // and stroke on every frame.
        const int step = std::max(1, (std::min(w, h) + maxEdge - 1) / maxEdge);
        auto at = [&](int x, int y) {
            x = x < 0 ? 0 : (x >= w ? w - 1 : x);
            y = y < 0 ? 0 : (y >= h ? h - 1 : y);
            return (float)cov[(std::size_t)y * w + x];
        };

        std::vector<OutlinePoint> segs;   // pairs: [2i], [2i+1]
        for (int y = 0; y + step < h; y += step)
            for (int x = 0; x + step < w; x += step)
            {
                const float va = at(x, y), vb = at(x + step, y);
                const float vc = at(x + step, y + step), vd = at(x, y + step);
                int code = 0;
                if (va > threshold) code |= 1;
                if (vb > threshold) code |= 2;
                if (vc > threshold) code |= 4;
                if (vd > threshold) code |= 8;
                if (code == 0 || code == 15) continue;

                // Where the contour crosses each of the four edges, if it does.
                const float fx = (float)x, fy = (float)y, fs = (float)step;
                const OutlinePoint e[4] = {
                    {fx + fs * crossAt(va, vb, threshold), fy},                    // top
                    {fx + fs, fy + fs * crossAt(vb, vc, threshold)},               // right
                    {fx + fs * crossAt(vd, vc, threshold), fy + fs},               // bottom
                    {fx, fy + fs * crossAt(va, vd, threshold)}};                   // left

                int pairs[4] = {-1, -1, -1, -1};
                if (code == 5 || code == 10)
                {
                    // The saddle. Two opposite corners are inside and the cell alone cannot say
                    // whether they are one region pinched in the middle or two that merely touch.
                    // The centre value decides — which is the standard resolution and also the
                    // only one that stays consistent with the neighbouring cells' answers.
                    const float centre = (va + vb + vc + vd) * 0.25f;
                    const bool joined = centre > threshold;
                    if ((code == 5) == joined) { pairs[0] = 0; pairs[1] = 1; pairs[2] = 2; pairs[3] = 3; }
                    else                       { pairs[0] = 3; pairs[1] = 0; pairs[2] = 1; pairs[3] = 2; }
                }
                else
                {
                    pairs[0] = kCaseEdges[code][0];
                    pairs[1] = kCaseEdges[code][1];
                }
                for (int k = 0; k + 1 < 4; k += 2)
                {
                    if (pairs[k] < 0 || pairs[k + 1] < 0) continue;
                    segs.push_back(e[pairs[k]]);
                    segs.push_back(e[pairs[k + 1]]);
                }
            }
        if (segs.empty()) return out;

        // ── stitch ──
        // Endpoints are quantised into the key rather than compared as floats. Neighbouring cells
        // do produce bit-identical crossings today (same two corner values, same arithmetic), but
        // that is a property of the code above, not of the algorithm, and a data structure should
        // not depend on it.
        const double kQ = 64.0;
        auto key = [&](const OutlinePoint &p) {
            return ((long long)std::llround(p.x * kQ) << 24) ^ (long long)std::llround(p.y * kQ);
        };
        std::unordered_multimap<long long, std::size_t> ends;   // key -> segment index
        ends.reserve(segs.size());
        for (std::size_t i = 0; i < segs.size(); i += 2)
        {
            ends.emplace(key(segs[i]), i);
            ends.emplace(key(segs[i + 1]), i);
        }
        std::vector<bool> used(segs.size() / 2, false);

        for (std::size_t s0 = 0; s0 < segs.size(); s0 += 2)
        {
            if (used[s0 / 2]) continue;
            used[s0 / 2] = true;
            std::vector<std::pair<float, float>> loop;
            loop.push_back({segs[s0].x, segs[s0].y});
            OutlinePoint tip = segs[s0 + 1];
            loop.push_back({tip.x, tip.y});

            // Walk forward until the chain closes or runs out. Bounded by the segment count, so a
            // key collision cannot turn this into an infinite walk — which is the failure a
            // hash-keyed stitcher has if it trusts its keys.
            for (std::size_t guard = 0; guard < segs.size(); ++guard)
            {
                bool advanced = false;
                auto range = ends.equal_range(key(tip));
                for (auto it = range.first; it != range.second; ++it)
                {
                    const std::size_t i = it->second;
                    if (used[i / 2]) continue;
                    const OutlinePoint &a = segs[i], &b = segs[i + 1];
                    const OutlinePoint next = (key(a) == key(tip)) ? b : a;
                    used[i / 2] = true;
                    tip = next;
                    loop.push_back({tip.x, tip.y});
                    advanced = true;
                    break;
                }
                if (!advanced) break;
            }

            if ((int)loop.size() < minLoopPoints) continue;   // a speck, not a boundary
            // Into normalised framed-image coordinates. A plane sample at index (x,y) is the
            // CENTRE of that pixel, which is the same convention buildMaskCoverage fills it with.
            for (auto &p : loop)
            {
                p.first = (p.first + 0.5f) / (float)w;
                p.second = (p.second + 0.5f) / (float)h;
            }
            out.push_back(std::move(loop));
        }
        return out;
    }

    static bool isIdentity(const LocalAdjust &a)
    {
        return a.exposure == 0 && a.contrast == 0 && a.highlights == 0 && a.shadows == 0 &&
               a.whites == 0 && a.blacks == 0 && a.temp == 0 && a.tint == 0 &&
               a.saturation == 0 && a.texture == 0 && a.clarity == 0 && a.dehaze == 0;
    }

    void applyMaskStack(Image &img, const std::vector<MaskParams> &masks, ISegmenter *seg,
                        std::vector<MaskOutline> *outlines)
    {
        const int w = img.width(), h = img.height(), ch = img.channels();
        const int colorCh = ch >= 3 ? 3 : ch;
        if (w <= 0 || h <= 0) return;
        if (outlines) outlines->clear();

        for (std::size_t mi = 0; mi < masks.size(); ++mi)
        {
            const MaskParams &m = masks[mi];
            const LocalAdjust &a = m.adjust;
            // A mask whose region is COMPUTED is outlined even when it changes no pixel yet: a
            // photographer who has just added a Detect mask is looking at the photo to decide
            // whether the region is right, and an outline that only appeared once a slider had
            // moved would be missing at exactly the moment it is wanted.
            const bool planar = m.type == MaskParams::Path || m.type == MaskParams::Semantic;
            const bool wantOutline = outlines != nullptr && planar;
            if (isIdentity(a) && !wantOutline) continue;
            // A path with fewer than three points has no area — nothing to fill and nothing to
            // outline.
            if (m.type == MaskParams::Path && m.path.size() < 3 && !m.inverted) continue;

            // The plane FIRST, before the adjusted copy. A mask that is only being outlined must
            // not pay for a clone of the framed image (27 MB at preview size) to produce a
            // picture nobody blends.
            //
            // A path's feather is a distance from its boundary and a semantic mask's region is a
            // question about the pixels, so both are built once for the whole plane rather than
            // evaluated per pixel (see the header). Every other type is closed-form and
            // allocates nothing.
            std::vector<Pixel> plane;
            if (m.type == MaskParams::Path) buildMaskCoverage(m, w, h, plane);
            else if (m.type == MaskParams::Semantic) buildMaskCoverage(m, img, plane, seg);
            if (wantOutline && !plane.empty())
            {
                MaskOutline o;
                o.maskIndex = (int)mi;
                o.loops = traceCoverageOutline(plane, w, h);
                outlines->push_back(std::move(o));
            }
            if (isIdentity(a)) continue;   // outlined, and there is nothing to blend

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
