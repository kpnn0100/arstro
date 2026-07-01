/*
 *  Pulsar by arstro — Keyboard: a two-octave on-screen piano along the bottom.
 *  Pressing a key gates the synth (note-on); releasing un-gates it. Dragging
 *  across keys glides the held note. The gate drives note-only behaviour such as
 *  the LFOs (which advance and retrigger only while a note is held).
 *
 *  Reserved for later: pitch → oscillator frequency once audio is wired.
 */
#pragma once
#include "../Artboard/include/artboard/artboard.h"
#include <functional>

namespace arstro
{
namespace pulsar
{
    class Keyboard : public artboard::Segment
    {
    public:
        explicit Keyboard(const artboard::Color &accent);

        std::function<void(bool)> onGate; // true on note-on, false on note-off
        int note() const { return mNote; }

        void advance(double nowMs) override; // fades the pressed-key highlight in/out

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &localPoint) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        static constexpr int kWhite = 14; // two octaves
        int keyAt(const artboard::Point &p) const;       // semitone, or -1
        double whiteW() const { return width.value() / kWhite; }

        artboard::Color mAccent;
        int mNote = -1;    // currently held semitone (relative), -1 = none
        int mLitNote = -1; // key whose highlight is fading (kept during release fade-out)
        artboard::Spring mLit; // 0..1 press highlight, springs in on down and out on release
        double mLastMs = -1.0;
        bool mDown = false;
    };
}
}
