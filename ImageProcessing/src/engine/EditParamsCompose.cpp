/*
 *  Arstro ImageProcessing Library
 *
 *  composeParams: stack one EditParams (`over`) on top of another (`base`). This is
 *  the math behind cosmo's group settings — an image inside a group renders with its
 *  own params composed with the group's, and nested groups fold from child to root.
 *  See composeParams() in EditParams.h for the per-field contract.
 */
#include "EditParams.h"
#include "../base/CurvePoint.h"
#include <algorithm>

namespace arstro
{
    namespace
    {
        using Points = std::vector<std::pair<float, float>>;

        bool isIdentityCurve(const Points &p)
        {
            return p.size() == 2 && p[0].first == 0 && p[0].second == 0 &&
                   p[1].first == 1 && p[1].second == 1;
        }

        // Piecewise-linear evaluation of a sorted (x-ascending) polyline, clamped at
        // the ends. Used for both tone-curve control points and sampled mixer curves.
        float interpPoly(const Points &p, float x)
        {
            if (p.empty()) return x;
            if (x <= p.front().first) return p.front().second;
            if (x >= p.back().first) return p.back().second;
            for (size_t k = 1; k < p.size(); ++k)
                if (x <= p[k].first)
                {
                    const float x0 = p[k - 1].first, y0 = p[k - 1].second;
                    const float x1 = p[k].first, y1 = p[k].second;
                    const float t = x1 > x0 ? (x - x0) / (x1 - x0) : 0.f;
                    return y0 + (y1 - y0) * t;
                }
            return p.back().second;
        }

        // Stack two tone curves additively in Y: at each sampled input the deviation
        // of `over` from identity is added to `base`. An identity `over` is a no-op
        // (returns base untouched, so no resample happens in the common case).
        Points addCurveDeltaY(Points base, Points over)
        {
            if (isIdentityCurve(over)) return base;
            std::sort(base.begin(), base.end(), [](auto &a, auto &b) { return a.first < b.first; });
            std::sort(over.begin(), over.end(), [](auto &a, auto &b) { return a.first < b.first; });
            const int kSamples = 33;
            Points out;
            out.reserve(kSamples);
            for (int i = 0; i < kSamples; ++i)
            {
                const float x = (float)i / (kSamples - 1);
                float y = interpPoly(base, x) + (interpPoly(over, x) - x);  // base + over's deviation
                y = y < 0 ? 0 : (y > 1 ? 1 : y);
                out.push_back({x, y});
            }
            return out;
        }

        // Stack two mixer curves (cyclic hue -> adjustment, both deltas from 0):
        // sample each to a polyline, add the adjustments, emit corner control points.
        std::vector<CurvePoint> addMixerDeltaY(const std::vector<CurvePoint> &base,
                                               const std::vector<CurvePoint> &over)
        {
            if (over.empty()) return base;   // group has no mixer curve on this channel
            if (base.empty()) return over;
            const Points bp = curve::sample(base, true, 360.f);
            const Points op = curve::sample(over, true, 360.f);
            const int kSamples = 37;
            std::vector<CurvePoint> out;
            out.reserve(kSamples);
            for (int i = 0; i < kSamples; ++i)
            {
                const float hue = i * (360.f / kSamples);
                float y = interpPoly(bp, hue) + interpPoly(op, hue);
                y = y < -1 ? -1 : (y > 1 ? 1 : y);
                CurvePoint c;
                c.x = hue;
                c.y = y;
                out.push_back(c);
            }
            return out;
        }
    }

    EditParams composeParams(const EditParams &base, const EditParams &over)
    {
        EditParams r = base;

        // ── additive scalar adjustments (neutral = 0) ──
        r.exposure += over.exposure;
        r.contrast += over.contrast;
        r.highlights += over.highlights;
        r.shadows += over.shadows;
        r.whites += over.whites;
        r.blacks += over.blacks;
        r.tint += over.tint;
        r.vibrance += over.vibrance;
        r.saturation += over.saturation;
        r.texture += over.texture;
        r.clarity += over.clarity;
        r.dehaze += over.dehaze;
        r.grainAmount += over.grainAmount;
        r.grainSize += over.grainSize;
        r.sharpenAmount += over.sharpenAmount;
        r.sharpenMasking += over.sharpenMasking;
        r.nrLuminance += over.nrLuminance;
        r.nrColor += over.nrColor;
        r.lensDistortion += over.lensDistortion;
        r.lensCA += over.lensCA;
        r.lensVignette += over.lensVignette;
        r.balance += over.balance;
        r.rotation += over.rotation;
        r.quarterTurns += over.quarterTurns;

        // ── scalars whose neutral is non-zero: add the offset from neutral ──
        r.temp += (over.temp - 6500.f);          // Kelvin offset (neutral 6500)
        r.sharpenRadius += (over.sharpenRadius - 1.f);

        // ── 3-way colour grade wheels ──
        for (int i = 0; i < 3; ++i)
        {
            r.grade[i].hue += over.grade[i].hue;
            r.grade[i].sat += over.grade[i].sat;
            r.grade[i].lum += over.grade[i].lum;
        }

        // ── hue-range remap: enable if either; add strength; adopt over's range when base is off ──
        r.remapStrength += over.remapStrength;
        if (over.remapEnable && !base.remapEnable)
        {
            r.remapEnable = true;
            r.remapSrc = over.remapSrc;
            r.remapRange = over.remapRange;
            r.remapDst = over.remapDst;
        }
        else
            r.remapEnable = base.remapEnable || over.remapEnable;

        // ── tone curves (RGB master + R/G/B) + mixer: additive in Y ──
        r.curve = addCurveDeltaY(base.curve, over.curve);
        for (int c = 0; c < 3; ++c)
            r.curveChannel[c] = addCurveDeltaY(base.curveChannel[c], over.curveChannel[c]);
        for (int c = 0; c < 3; ++c)
            r.mixer[c] = addMixerDeltaY(base.mixer[c], over.mixer[c]);
        // curveLog and crop follow `base` (already copied): framing/domain are per-item.

        // ── masks concatenate: the member's own first, the group's on top ──
        r.masks = base.masks;
        r.masks.insert(r.masks.end(), over.masks.begin(), over.masks.end());

        return r;
    }
}
