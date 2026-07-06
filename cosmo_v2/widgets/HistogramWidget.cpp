#include "HistogramWidget.h"
#include "TextMetrics.h"
#include "../Theme.h"
#include <algorithm>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace
    {
        constexpr double kLabelRowH = 19.825;  // 9px*1.3 line height + pt-2(6.5) + pb-0.5(1.625)
        constexpr double kPlotH = 68.0;         // literal (SVG H=68)
        constexpr double kPadX = 9.75;
    }

    HistogramWidget::HistogramWidget() { height.set(kHeight); }

    void HistogramWidget::onPaint(IRenderTarget &t) const
    {
        const double w = width.value();
        drawRoundedRect(t, Rect{0, 0, w, height.value()}, 0.0, Paint::filled(palette::histogramBg()));
        t.beginPath(); t.moveTo(0, height.value()); t.lineTo(w, height.value());
        t.setStroke(palette::border(), 1.0); t.strokePath();

        // Just the section label -- the Log/Linear toggle was removed (task point 7);
        // the plot always uses the log scale (steadier for photographic data).
        t.setFill(palette::mutedForeground());
        t.drawText("HISTOGRAM", kPadX, kLabelRowH * 0.5 + 9.0 * 0.35 + 3.0, 9.0, font::sansSemiBold(), 0.12 * 9.0);

        if (!mHasData) return;

        float rows[4][HistogramData::kBins];
        Histogram::toLog(mData, rows);

        const double plotY = kLabelRowH;
        const int n = HistogramData::kBins;
        auto plotFill = [&](const float *row, const Color &fill) {
            t.beginPath();
            t.moveTo(0, plotY + kPlotH);
            for (int i = 0; i < n; ++i)
            {
                const double x = w * (double)i / (n - 1);
                const double y = plotY + kPlotH - kPlotH * 0.92 * std::min(1.0f, row[i]);
                t.lineTo(x, y);
            }
            t.lineTo(w, plotY + kPlotH);
            t.closePath();
            t.setFill(fill);
            t.fillPath();
        };
        plotFill(rows[0], Color{1.0, 68 / 255.0, 68 / 255.0, 0.28});
        plotFill(rows[1], Color{52 / 255.0, 211 / 255.0, 80 / 255.0, 0.28});
        plotFill(rows[2], Color{60 / 255.0, 120 / 255.0, 1.0, 0.32});

        t.beginPath();
        for (int i = 0; i < n; ++i)
        {
            const double x = w * (double)i / (n - 1);
            const double y = plotY + kPlotH - kPlotH * 0.92 * std::min(1.0f, rows[3][i]);
            if (i == 0) t.moveTo(x, y); else t.lineTo(x, y);
        }
        t.setStroke(Color{1, 1, 1, 0.32}, 0.75);
        t.strokePath();
    }
}
}
