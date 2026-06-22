/*
 *  Cosmo by arstro — HistogramPanel: draws the engine's R/G/B/luminance histogram
 *  as four translucent filled curves, with a log/linear toggle. Custom drawing
 *  (rather than LineGraph) so all four channels render in one panel.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "../../ImageProcessing/src/analysis/Histogram.h"
#include <memory>

namespace arstro
{
namespace cosmo
{
    class HistogramPanel : public artboard::Segment
    {
    public:
        HistogramPanel(const artboard::Theme &theme, const artboard::Color &accent);

        void setHistogram(const arstro::HistogramData &h) { mData = h; }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        artboard::Color mAccent;
        std::shared_ptr<artboard::ToggleSwitch> mLogToggle;
        bool mLog = false;
        arstro::HistogramData mData;
    };
}
}
