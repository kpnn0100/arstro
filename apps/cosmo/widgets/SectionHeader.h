/*
 *  cosmo_v2 by arstro — SectionHeader: the "TONE" / "COLOUR" / ... group label
 *  used inside every param panel (App.tsx SectionHeader: 9px SemiBold
 *  uppercase, tracking 0.13em, + a hairline divider filling the rest of the
 *  row). Stateless, so it's a free function rather than a Segment.
 *
 *  It is a ROW, and the rule is its flexible member (R-G-4): a fixed-width label,
 *  a rule that takes whatever is left, and an optional fixed-width trailing
 *  control. It used to be "label, then a line to the full width", with the
 *  section's tool button right-aligned inside that same span — so the hairline
 *  ran straight under the COLOUR section's eyedropper and under its hover box
 *  (D-58). A rule crossing a hoverable control makes the control read as
 *  decoration, which is R-G-3's mistake seen from the other side.
 *
 *  One code path, not two: a header with no trailing control is the same row with
 *  a zero-width third member.
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

    /** The box a section's optional trailing control occupies — the square the eyedropper sits
     *  in today (R-WB-1). One constant, read by the layout that places the button AND by the
     *  paint that stops the rule short of it (R-G-4): two numbers here would drift, and the
     *  symptom of the drift would be a hairline one pixel into a hover box. */
    inline constexpr double kSectionActionSize = kSectionHeaderHeight - 2.0;

    /** Draws at the given top `y` (local space) and returns kSectionHeaderHeight
     *  so callers can advance their own layout cursor by the return value.
     *
     *  `trailingW` is the width the caller has already reserved at the RIGHT end of the row
     *  for its own control — the section tool button, in the one case that has one. The rule
     *  stops a gap short of it (R-G-4). 0 means the row has no third member and the rule runs
     *  the full width, which is every other header in the app. */
    inline double drawSectionHeader(artboard::IRenderTarget &t, double x, double y, double w,
                                    const std::string &label, double trailingW = 0.0)
    {
        constexpr double kPreH = 11.375, kFontPx = 9.0, kGap = 6.5;
        const double baseline = y + kPreH + kFontPx * 0.85;
        t.setFill(palette::mutedForeground());
        t.drawText(label, x, baseline, kFontPx, font::sansSemiBold(), 0.13 * kFontPx);
        const double lineY = baseline - kFontPx * 0.35;
        const double labelW = estimateTextWidth(label, kFontPx) + kFontPx * label.size() * 0.13;  // + tracking
        // The three members, left to right: label (its text's width), rule (what is left),
        // trailing control (its box). Reserving the whole BOX and not just the glyph is
        // deliberate — the button's hover wash is as wide as the box, and a hairline crossing
        // that wash is the same visual bug one pixel further out.
        const double ruleStart = x + labelW + kGap;
        const double ruleEnd = x + w - (trailingW > 0.0 ? trailingW + kGap : 0.0);
        // A header narrow enough that the two fixed members meet simply has no rule, rather
        // than a rule drawn backwards (R5: the text is what must survive, not the decoration).
        if (ruleEnd > ruleStart)
        {
            t.beginPath();
            t.moveTo(ruleStart, lineY);
            t.lineTo(ruleEnd, lineY);
            t.setStroke(palette::border(), 1.0);
            t.strokePath();
        }
        return kSectionHeaderHeight;
    }
}
}
