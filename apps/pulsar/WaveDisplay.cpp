#include "WaveDisplay.h"
#include <cmath>
#include <vector>

namespace arstro
{
namespace pulsar
{
    using namespace artboard;

    namespace
    {
        constexpr double kPi = 3.14159265358979323846;

        double shapeSample(int shape, double ph)
        {
            switch (shape)
            {
            case 0: return std::sin(ph);                                  // sine
            case 1: return std::asin(std::sin(ph)) * (2.0 / kPi);         // triangle
            case 2: { double q = std::fmod(ph, 2 * kPi); if (q < 0) q += 2 * kPi; return q < kPi ? q / kPi : q / kPi - 2.0; } // saw (mid jump)
            default: return std::sin(ph) >= 0 ? 1.0 : -1.0;              // square
            }
        }
        double clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
    }

    void WaveDisplay::setPosition(double pos01) { mPos.setTarget(clamp01(pos01)); }
    void WaveDisplay::setPhase(double turns01) { mPhase.setTarget(clamp01(turns01)); }
    void WaveDisplay::setWarp(int mode) { mWarp = mode; }
    void WaveDisplay::setWarpAmount(double amt01) { mWarpAmt.setTarget(clamp01(amt01)); }
    void WaveDisplay::setUnison(int voices, double detune01, double blend01)
    {
        mVoices = voices < 1 ? 1 : voices;
        mDetune = clamp01(detune01);
        mBlend = clamp01(blend01);
    }

    // WARP stage: remap normalized phase p∈[0,1) → [0,1); identity at amount 0.
    double WaveDisplay::warpPhase(double p01) const
    {
        const double a = mWarpAmt.value();
        if (a <= 1e-4) return p01;
        switch (mWarp)
        {
        case Sync: { const double q = p01 * (1.0 + 3.0 * a); return q - std::floor(q); }
        case Bend: return std::pow(p01, std::pow(0.2, a));
        case PWM: { const double d = 0.5 * (1.0 - 0.9 * a); return p01 < d ? 0.5 * p01 / d : 0.5 + 0.5 * (p01 - d) / (1.0 - d); }
        case Mirror: { const double f = p01 < 0.5 ? 2.0 * p01 : 2.0 * (1.0 - p01); return p01 * (1.0 - a) + f * a; }
        default: return p01;
        }
    }

    double WaveDisplay::tableAt(double p01, double tablePos) const
    {
        const double seg = tablePos * 3.0;
        int i = (int)seg; if (i < 0) i = 0; if (i > 2) i = 2;
        const double f = seg - i;
        const double ph = p01 * 2.0 * kPi;
        return shapeSample(i, ph) * (1.0 - f) + shapeSample(i + 1, ph) * f;
    }

    double WaveDisplay::waveAt(double p01) const
    {
        double read = p01 + mPhase.value();
        read -= std::floor(read);
        return tableAt(warpPhase(read), mPos.value());
    }

    double WaveDisplay::sample(double p01) const { return waveAt(p01); }

    void WaveDisplay::advance(double nowMs)
    {
        const double dt = mLastMs < 0.0 ? 0.0 : (nowMs - mLastMs) / 1000.0;
        mLastMs = nowMs;
        const double omega = 16.0; // slightly slower than the knob follower; a lush morph
        mPos.advance(dt, omega);
        mPhase.advance(dt, omega);
        mWarpAmt.advance(dt, omega);
        Segment::advance(nowMs);
    }

    void WaveDisplay::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, 6.0,
                        Paint::filledStroked(Color{0, 0, 0, 0.35}, Color{1, 1, 1, 0.08}, 1.0));
        const double pad = 8.0, midY = h * 0.5, amp = h * 0.40;
        const double x0 = pad, x1 = w - pad;

        // centre line
        t.beginPath(); t.moveTo(x0, midY); t.lineTo(x1, midY);
        t.setStroke(Color{1, 1, 1, 0.08}, 1.0); t.strokePath();

        std::vector<Point> pts;
        for (double x = x0; x <= x1; x += 1.5)
            pts.push_back({x, midY - waveAt((x - x0) / (x1 - x0)) * amp});
        if (pts.size() < 2) return;

        // filled area under the wave (gradient floor)
        t.beginPath(); t.moveTo(pts[0].x, midY);
        for (auto &p : pts) t.lineTo(p.x, p.y);
        t.lineTo(pts.back().x, midY); t.closePath();
        t.setLinearFill(0, midY - amp, 0, midY + amp,
                        Color{mColor.r, mColor.g, mColor.b, 0.14},
                        Color{mColor.r, mColor.g, mColor.b, 0.0});
        t.fillPath();

        // unison fan: faint phase-spread ghost cycles behind the core
        const int ghosts = mVoices - 1 < 6 ? mVoices - 1 : 6;
        if (ghosts > 0 && mBlend > 0.01)
        {
            const double maxOff = 0.015 + mDetune * 0.06;
            for (int v = 1; v <= ghosts; ++v)
            {
                const double s = (v % 2 ? 1.0 : -1.0) * ((v + 1) / 2);
                const double off = s * maxOff / ((ghosts + 1) * 0.5);
                const double ga = 0.10 + 0.22 * mBlend;
                t.beginPath();
                for (size_t i = 0; i < pts.size(); ++i)
                {
                    const double p01 = (pts[i].x - x0) / (x1 - x0) + off;
                    const double y = midY - waveAt(p01 - std::floor(p01)) * amp;
                    if (i == 0) t.moveTo(pts[i].x, y); else t.lineTo(pts[i].x, y);
                }
                t.setStroke(Color{mColor.r, mColor.g, mColor.b, ga}, 1.2);
                t.strokePath();
            }
        }

        // soft glow (three widening, fading passes) + crisp core
        const double passW[3] = {6.0, 4.0, 2.6};
        const double passA[3] = {0.07, 0.13, 0.24};
        for (int pass = 0; pass < 3; ++pass)
        {
            t.beginPath(); t.moveTo(pts[0].x, pts[0].y);
            for (size_t i = 1; i < pts.size(); ++i) t.lineTo(pts[i].x, pts[i].y);
            t.setStroke(Color{mColor.r, mColor.g, mColor.b, passA[pass]}, passW[pass]);
            t.strokePath();
        }
        t.beginPath(); t.moveTo(pts[0].x, pts[0].y);
        for (size_t i = 1; i < pts.size(); ++i) t.lineTo(pts[i].x, pts[i].y);
        t.setStroke(mColor, 2.0); t.strokePath();
    }
}
}
