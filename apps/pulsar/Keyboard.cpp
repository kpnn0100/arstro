#include "Keyboard.h"
#include <cmath>

namespace arstro
{
namespace pulsar
{
    using namespace artboard;

    namespace
    {
        // white-key semitone within an octave, and the index of the white key a
        // black key sits after (C#,D#,(none),F#,G#,A#)
        const int kWhiteSemis[7] = {0, 2, 4, 5, 7, 9, 11};
        const int kBlackAfterWhite[5] = {0, 1, 3, 4, 5}; // C#,D#,F#,G#,A#
        const int kBlackSemis[5] = {1, 3, 6, 8, 10};
    }

    Keyboard::Keyboard(const Color &accent) : mAccent(accent)
    {
        width.set(1308.0);
        height.set(84.0);
    }

    int Keyboard::keyAt(const Point &p) const
    {
        const double ww = whiteW(), h = height.value();
        const double bw = ww * 0.62, bh = h * 0.62;
        // black keys are on top — test them first
        for (int oct = 0; oct < 2; ++oct)
            for (int b = 0; b < 5; ++b)
            {
                const double cx = (oct * 7 + kBlackAfterWhite[b] + 1) * ww;
                if (p.x >= cx - bw * 0.5 && p.x <= cx + bw * 0.5 && p.y <= bh)
                    return oct * 12 + kBlackSemis[b];
            }
        if (p.x < 0 || p.x >= width.value() || p.y < 0 || p.y > h) return -1;
        const int wi = (int)(p.x / ww);
        if (wi < 0 || wi >= kWhite) return -1;
        return (wi / 7) * 12 + kWhiteSemis[wi % 7];
    }

    void Keyboard::advance(double nowMs)
    {
        const double dt = mLastMs < 0.0 ? 0.0 : (nowMs - mLastMs) / 1000.0;
        mLastMs = nowMs;
        mLit.advance(dt, 26.0); // snappy press feedback
        Segment::advance(nowMs);
    }

    void Keyboard::onPaint(IRenderTarget &t) const
    {
        const double ww = whiteW(), w = width.value(), h = height.value();
        const double bw = ww * 0.62, bh = h * 0.62;
        // blend a key's base colour toward the accent by its live highlight amount
        auto lerp = [](const Color &a, const Color &b, double u) {
            return Color{a.r + (b.r - a.r) * u, a.g + (b.g - a.g) * u, a.b + (b.b - a.b) * u, 1.0};
        };
        const double lit = mLit.value();

        // white keys
        for (int i = 0; i < kWhite; ++i)
        {
            const int semi = (i / 7) * 12 + kWhiteSemis[i % 7];
            const double k = (semi == mLitNote) ? lit : 0.0;
            const Color fill = lerp(Color::hex(0xdfe4ec), mAccent, k);
            drawRoundedRect(t, Rect{i * ww + 1, 0, ww - 2, h}, 4.0,
                            Paint::filledStroked(fill, Color{0, 0, 0, 0.4}, 1.0));
        }
        // black keys
        for (int oct = 0; oct < 2; ++oct)
            for (int b = 0; b < 5; ++b)
            {
                const double cx = (oct * 7 + kBlackAfterWhite[b] + 1) * ww;
                const int semi = oct * 12 + kBlackSemis[b];
                const double k = (semi == mLitNote) ? lit : 0.0;
                const Color fill = lerp(Color::hex(0x14181f), mAccent, k);
                drawRoundedRect(t, Rect{cx - bw * 0.5, 0, bw, bh}, 3.0,
                                Paint::filledStroked(fill, Color{0, 0, 0, 0.5}, 1.0));
            }
    }

    bool Keyboard::handleGesture(const Gesture &g, const Point &lp)
    {
        using T = Gesture::Type;
        if (g.type == T::Down)
        {
            mDown = true;
            const int k = keyAt(lp);
            if (k >= 0) { mNote = k; mLitNote = k; mLit.setTarget(1.0); if (onGate) onGate(true); }
            return true;
        }
        if (g.type == T::Drag && mDown)
        {
            const int k = keyAt(lp);
            if (k >= 0) { mNote = k; mLitNote = k; } // glide the held note (highlight follows)
            return true;
        }
        if (g.type == T::Up || g.type == T::Drop)
        {
            mDown = false; mNote = -1;
            mLit.setTarget(0.0); // fade the highlight out (mLitNote kept for the fade)
            if (onGate) onGate(false);
            return true;
        }
        return Segment::handleGesture(g, lp);
    }
}
}
