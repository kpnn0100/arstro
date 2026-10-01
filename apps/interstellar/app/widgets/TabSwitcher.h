/*
 *  interstellar_v1 — TabSwitcher: the Edit page's [ Grade | Cut | Deliver ] (R-UI-3, ui-brief §3).
 *
 *  Cosmo's segmented-picker look, exactly — a segmentedBg tray with a hairline border, a solid
 *  accent highlight with white text — at top-bar scale. It is not cosmo's `SegmentedControl`
 *  itself for one reason: that class keeps its eased highlight position private, and the design
 *  law requires the LIVE eased value to be readable wherever a test must tell a tween from a
 *  snap. `highlightPos()` is that value; `selected()` is the target.
 *
 *  The highlight TRAVELS (220 ms, motion::kSlideMs) and each label's colour is derived every frame
 *  from how much of the highlight covers it, so the white label slides with the fill instead of
 *  switching when the fill arrives. Labels are measured (`measureText`) and centred in their
 *  segment; hover is a per-segment HoverFade cross-fade.
 */
#pragma once
#include "../Theme.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar_v1
{
    class TabSwitcher : public artboard::Segment
    {
    public:
        static constexpr double kHeight = 21.0;     // cosmo's MenuStrip height, inside the 29.25 bar
        static constexpr double kSegW = 64.0;       // fits "Deliver" at 10 px Medium with air
        static constexpr double kPad = 2.0;

        explicit TabSwitcher(std::vector<std::string> labels);

        /** Programmatic: records the target; `advance` starts the travel. Never fires onSelect. */
        void setSelected(int index);
        int selected() const { return mSelected; }
        /** The LIVE eased highlight position, in segment units (0 = first). */
        double highlightPos() const { return mPos.value(); }
        double preferredWidth() const { return kPad * 2 + kSegW * (double)mLabels.size(); }
        artboard::Rect segmentRect(int i) const { return artboard::Rect{kPad + i * kSegW, kPad, kSegW, kHeight - 2 * kPad}; }

        std::function<void(int)> onSelect;

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        int segmentAt(const artboard::Point &local) const;

        std::vector<std::string> mLabels;
        int mSelected = 0;
        bool mPending = false, mInit = false;
        artboard::AnimatedProperty mPos{0.0};
        cosmo_v2::HoverFade mHover;
    };
}
}
