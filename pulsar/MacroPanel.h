/*
 *  Pulsar by arstro — MacroPanel: four assignable macro knobs (M1–M4). They hold
 *  a [0,1] value each; routing them to targets via a mod matrix is reserved for a
 *  later pass.
 */
#pragma once
#include "../Artboard/include/artboard/artboard.h"
#include "ModSourceBadge.h"
#include <array>
#include <functional>
#include <memory>

namespace arstro
{
namespace pulsar
{
    class MacroPanel : public artboard::Segment
    {
    public:
        MacroPanel(const artboard::Theme &theme, const artboard::Color &accent);

        int count() const { return 4; }
        int sourceId(int i) const { return 100 + i; }      // bus id for macro i
        double value(int i) const { return mMacro[i]; }
        void setAssignSink(std::function<void(int, const artboard::Color &, const artboard::Point &)> fn);

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        artboard::Color mAccent;
        std::array<double, 4> mMacro{{0.0, 0.0, 0.0, 0.0}};
        std::array<std::shared_ptr<artboard::Knob>, 4> mKnobs;
        std::array<std::shared_ptr<ModSourceBadge>, 4> mBadges;
    };
}
}
