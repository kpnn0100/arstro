/*
 *  Pulsar by arstro — WaveDisplay: a Serum / Massive-X-style 3D wavetable "scape".
 *
 *  The wavetable is a continuous morph across the four basic shapes (sine → tri →
 *  saw → square) addressed by a table position in [0,1]; the four 2D selections
 *  snap the position to a stop (0, 1/3, 2/3, 1) and "3D" lets the POSITION knob
 *  roam freely. On top of the table sits a WARP stage (Serum's signature): a
 *  per-cycle phase distortion (Sync / Bend / PWM / Mirror) scaled by a [0,1]
 *  amount. UNISON is visualised as a fan of faint, phase-spread ghost cycles.
 *
 *  The view draws N single-cycle frames receding in perspective over a gradient
 *  "floor": the front frame is the active table position (bright, glowing); the
 *  frames behind preview the morph heading into the distance. Position, phase and
 *  warp all spring toward their targets, so every change animates smoothly.
 *
 *  Reserved for later: parsing an audio file into the morph stops, and per-param
 *  macro/LFO modulation.
 */
#pragma once
#include "../Artboard/include/artboard/artboard.h"

namespace arstro
{
namespace pulsar
{
    class WaveDisplay : public artboard::Segment
    {
    public:
        enum Shape { Sine = 0, Triangle = 1, Saw = 2, Square = 3, Morph3D = 4 };
        enum Warp { WarpOff = 0, Sync = 1, Bend = 2, PWM = 3, Mirror = 4 };

        void setShape(int shape);            // 0..3 snap to a stop; 4 = free (3D morph)
        int shape() const { return mShape; }
        void setPosition(double pos01);      // table position; only roams in 3D mode
        void setPhase(double turns01);       // horizontal phase shift in [0,1] cycles
        void setWarp(int mode);              // Warp mode (phase-distortion stage)
        void setWarpAmount(double amt01);    // warp depth in [0,1]
        void setUnison(int voices, double detune01, double blend01); // unison fan
        void setColor(const artboard::Color &c) { mColor = c; }

        /** Morphed + warped sample of the front frame at normalized phase `p01`. */
        double sample(double p01) const;
        void advance(double nowMs) override; // springs position / phase / warp

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool hitTestSelf(const artboard::Point &) const override { return false; }

    private:
        // table morph + warp + phase, all in normalized phase space [0,1)
        double warpPhase(double p01) const;            // apply the warp stage
        double tableAt(double p01, double tablePos) const; // morph at a table position
        double waveAt(double p01, double tablePos) const;  // warp + phase + table

        int mShape = Morph3D;
        bool mFree = true;                 // 3D mode: POSITION roams
        double mPosTarget = 0.5;
        double mPosDisplay = 0.5;
        double mVel = 0.0;
        double mPhaseTarget = 0.0;         // phase shift target (cycles)
        double mPhaseDisplay = 0.0;
        double mPhaseVel = 0.0;
        int mWarp = WarpOff;
        double mWarpTarget = 0.0;          // warp amount target
        double mWarpDisplay = 0.0;
        double mWarpVel = 0.0;
        int mVoices = 1;                   // unison fan
        double mDetune = 0.0;
        double mBlend = 0.0;
        double mLastMs = -1.0;
        artboard::Color mColor = artboard::Color::hex(0x4de2ff);
    };
}
}
