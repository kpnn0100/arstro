/*
 *  Pulsar by arstro — EnvPanel: an ADSR envelope section. Four knobs (attack,
 *  decay, sustain, release) drive a live envelope graph: the segment widths and
 *  the sustain level spring toward their targets so editing animates smoothly.
 *  The curve is filled with a gradient and glows like the other displays.
 *
 *  Reserved for later: dragging the graph nodes directly, and routing the
 *  envelope into the audio path.
 */
#pragma once
#include "../Artboard/include/artboard/artboard.h"
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace pulsar
{
    class EnvPanel : public artboard::Segment
    {
    public:
        EnvPanel(const artboard::Theme &theme, const artboard::Color &accent, const std::string &title);
        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        artboard::Color mAccent;
        std::string mTitle;
        double mA = 0.05, mD = 0.30, mS = 0.60, mR = 0.40;       // targets
        double mAd = 0.05, mDd = 0.30, mSd = 0.60, mRd = 0.40;   // sprung display
        double mAv = 0, mDv = 0, mSv = 0, mRv = 0;               // velocities
        double mLastMs = -1.0;

        std::vector<std::shared_ptr<artboard::Knob>> mKnobs;
    };
}
}
