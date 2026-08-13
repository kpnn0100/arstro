/*
 *  Pulsar by arstro — GainPanel: the per-note output stage (NOT the global master
 *  volume). A gain knob and a pan knob, plus a vertical meter that shows the gain's
 *  current modulated value — by default the amp envelope is routed here, so the
 *  meter visibly follows the envelope while a note plays. The gain knob is exposed
 *  so the app can wire that default modulation.
 *
 *  Reserved for later: feeding the audio output.
 */
#pragma once
#include "../../core/Artboard/include/artboard/artboard.h"
#include <memory>

namespace arstro
{
namespace pulsar
{
    class GainPanel : public artboard::Segment
    {
    public:
        GainPanel(const artboard::Theme &theme, const artboard::Color &accent);

        artboard::Knob *gainKnob() const { return mGain.get(); } // modulation target

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        artboard::Color mAccent;
        double mGainVal = 0.0, mPanVal = 0.5; // gain base 0: the amp envelope drives it
        std::shared_ptr<artboard::Knob> mGain;
        std::shared_ptr<artboard::Knob> mPan;
    };
}
}
