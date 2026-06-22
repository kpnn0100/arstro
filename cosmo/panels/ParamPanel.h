/*
 *  Cosmo by arstro — ParamPanel: a titled panel of labeled knobs, each bound to a
 *  callback. The Basic / Color / Effects panels are just different specs of this
 *  one widget (DRY). Knobs are Pulsar's signature control and the only Artboard
 *  control with an onChange, so cosmo edits with knobs (matching the Pulsar look).
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <functional>
#include <string>
#include <vector>
#include <memory>

namespace arstro
{
namespace cosmo
{
    class ParamPanel : public artboard::Segment
    {
    public:
        struct Spec
        {
            std::string label;
            double min, max, def;
            std::function<void(double)> onChange;  // called live as the knob drags
        };

        ParamPanel(const std::string &title, const artboard::Theme &theme,
                   const artboard::Color &accent, const std::vector<Spec> &specs,
                   int columns = 3);

        /** Set knob positions without firing callbacks (used when switching image slots). */
        void setValues(const std::vector<double> &values);
        double valueOf(int i) const;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        std::string mTitle;
        artboard::Color mAccent;
        std::vector<std::shared_ptr<artboard::Knob>> mKnobs;
    };
}
}
