/*
 *  Arstro ImageProcessing Library
 *
 *  CurvePoint: one bezier control point for a mixer curve, in the display domain.
 *  A CORNER point (smooth=false) ignores its handles and joins its neighbours as a
 *  straight-ish segment; a SMOOTH point uses the independent in/out tangent handles
 *  (Alt-dragged out in the editor). This is the PERSISTED representation of a mixer
 *  curve: storing the control points (not a pre-sampled polyline) is what lets a
 *  reopened project rebuild the exact editable curve instead of a linear resampling.
 *
 *  curve::sample() flattens the control points to a dense piecewise-linear polyline
 *  the engine LUTs consume; it lives here (the model layer) so the engine's render
 *  and the editor's on-screen drawing use ONE sampler and can never diverge.
 */
#pragma once
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace arstro
{
    struct CurvePoint
    {
        float x = 0, y = 0;    // position
        float ix = 0, iy = 0;  // in-handle offset (toward lower x)
        float ox = 0, oy = 0;  // out-handle offset (toward higher x)
        bool smooth = false;   // false = corner (handles ignored)
    };

    namespace curve
    {
        inline float cubic(float p0, float p1, float p2, float p3, float t)
        {
            const float u = 1.0f - t;
            return u * u * u * p0 + 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t * p3;
        }

        // Sample one segment a->b (bx = b.x, already shifted by +period for a wrap segment).
        inline void sampleSeg(const CurvePoint &a, const CurvePoint &b, float bx,
                              int perSeg, float period, std::vector<std::pair<float, float>> &out)
        {
            const float x0 = a.x, x3 = bx;
            float x1 = a.smooth ? a.x + a.ox : x0;
            float y1 = a.smooth ? a.y + a.oy : a.y;
            float x2 = b.smooth ? bx + b.ix : x3;
            float y2 = b.smooth ? b.y + b.iy : b.y;
            x1 = std::min(std::max(x1, x0), x3);
            x2 = std::min(std::max(x2, x0), x3);
            for (int s = 1; s <= perSeg; ++s)
            {
                const float t = (float)s / perSeg;
                float x = cubic(x0, x1, x2, x3, t);
                const float y = cubic(a.y, y1, y2, b.y, t);
                if (period > 0) { x = x - period * std::floor(x / period); }
                out.push_back({x, y});
            }
        }

        /** Dense piecewise-linear sampling of the bezier control points (sorted by x).
         *  cyclic closes the loop across the wrap seam for a periodic axis (hue). */
        inline std::vector<std::pair<float, float>>
        sample(std::vector<CurvePoint> pts, bool cyclic, float period, int perSeg = 14)
        {
            std::vector<std::pair<float, float>> out;
            if (pts.empty()) return out;
            std::sort(pts.begin(), pts.end(), [](const CurvePoint &a, const CurvePoint &b) { return a.x < b.x; });
            out.push_back({pts.front().x, pts.front().y});
            for (size_t i = 0; i + 1 < pts.size(); ++i)
                sampleSeg(pts[i], pts[i + 1], pts[i + 1].x, perSeg, 0.0f, out);
            if (cyclic && pts.size() >= 2 && period > 0)
            {
                CurvePoint first = pts.front();
                sampleSeg(pts.back(), first, first.x + period, perSeg, period, out);
            }
            return out;
        }
    }
}
