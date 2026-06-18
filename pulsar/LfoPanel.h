/*
 *  Pulsar by arstro — LfoPanel: a low-frequency modulator. A shape selector
 *  (sine / tri / saw / square / sample-&-hold), a rate and a depth knob, and a
 *  live display that shows two cycles of the shape with a playhead dot cycling at
 *  the rate — so you can see the modulation moving. The depth scales the drawn
 *  amplitude. The shape morphs smoothly when switched.
 *
 *  Reserved for later: tempo sync and routing into the mod matrix.
 */
#pragma once
#include "../Artboard/include/artboard/artboard.h"
#include <memory>

namespace arstro
{
namespace pulsar
{
    class LfoPanel : public artboard::Segment
    {
    public:
        LfoPanel(const artboard::Theme &theme, const artboard::Color &accent);
        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        double lfoSample(double p01) const; // morphed LFO shape at normalized phase

        artboard::Color mAccent;
        double mShapeTarget = 0.0, mShapeDisp = 0.0, mShapeVel = 0.0; // 0..4
        double mRate = 0.4, mDepth = 0.8;
        double mPhase = 0.0; // playhead, advances at the rate
        double mLastMs = -1.0;

        std::shared_ptr<artboard::ComboBox> mShape;
        std::shared_ptr<artboard::Knob> mRateKnob;
        std::shared_ptr<artboard::Knob> mDepthKnob;
    };
}
}
