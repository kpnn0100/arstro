/*
 *  Pulsar by arstro — EnvPanel: a directly-editable ADSR envelope. The graph is
 *  the editor: drag the attack-peak / decay-sustain / release nodes to set A, D,
 *  S, R, and drag each segment's curve handle to bend it (per-segment curve). The
 *  envelope plays while a note is gated (attack→decay→sustain, release on note-off)
 *  and publishes its level as a modulation source; by default the app routes it to
 *  the GAIN, but the source badge can drag it anywhere.
 *
 *  Reserved for later: feeding the level into the audio amplitude.
 */
#pragma once
#include "../Artboard/include/artboard/artboard.h"
#include "ModSourceBadge.h"
#include <functional>
#include <memory>
#include <string>

namespace arstro
{
namespace pulsar
{
    class EnvPanel : public artboard::Segment
    {
    public:
        EnvPanel(const artboard::Theme &theme, const artboard::Color &accent, const std::string &title);
        void advance(double nowMs) override;

        void setGate(bool on);                 // note on/off drives playback
        int sourceId() const { return 200; }   // bus id for this envelope
        double output() const { return mLevel; }
        const artboard::Color &accent() const { return mAccent; }
        void setAssignSink(std::function<void(int, const artboard::Color &, const artboard::Point &)> fn);

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &localPoint) override;

    private:
        // graph geometry / resolved node positions in panel pixels
        struct Geom { double x0, x1, top, bot, region, hold, susX, nAx, nDx, nDy, nRx; };
        Geom geom() const;
        double levelAtTime(double tSec, bool releasing, double relFrom, double relT) const;

        artboard::Color mAccent;
        std::string mTitle;
        // params [0,1]; curves [-1,1]
        double mA = 0.10, mD = 0.30, mS = 0.60, mR = 0.40;
        double mCA = 0.0, mCD = 0.0, mCR = 0.0;
        // editing
        int mDrag = 0; // 0 none, 1 nodeA, 2 nodeD, 3 nodeR, 4 curveA, 5 curveD, 6 curveR
        double mDragRef = 0.0;
        // playback
        bool mGate = false;
        double mElapsed = 0.0, mRelElapsed = 0.0, mRelFrom = 0.0, mLevel = 0.0;
        double mLastMs = -1.0;

        std::shared_ptr<ModSourceBadge> mBadge;
    };
}
}
