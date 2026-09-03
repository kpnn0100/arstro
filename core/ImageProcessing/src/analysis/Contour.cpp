#include "Contour.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace arstro
{
    // ── Marching squares ─────────────────────────────────────────────────────────────────
    //
    // The honest outline of a plane of numbers is its `threshold` contour, and marching squares
    // is the whole algorithm: classify
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

    std::vector<ContourLoop>
    traceCoverageOutline(const std::vector<Pixel> &cov, int w, int h, float threshold,
                         int maxEdge, int minLoopPoints, bool closeAtBorder)
    {
        std::vector<ContourLoop> out;
        if (w <= 1 || h <= 1 || cov.size() < (std::size_t)w * h) return out;

        // `maxEdge == 0` walks every cell: a contour that is traced once and then stored is not
        // on the per-frame budget the coarsening was invented for, and the shape is thinned
        // afterwards by `simplifyLoop`, which does not blunt a corner to save a point.
        const int step = maxEdge > 0 ? std::max(1, (std::min(w, h) + maxEdge - 1) / maxEdge) : 1;
        // Outside the plane reads as BELOW the threshold, not as its nearest edge pixel. That
        // one change is what closes a region running off the frame — see the header; an open
        // chain has no area and no interior, so a caller cannot measure or fill one.
        auto at = [&](int x, int y) {
            if (x < 0 || y < 0 || x >= w || y >= h)
            {
                if (closeAtBorder) return 0.f;
                x = x < 0 ? 0 : (x >= w ? w - 1 : x);
                y = y < 0 ? 0 : (y >= h ? h - 1 : y);
            }
            return (float)cov[(std::size_t)y * w + x];
        };

        // One ring of virtual cells outside the plane, so a boundary can be found between the
        // outside and the first real row.
        const int first = closeAtBorder ? -step : 0;
        const int lastX = closeAtBorder ? w : w - step - 1;
        const int lastY = closeAtBorder ? h : h - step - 1;

        std::vector<OutlinePoint> segs;   // pairs: [2i], [2i+1]
        for (int y = first; y <= lastY; y += step)
            for (int x = first; x <= lastX; x += step)
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
            ContourLoop loop;
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
            //
            // Clamped to the frame, because a crossing found against the virtual outside ring
            // lands half a step beyond it — geometrically right and useless to draw, since the
            // boundary of a region that reaches the edge of the photo IS the edge of the photo.
            for (auto &p : loop)
            {
                const float nx = (p.first + 0.5f) / (float)w;
                const float ny = (p.second + 0.5f) / (float)h;
                p.first = nx < 0.f ? 0.f : (nx > 1.f ? 1.f : nx);
                p.second = ny < 0.f ? 0.f : (ny > 1.f ? 1.f : ny);
            }
            out.push_back(std::move(loop));
        }
        return out;
    }

    // ── Douglas-Peucker ──────────────────────────────────────────────────────────────────
    //
    // Iterative rather than recursive: a traced contour can be tens of thousands of points and
    // the worst case of the recursive form is one frame per point. The explicit stack costs two
    // lines and removes the failure entirely.
    namespace
    {
        /** Perpendicular distance from `p` to the segment ab. Degenerates to the distance to `a`
         *  when the segment has no length, which is the answer that keeps a duplicated point
         *  from being kept forever. */
        float pointSegDistance(const std::pair<float, float> &p, const std::pair<float, float> &a,
                               const std::pair<float, float> &b)
        {
            const float dx = b.first - a.first, dy = b.second - a.second;
            const float len2 = dx * dx + dy * dy;
            float px = p.first - a.first, py = p.second - a.second;
            if (len2 > 1e-20f)
            {
                float t = (px * dx + py * dy) / len2;
                t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
                px -= dx * t;
                py -= dy * t;
            }
            return std::sqrt(px * px + py * py);
        }
    }

    ContourLoop simplifyLoop(const ContourLoop &loop, float tolerance)
    {
        const std::size_t n = loop.size();
        if (n < 4 || tolerance <= 0.f) return loop;

        std::vector<bool> keep(n, false);
        keep[0] = true;
        keep[n - 1] = true;
        std::vector<std::pair<std::size_t, std::size_t>> stack{{0, n - 1}};
        while (!stack.empty())
        {
            const auto seg = stack.back();
            stack.pop_back();
            if (seg.second <= seg.first + 1) continue;
            float worst = 0.f;
            std::size_t at = seg.first;
            for (std::size_t i = seg.first + 1; i < seg.second; ++i)
            {
                const float d = pointSegDistance(loop[i], loop[seg.first], loop[seg.second]);
                if (d > worst) { worst = d; at = i; }
            }
            if (worst <= tolerance) continue;
            keep[at] = true;
            stack.push_back({seg.first, at});
            stack.push_back({at, seg.second});
        }

        ContourLoop out;
        out.reserve(n / 4 + 4);
        for (std::size_t i = 0; i < n; ++i)
            if (keep[i]) out.push_back(loop[i]);
        // Fewer than three points has no area, and a region that survived every earlier filter
        // must not be simplified out of existence — it would become an invisible mask rather
        // than an absent one, which is the harder bug to see.
        return out.size() >= 3 ? out : loop;
    }

    float loopArea(const ContourLoop &loop)
    {
        if (loop.size() < 3) return 0.f;
        double acc = 0.0;
        for (std::size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++)
            acc += (double)loop[j].first * loop[i].second - (double)loop[i].first * loop[j].second;
        return (float)std::fabs(acc * 0.5);
    }
}
