/*
 *  Pulsar by arstro — FilterPanel: a multimode filter section. A type selector
 *  (LP12 / LP24 / BP / HP / NOTCH) drives a live magnitude-response curve drawn
 *  over a gradient floor; cutoff and resonance spring toward their knob targets so
 *  the curve animates. Cutoff, resonance, drive and envelope-amount knobs below.
 *
 *  The response curve is a visual approximation (UI), not the audio DSP — the
 *  audio path is reserved for a later pass.
 */
#pragma once
#include "../Artboard/include/artboard/artboard.h"
#include <memory>
#include <vector>

namespace arstro
{
namespace pulsar
{
    class FilterPanel : public artboard::Segment
    {
    public:
        FilterPanel(const artboard::Theme &theme, const artboard::Color &accent);
        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        double gain(double x) const; // magnitude response at log-freq position x∈[0,1]

        artboard::Color mAccent;
        int mType = 0;
        double mCutoff = 0.6, mReso = 0.15, mDrive = 0.0, mEnv = 0.3;
        double mCutoffDisp = 0.6, mResoDisp = 0.15;
        double mCutoffVel = 0.0, mResoVel = 0.0;
        double mLastMs = -1.0;

        std::shared_ptr<artboard::ComboBox> mTypeCombo;
        std::vector<std::shared_ptr<artboard::Knob>> mKnobs;
    };
}
}
