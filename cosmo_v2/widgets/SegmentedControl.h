/*
 *  cosmo_v2 by arstro — SegmentedControl: N labeled segments in a row, one
 *  selected at a time, with a SINGLE animated highlight rectangle that slides
 *  between segments (rather than each segment fading its own fill in/out). The
 *  Figma design reuses this exact shape for the before/split/after switch
 *  (PhotoCanvas), the RGB/R/G/B tone-curve channel picker, the Hue/Sat/Lum
 *  mixer sub-tabs, and the Shadows/Midtones/Highlights grade region picker.
 *
 *  The highlight rounds only the corners that touch the container edge: the
 *  first segment rounds its LEFT corners, the last its RIGHT corners, interior
 *  segments stay square (`edgeRadius` for the outer corners, activeSegBox's
 *  cornerRadius for interior ones). That gives the before/split/after look
 *  (before = left-rounded, split = square, after = right-rounded) for free,
 *  and neatly nests the highlight inside a rounded tray for the pickers.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "PillButton.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class SegmentedControl : public artboard::Segment
    {
    public:
        explicit SegmentedControl(std::vector<std::string> labels);

        void setSelected(int index);          // animates the highlight to `index`
        void setSelectedImmediate(int index);  // snaps (no animation) -- for programmatic sync
        int selected() const { return mSelected; }
        std::function<void(int)> onChange;

        artboard::BoxStyle containerBox;                 // outer tray/pill background
        artboard::BoxStyle idleSegBox, activeSegBox;      // activeSegBox.paint = highlight fill
        artboard::TextStyle idleText, activeText;
        double padding = 2.0;   // inset between the container edge and the segment row
        double gap = 2.0;       // gap between segments
        /** Radius applied to the highlight's outer corners on the first/last
         *  segment (the ones that touch the container edge). <0 => use
         *  activeSegBox.cornerRadius for every corner (uniform). */
        double edgeRadius = -1.0;

        void layout();  // call after width/height changes
        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        void cornerRadii(int seg, double &tl, double &tr, double &br, double &bl) const;

        std::vector<std::shared_ptr<PillButton>> mSegs;
        int mSelected = 0;
        int mPendingTarget = -1;          // slot to animate toward on the next advance()
        artboard::AnimatedProperty mHiPos;  // continuous slot index (0..n-1)
        bool mInit = false;
    };
}
}
