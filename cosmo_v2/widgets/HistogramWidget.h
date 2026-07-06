/*
 *  cosmo_v2 by arstro — HistogramWidget: RGB + luminance histogram above the
 *  tab strip (App.tsx Histogram: `bg-[#0F0F0F] border-b`, a 280x68 plot of
 *  translucent overlapping R/G/B fills + a white luminance outline; the Figma
 *  mock's Log/Linear toggle is intentionally dropped -- task point 7 -- and the
 *  plot always uses the log scale). Unlike the Figma mock (synthetic bump-function
 *  peaks), this plots REAL data from RenderService::Frame::hist via
 *  arstro::Histogram::toLog/toLinear -- the point of wiring cosmo's engine in
 *  rather than leaving the mock's fake curve.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "../../ImageProcessing/src/analysis/Histogram.h"
#include <functional>

namespace arstro
{
namespace cosmo_v2
{
    class HistogramWidget : public artboard::Segment
    {
    public:
        static constexpr double kHeight = 94.325;

        HistogramWidget();

        void setHistogram(const HistogramData &data) { mData = data; mHasData = true; }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        HistogramData mData;
        bool mHasData = false;
    };
}
}
