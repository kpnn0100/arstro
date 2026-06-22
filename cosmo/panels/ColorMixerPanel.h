/*
 *  Cosmo by arstro — ColorMixerPanel: the HSL colour mixer. A ComboBox selects one
 *  of the eight hue bands; three knobs (hue / saturation / luminance) edit the
 *  selected band. Switching bands saves the current knob values and loads the new
 *  band's. Built from existing Artboard controls (no custom widget).
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <array>
#include <functional>
#include <memory>

namespace arstro
{
namespace cosmo
{
    class ColorMixerPanel : public artboard::Segment
    {
    public:
        ColorMixerPanel(const artboard::Theme &theme, const artboard::Color &accent);

        // Called whenever a band's value changes: (band 0..7, hue, sat, lum), each -100..+100.
        std::function<void(int, double, double, double)> onChange;

        /** Load all 8 bands' values (used when switching image slots). */
        void setValues(const std::array<std::array<double, 3>, 8> &values);

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        void loadBand();
        void emitBand();

        artboard::Color mAccent;
        std::shared_ptr<artboard::ComboBox> mBandSel;
        std::shared_ptr<artboard::Knob> mHue, mSat, mLum;
        std::array<std::array<double, 3>, 8> mValues{};
        int mBand = 0;
    };
}
}
