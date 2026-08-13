/*
 *  Pulsar by arstro — WaveDisplay: a single-cycle 2-D wavetable view. The table is
 *  a continuous morph across the four basic shapes (sine → tri → saw → square)
 *  addressed by a position in [0,1]; on top sits a WARP stage (Serum-style) — a
 *  per-cycle phase distortion (Sync / Bend / PWM / Mirror) scaled by a [0,1] amount.
 *  UNISON is shown as a fan of faint, phase-spread ghost cycles. Position, phase and
 *  warp all spring toward their targets, so every change animates smoothly.
 *
 *  Reserved for later: parsing an audio file into the morph stops, and audio output.
 */
#pragma once
#include "../../core/Artboard/include/artboard/artboard.h"

namespace arstro
{
namespace pulsar
{
    class WaveDisplay : public artboard::Segment
    {
    public:
        enum Warp { WarpOff = 0, Sync = 1, Bend = 2, PWM = 3, Mirror = 4 };

        void setPosition(double pos01);      // wavetable morph position
        void setPhase(double turns01);       // horizontal phase shift in [0,1] cycles
        void setWarp(int mode);              // Warp mode (phase-distortion stage)
        void setWarpAmount(double amt01);    // warp depth in [0,1]
        void setUnison(int voices, double detune01, double blend01); // unison fan
        void setColor(const artboard::Color &c) { mColor = c; }

        /** Morphed + warped sample at normalized phase `p01` (one cycle). */
        double sample(double p01) const;
        void advance(double nowMs) override; // springs position / phase / warp

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool hitTestSelf(const artboard::Point &) const override { return false; }

    private:
        double warpPhase(double p01) const;
        double tableAt(double p01, double tablePos) const;
        double waveAt(double p01) const;

        // Position / phase / warp-amount glide to their targets via the shared core
        // Spring (framerate-independent), so every change animates smoothly.
        artboard::Spring mPos, mPhase, mWarpAmt;
        int mWarp = WarpOff;
        int mVoices = 1;
        double mDetune = 0.0, mBlend = 0.0;
        double mLastMs = -1.0;
        artboard::Color mColor = artboard::Color::hex(0x4de2ff);
    };
}
}
