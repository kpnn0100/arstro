/*
 *  Pulsar by arstro — LfoPanel: three LFOs selected by tabs down the LEFT side.
 *  The centre is the selected LFO's editable bezier curve (its shape). On the
 *  RIGHT are a rate knob and a BPM toggle: with BPM on the rate snaps to a musical
 *  ratio (1/16 1/8 1/4 1/2 1 2 of a beat); with it off the rate is in Hz. There is
 *  no depth knob — modulation depth lives on the target knob's ring (bipolar, ±).
 *  An LFO only advances while a note is gated, retriggering on note-on. A single
 *  draggable source badge (following the selected tab) routes the LFO to a knob.
 *
 *  Reserved for later: a real global tempo (BPM is assumed 120 for the ratios).
 */
#pragma once
#include "../Artboard/include/artboard/artboard.h"
#include "LfoCurve.h"
#include "ModSourceBadge.h"
#include <array>
#include <functional>
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

        void setGate(bool on);
        int count() const { return kCount; }
        int sourceId(int i) const { return i; }
        double output(int i) const; // current LFO value (0 when un-gated)
        void setAssignSink(std::function<void(int, const artboard::Color &, const artboard::Point &)> fn);

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &localPoint) override;

    private:
        static constexpr int kCount = 3;
        void selectTab(int i);
        double rateHz(int i) const; // effective cycle rate (Hz) honouring BPM mode

        artboard::Color mAccent;
        std::array<std::shared_ptr<LfoCurve>, kCount> mCurves;
        std::array<double, kCount> mPhase{{0, 0, 0}};
        std::array<double, kCount> mRate{{0.4, 0.4, 0.4}};
        std::array<bool, kCount> mBpm{{false, false, false}};
        int mSelected = 0;
        bool mGate = false;
        double mLastMs = -1.0;

        std::shared_ptr<artboard::Knob> mRateKnob;
        std::shared_ptr<artboard::ToggleSwitch> mBpmToggle;
        std::shared_ptr<ModSourceBadge> mBadge;
    };
}
}
