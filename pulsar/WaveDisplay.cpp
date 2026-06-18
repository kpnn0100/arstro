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

        // One cycle of each basic shape over phase ph (radians).
        double shapeSample(int shape, double ph)
        {
            switch (shape)
            {
            case WaveDisplay::Sine: return std::sin(ph);
            case WaveDisplay::Triangle: return std::asin(std::sin(ph)) * (2.0 / kPi);
            // Saw with the discontinuity in the MIDDLE: 0 → +1 over the first half,
            // jump to −1, then −1 → 0 over the second half.
            case WaveDisplay::Saw:
            {
                double q = std::fmod(ph, 2 * kPi);
                if (q < 0) q += 2 * kPi;
                return q < kPi ? q / kPi : q / kPi - 2.0;
            }
            case WaveDisplay::Square: return std::sin(ph) >= 0 ? 1.0 : -1.0;
            default: return std::sin(ph);
            }
        }

        Color mix(const Color &a, const Color &b, double f)
        {
            return Color{a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f,
                         a.b + (b.b - a.b) * f, a.a + (b.a - a.a) * f};
        }
        double clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
    }

    void WaveDisplay::setShape(int shape)
    {
        mShape = shape;
        if (shape == Morph3D)
            mFree = true;
        else
        {
            mFree = false;
            mPosTarget = (double)shape / 3.0; // snap to the shape's morph stop
        }
    }

    void WaveDisplay::setPosition(double pos01)
    {
        if (mFree)
            mPosTarget = clamp01(pos01);
    }

    void WaveDisplay::setPhase(double turns01) { mPhaseTarget = clamp01(turns01); }
    void WaveDisplay::setWarp(int mode) { mWarp = mode; }
    void WaveDisplay::setWarpAmount(double amt01) { mWarpTarget = clamp01(amt01); }
    void WaveDisplay::setUnison(int voices, double detune01, double blend01)
    {
        mVoices = voices < 1 ? 1 : voices;
        mDetune = clamp01(detune01);
        mBlend = clamp01(blend01);
    }

    // --- the WARP stage: remap normalized phase p∈[0,1) → [0,1). At amount 0 every
    //     mode is the identity, so WARP fades in continuously from "off". ---
    double WaveDisplay::warpPhase(double p01) const
    {
        const double a = mWarpDisplay;
        if (a <= 1e-4) return p01;
        switch (mWarp)
        {
        case Sync: // play the cycle faster and wrap — classic hard-sync harmonics
        {
            const double q = p01 * (1.0 + 3.0 * a);
            return q - std::floor(q);
        }
        case Bend: // phase-distortion skew — bends detail toward the start
            return std::pow(p01, std::pow(0.2, a)); // a:0→exp 1 (identity) … 1→0.2
        case PWM: // narrow the first half — pulse-width / asymmetry
        {
            const double d = 0.5 * (1.0 - 0.9 * a); // duty 0.5 → 0.05
            return p01 < d ? 0.5 * p01 / d : 0.5 + 0.5 * (p01 - d) / (1.0 - d);
        }
        case Mirror: // fold the second half back over the first (symmetric wave)
        {
            const double folded = p01 < 0.5 ? 2.0 * p01 : 2.0 * (1.0 - p01);
            return p01 * (1.0 - a) + folded * a;
        }
        default: return p01;
        }
    }

    double WaveDisplay::tableAt(double p01, double tablePos) const
    {
        const double seg = tablePos * 3.0; // 0..3 over [sine,tri,saw,square]
        int i = (int)seg;
        if (i < 0) i = 0;
        if (i > 2) i = 2;
        const double f = seg - i;
        const double ph = p01 * 2.0 * kPi;
        return shapeSample(i, ph) * (1.0 - f) + shapeSample(i + 1, ph) * f;
    }

    double WaveDisplay::waveAt(double p01, double tablePos) const
    {
        double read = p01 + mPhaseDisplay; // phase shift (cycles)
        read -= std::floor(read);
        return tableAt(warpPhase(read), tablePos);
    }

    double WaveDisplay::sample(double p01) const { return waveAt(p01, mPosDisplay); }

    void WaveDisplay::advance(double nowMs)
    {
        double dt = mLastMs < 0.0 ? 0.0 : (nowMs - mLastMs) / 1000.0;
        mLastMs = nowMs;
        if (dt > 0.0)
        {
            if (dt > 0.05) dt = 0.05;
            const double omega = 16.0; // critically damped morph / phase / warp
            double acc = -2.0 * omega * mVel - omega * omega * (mPosDisplay - mPosTarget);
            mVel += acc * dt;
            mPosDisplay += mVel * dt;
            acc = -2.0 * omega * mPhaseVel - omega * omega * (mPhaseDisplay - mPhaseTarget);
            mPhaseVel += acc * dt;
            mPhaseDisplay += mPhaseVel * dt;
            acc = -2.0 * omega * mWarpVel - omega * omega * (mWarpDisplay - mWarpTarget);
            mWarpVel += acc * dt;
            mWarpDisplay += mWarpVel * dt;
        }
        Segment::advance(nowMs);
    }

    void WaveDisplay::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        const double pad = 8.0;

        // panel well
        drawRoundedRect(t, Rect{0, 0, w, h}, 6.0,
                        Paint::filledStroked(Color{0, 0, 0, 0.35}, Color{1, 1, 1, 0.08}, 1.0));

        // ---- perspective parameters: a frame at depth d∈[0,1] (0 = front/near) ----
        const int N = 7;                       // receding frames
        const double frontY = h - 16.0, backY = 24.0;
        const double frontAmp = h * 0.30, backAmp = h * 0.10;
        const double maxInset = w * 0.16;      // far frames pulled inward
        const double posSpread = 0.55;         // table positions previewed into the distance

        auto baselineY = [&](double d) { return frontY + (backY - frontY) * d; };
        auto frameAmp = [&](double d) { return frontAmp + (backAmp - frontAmp) * d; };
        auto frameInset = [&](double d) { return maxInset * d; };

        // ---- gradient "floor": a perspective trapezoid fading up to transparent ----
        {
            const double topY = baselineY(1.0) - backAmp - 2.0;
            const double botY = baselineY(0.0) + frontAmp * 0.15;
            const double tIn = frameInset(1.0);
            t.beginPath();
            t.moveTo(pad + tIn, topY);
            t.lineTo(w - pad - tIn, topY);
            t.lineTo(w - pad, botY);
            t.lineTo(pad, botY);
            t.closePath();
            t.setLinearFill(0, topY, 0, botY,
                            Color{mColor.r, mColor.g, mColor.b, 0.0},
                            Color{mColor.r, mColor.g, mColor.b, 0.16});
            t.fillPath();
        }

        // ---- frames, back (dim, small, high) → front (bright, big, low) ----
        for (int j = N - 1; j >= 0; --j)
        {
            const double d = (N == 1) ? 0.0 : (double)j / (N - 1);
            const double tablePos = clamp01(mPosDisplay + d * posSpread);
            const double by = baselineY(d), amp = frameAmp(d), inset = frameInset(d);
            const double x0 = pad + inset, x1 = w - pad - inset;
            const double step = 2.0;

            std::vector<Point> pts;
            for (double x = x0; x <= x1; x += step)
            {
                const double p01 = (x - x0) / (x1 - x0);
                pts.push_back({x, by - waveAt(p01, tablePos) * amp});
            }
            if (pts.size() < 2) continue;

            if (j == 0)
            {
                // active frame: soft glow (widening, fading passes) + crisp core
                const double passW[3] = {7.0, 4.5, 2.8};
                const double passA[3] = {0.06, 0.12, 0.22};
                for (int pass = 0; pass < 3; ++pass)
                {
                    t.beginPath(); t.moveTo(pts[0].x, pts[0].y);
                    for (size_t i = 1; i < pts.size(); ++i) t.lineTo(pts[i].x, pts[i].y);
                    t.setStroke(Color{mColor.r, mColor.g, mColor.b, passA[pass]}, passW[pass]);
                    t.strokePath();
                }
                // unison fan: faint, phase-spread ghost cycles behind the core
                const int ghosts = mVoices - 1 < 6 ? mVoices - 1 : 6;
                if (ghosts > 0 && mBlend > 0.01)
                {
                    const double maxOff = 0.015 + mDetune * 0.06; // cycles of spread
                    for (int v = 1; v <= ghosts; ++v)
                    {
                        const double s = (v % 2 ? 1.0 : -1.0) * ((v + 1) / 2);
                        const double off = s * maxOff / ((ghosts + 1) * 0.5);
                        const double ga = 0.10 + 0.22 * mBlend;
                        t.beginPath();
                        for (size_t i = 0; i < pts.size(); ++i)
                        {
                            const double p01 = (pts[i].x - x0) / (x1 - x0) + off;
                            const double y = by - waveAt(p01 - std::floor(p01), tablePos) * amp;
                            if (i == 0) t.moveTo(pts[i].x, y); else t.lineTo(pts[i].x, y);
                        }
                        t.setStroke(Color{mColor.r, mColor.g, mColor.b, ga}, 1.2);
                        t.strokePath();
                    }
                }
                t.beginPath(); t.moveTo(pts[0].x, pts[0].y);
                for (size_t i = 1; i < pts.size(); ++i) t.lineTo(pts[i].x, pts[i].y);
                t.setStroke(mColor, 2.0); t.strokePath();
            }
            else
            {
                // receding frame: dimmer + thinner with depth, tinted toward the well
                const double a = 0.55 * (1.0 - d) + 0.06;
                const Color c = mix(mColor, Color{0.4, 0.45, 0.5, 1.0}, d * 0.5);
                t.beginPath(); t.moveTo(pts[0].x, pts[0].y);
                for (size_t i = 1; i < pts.size(); ++i) t.lineTo(pts[i].x, pts[i].y);
                t.setStroke(Color{c.r, c.g, c.b, a}, 1.0 + (1.0 - d) * 0.8);
                t.strokePath();
            }
        }
    }
}
}
