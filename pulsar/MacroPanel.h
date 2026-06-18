/*
 *  Pulsar by arstro — MacroPanel: four assignable macro knobs (M1–M4). They hold
 *  a [0,1] value each; routing them to targets via a mod matrix is reserved for a
 *  later pass.
 */
#pragma once
#include "../Artboard/include/artboard/artboard.h"
#include <array>
#include <memory>

namespace arstro
{
namespace pulsar
{
    class MacroPanel : public artboard::Segment
    {
    public:
        MacroPanel(const artboard::Theme &theme, const artboard::Color &accent);

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        artboard::Color mAccent;
        std::array<double, 4> mMacro{{0.0, 0.0, 0.0, 0.0}};
        std::array<std::shared_ptr<artboard::Knob>, 4> mKnobs;
    };
}
}
