/*
 *  Pulsar by arstro — WaveDisplay: a single-cycle waveform view. Internally it is
 *  always a continuous morph across the four basic shapes (sine → tri → saw →
 *  square) by a table position in [0,1]; the four 2D selections snap the position
 *  to a stop (0, 1/3, 2/3, 1) and "3D" lets the POSITION knob roam freely. The
 *  displayed position springs toward its target, so every waveform change — both
 *  selecting a shape and dragging POSITION — animates smoothly.
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

        void setShape(int shape);            // 0..3 snap to a stop; 4 = free (3D morph)
        int shape() const { return mShape; }
        void setPosition(double pos01);      // table position; only roams in 3D mode
        void setPhase(double turns01);       // horizontal phase shift in [0,1] cycles
        void setColor(const artboard::Color &c) { mColor = c; }

        /** Morphed sample at phase `ph` (radians) for the current display position. */
        double sample(double ph) const;
        void advance(double nowMs) override; // springs the display position

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool hitTestSelf(const artboard::Point &) const override { return false; }

    private:
        int mShape = Morph3D;
        bool mFree = true;                 // 3D mode: POSITION roams
        double mPosTarget = 0.5;
        double mPosDisplay = 0.5;
        double mVel = 0.0;
        double mPhaseTarget = 0.0;         // phase shift target (cycles)
        double mPhaseDisplay = 0.0;
        double mPhaseVel = 0.0;
        double mLastMs = -1.0;
        artboard::Color mColor = artboard::Color::hex(0x4de2ff);
    };
}
}
