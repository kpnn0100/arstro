/*
 *  Pulsar by arstro — LfoPanel: three LFOs selectable by tab (LFO 1/2/3). Each
 *  LFO owns an editable bezier curve (its shape, drawn AE-keyframe style), a rate
 *  and a depth. There is no fixed-shape selector — the curve IS the shape. A
 *  playhead tracks the phase, and an LFO only advances while a note is gated
 *  (retriggered on note-on); rate sets the cycle speed.
 *
 *  Reserved for later: tempo sync and routing into the mod matrix.
 */
#pragma once
#include "../Artboard/include/artboard/artboard.h"
#include "LfoCurve.h"
#include <array>
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

        void setGate(bool on); // note-on retriggers all LFO phases; off freezes them

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        static constexpr int kCount = 3;
        artboard::Color mAccent;
        std::array<std::shared_ptr<LfoCurve>, kCount> mCurves;
        std::array<double, kCount> mPhase{{0, 0, 0}};
        std::array<double, kCount> mRate{{0.4, 0.4, 0.4}};
        bool mGate = false;
        double mLastMs = -1.0;

        std::shared_ptr<artboard::TabView> mTabs;
    };
}
}
