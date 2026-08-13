/*
 *  cosmo_v2 by arstro — SectionHeader: the "TONE" / "COLOUR" / ... group label
 *  used inside every param panel (App.tsx SectionHeader: 9px SemiBold
 *  uppercase, tracking 0.13em, + a hairline divider filling the rest of the
 *  row). Stateless, so it's a free function rather than a Segment.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include "TextMetrics.h"
#include "../Theme.h"

namespace arstro
{
namespace cosmo_v2
{
    // pt-3.5(11.375) + ~9px*1.3 line height + pb-1.5(4.875)
    inline constexpr double kSectionHeaderHeight = 27.95;

    /** Draws at the given top `y` (local space) and returns kSectionHeaderHeight
     *  so callers can advance their own layout cursor by the return value. */
    inline double drawSectionHeader(artboard::IRenderTarget &t, double x, double y, double w, const std::string &label)
    {
        constexpr double kPreH = 11.375, kFontPx = 9.0, kGap = 6.5;
        const double baseline = y + kPreH + kFontPx * 0.85;
        t.setFill(palette::mutedForeground());
        t.drawText(label, x, baseline, kFontPx, font::sansSemiBold(), 0.13 * kFontPx);
        const double lineY = baseline - kFontPx * 0.35;
        const double labelW = estimateTextWidth(label, kFontPx) + kFontPx * label.size() * 0.13;  // + tracking
        t.beginPath();
        t.moveTo(x + labelW + kGap, lineY);
        t.lineTo(x + w, lineY);
        t.setStroke(palette::border(), 1.0);
        t.strokePath();
        return kSectionHeaderHeight;
    }
}
}
