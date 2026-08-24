#include "ToneCurve.h"
#include "../base/ColorSpace.h"
#include <algorithm>

namespace arstro
{
    ToneCurve::ToneCurve()
    {
        mSmoothEnable = false;
        buildLut({{0.0f, 0.0f}, {1.0f, 1.0f}}, mLut);  // identity master
        for (int c = 0; c < kChannels; ++c)
            buildLut({{0.0f, 0.0f}, {1.0f, 1.0f}}, mChanLut[c]);  // identity channels
        refreshIdentity();
    }

    void ToneCurve::setPoints(const std::vector<CurvePoint> &points)
    {
        // Flatten the bezier control points to a dense polyline (corners = straight,
        // smooth = bezier) with the SAME sampler the editor draws with, then LUT it.
        buildLut(curve::sample(points, false, 0.f), mLut);
        refreshIdentity();
    }

    void ToneCurve::setChannelPoints(int ch, const std::vector<CurvePoint> &points)
    {
        if (ch < 0 || ch >= kChannels) return;
        buildLut(curve::sample(points, false, 0.f), mChanLut[ch]);
        refreshIdentity();
    }

    // The domain flag does not change WHETHER the curve is identity — an identity curve
    // is identity in either domain, and a log round trip of a straight line is the
    // no-op this skip exists to remove — so it does not touch mIdentity.
    void ToneCurve::setLogScale(bool log) { mLog = log; }

    bool ToneCurve::lutIsIdentity(const Pixel *lut)
    {
        // buildLut evaluates a piecewise-linear curve at each entry, so an identity
        // curve lands on i/(kLut-1) up to float rounding of that division. A tolerance
        // far below one 8-bit step (1/255) keeps the claim honest while tolerating it.
        const Pixel kEps = (Pixel)1e-6;
        for (int i = 0; i < kLut; ++i)
        {
            const Pixel want = (Pixel)i / (Pixel)(kLut - 1);
            const Pixel d = lut[i] - want;
            if (d > kEps || d < -kEps)
                return false;
        }
        return true;
    }

    void ToneCurve::refreshIdentity()
    {
        mIdentity = lutIsIdentity(mLut);
        for (int c = 0; c < kChannels && mIdentity; ++c)
            mIdentity = lutIsIdentity(mChanLut[c]);
    }

    void ToneCurve::buildLut(std::vector<std::pair<float, float>> pts, Pixel *lut)
    {
        for (auto &p : pts)
        {
            p.first = p.first < 0 ? 0 : (p.first > 1 ? 1 : p.first);
            p.second = p.second < 0 ? 0 : (p.second > 1 ? 1 : p.second);
        }
        std::sort(pts.begin(), pts.end(),
                  [](const std::pair<float, float> &a, const std::pair<float, float> &b)
                  { return a.first < b.first; });
        if (pts.empty())
            pts = {{0.0f, 0.0f}, {1.0f, 1.0f}};

        for (int i = 0; i < kLut; ++i)
        {
            const float x = (float)i / (kLut - 1);
            float y;
            if (x <= pts.front().first)
                y = pts.front().second;
            else if (x >= pts.back().first)
                y = pts.back().second;
            else
            {
                y = pts.back().second;
                for (size_t k = 1; k < pts.size(); ++k)
                {
                    if (x <= pts[k].first)
                    {
                        const float x0 = pts[k - 1].first, y0 = pts[k - 1].second;
                        const float x1 = pts[k].first, y1 = pts[k].second;
                        const float t = x1 > x0 ? (x - x0) / (x1 - x0) : 0.0f;
                        y = y0 + (y1 - y0) * t;
                        break;
                    }
                }
            }
            lut[i] = (Pixel)y;
        }
    }

    Pixel ToneCurve::sampleLut(const Pixel *lut, Pixel d)
    {
        if (d < 0) d = 0;
        if (d > 1) d = 1;
        const Pixel f = d * (Pixel)(kLut - 1);
        int i = (int)f;
        if (i >= kLut - 1) return lut[kLut - 1];
        const Pixel frac = f - (Pixel)i;
        return lut[i] * ((Pixel)1 - frac) + lut[i + 1] * frac;
    }

    void ToneCurve::processPixel(const Pixel *in, Pixel *out, int channels)
    {
        const int colorCh = channels >= 3 ? 3 : channels;
        for (int c = 0; c < colorCh; ++c)
        {
            // map linear -> display domain, apply master then the per-channel curve, map back
            const Pixel d = mLog ? color::srgbEncode(in[c]) : in[c];
            Pixel y = sampleLut(mLut, d);                                   // RGB master
            y = sampleLut(mChanLut[c < kChannels ? c : kChannels - 1], y);  // per-channel R/G/B
            out[c] = mLog ? color::srgbDecode(y) : y;
        }
        for (int c = colorCh; c < channels; ++c)
            out[c] = in[c];
    }
}
