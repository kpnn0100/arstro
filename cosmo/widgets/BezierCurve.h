/*
 *  Cosmo by arstro — shared bezier-curve model + sampler for the curve editors.
 *
 *  A curve is a list of control points; each can be a CORNER (straight segments) or
 *  SMOOTH (cubic-bezier tangents via independent in/out handles, After-Effects style,
 *  pulled out with Alt-drag). The editors keep this model; the engine consumes a
 *  dense piecewise-linear sampling of it (so the engine's LUT stays simple). The
 *  sampler clamps each segment's control-point x into the segment so x stays
 *  monotonic (the curve is a well-defined y = f(x)); it can also close the loop for
 *  a cyclic axis (hue), sampling the wrap segment and folding x back into [0,period).
 */
#pragma once
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace arstro
{
namespace cosmo
{
    struct CtrlPoint
    {
        float x = 0, y = 0;          // position
        float ix = 0, iy = 0;        // in-handle offset (toward lower x)
        float ox = 0, oy = 0;        // out-handle offset (toward higher x)
        bool smooth = false;         // false = corner (handles ignored)
    };

    namespace bez
    {
        inline float cubic(float p0, float p1, float p2, float p3, float t)
        {
            const float u = 1.0f - t;
            return u * u * u * p0 + 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t * p3;
        }

        // Sample one segment a->b (b already shifted by +period for a wrap segment).
        inline void sampleSeg(const CtrlPoint &a, const CtrlPoint &b, float bx,
                              int perSeg, float period, std::vector<std::pair<float, float>> &out)
        {
            const float x0 = a.x, x3 = bx;
            float x1 = a.smooth ? a.x + a.ox : x0;
            float y1 = a.smooth ? a.y + a.oy : a.y;
            float x2 = b.smooth ? bx + b.ix : x3;
            float y2 = b.smooth ? b.y + b.iy : b.y;
            // keep control x within [x0,x3] so x(t) is monotonic
            x1 = std::min(std::max(x1, x0), x3);
            x2 = std::min(std::max(x2, x0), x3);
            for (int s = 1; s <= perSeg; ++s)
            {
                const float t = (float)s / perSeg;
                float x = bez::cubic(x0, x1, x2, x3, t);
                const float y = bez::cubic(a.y, y1, y2, b.y, t);
                if (period > 0) { x = x - period * std::floor(x / period); }  // fold into [0,period)
                out.push_back({x, y});
            }
        }
    }

    /** Dense piecewise-linear sampling of the bezier curve (sorted by x). */
    inline std::vector<std::pair<float, float>>
    sampleCurve(std::vector<CtrlPoint> pts, bool cyclic, float period, int perSeg = 14)
    {
        std::vector<std::pair<float, float>> out;
        if (pts.empty())
            return out;
        std::sort(pts.begin(), pts.end(), [](const CtrlPoint &a, const CtrlPoint &b) { return a.x < b.x; });
        out.push_back({pts.front().x, pts.front().y});
        for (size_t i = 0; i + 1 < pts.size(); ++i)
            bez::sampleSeg(pts[i], pts[i + 1], pts[i + 1].x, perSeg, 0.0f, out);
        if (cyclic && pts.size() >= 2 && period > 0)
        {
            // wrap segment: last point -> first point shifted by +period
            CtrlPoint first = pts.front();
            bez::sampleSeg(pts.back(), first, first.x + period, perSeg, period, out);
        }
        return out;
    }
}
}
