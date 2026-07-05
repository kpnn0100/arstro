/*
 *  cosmo_v2 by arstro — SegmentedControl: N labeled segments in a row, one
 *  selected at a time. The Figma design reuses this exact shape for the
 *  before/after pill (App.tsx center stage), the RGB/R/G/B tone-curve
 *  channel picker, the Hue/Sat/Lum mixer sub-tabs, and the Shadows/Midtones/
 *  Highlights grade region picker -- only the container/segment styling
 *  differs (a borderless full-pill with edge-to-edge segments for
 *  before/after vs. a bordered dark tray with 2px padding/gaps and
 *  individually-rounded segments for the others), so every knob is exposed
 *  as a public style field rather than this class assuming one look.
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

        void setSelected(int index);
        int selected() const { return mSelected; }
        std::function<void(int)> onChange;

        artboard::BoxStyle containerBox;                 // outer tray/pill background
        artboard::BoxStyle idleSegBox, activeSegBox;      // per-segment background
        artboard::TextStyle idleText, activeText;
        double padding = 2.0;   // inset between the container edge and the segment row
        double gap = 2.0;       // gap between segments

        void layout();  // call after width/height changes

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        std::vector<std::shared_ptr<PillButton>> mSegs;
        int mSelected = 0;
    };
}
}
