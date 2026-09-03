/*
 *  cosmo_v2 by arstro — MaskPanel: five "add mask" chips, a ComboBox to pick
 *  WHICH mask to edit, its invert/delete controls, a feather slider, then
 *  Basic-style tone/colour/presence sliders scoped to that mask's LocalAdjust.
 *
 *  The fifth chip is Detect (R-AISEG-10): a mask with no geometry, whose region
 *  is found in the pixels. Selecting one opens the DetectBlock below — a subject
 *  picker, a line saying what THAT subject is found by, and Sensitivity.
 *
 *  The per-mask sliders are wired (via onFeatherChange / onAdjustChange) so
 *  editing a mask actually writes its `adjust` — without a non-identity adjust
 *  the engine's applyMaskStack skips the mask entirely (MaskStack: identity ->
 *  skipped), which is why masks previously had no visible effect. The mask
 *  picker mirrors cosmo's MaskPanel ComboBox.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include "../../../core/ImageProcessing/src/engine/EditParams.h"
#include "PillButton.h"
#include "SegmentedControl.h"
#include "IconButton.h"
#include "SliderRow.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    /** What the panel needs to know about a detection to draw the block (R-AISEG-26/27/28).
     *
     *  Pushed in from `RightColumn`, which reads it off `AppModel` — the view holds no detection
     *  state of its own beyond the eased values it draws with (R-SVC-4). */
    struct DetectStatus
    {
        bool running = false;
        double fraction = 0.0;      // 0..1, the SERVICE's number; the bar eases toward it
        std::string stage;          // "colour" | "regions" | … — a stable name from Detection.cpp
        /** The selected mask's OWN region count (R-AISEG-28). Off the mask and not off the last
         *  detection's report, so it is still right after a project is reopened — at which point
         *  nothing has been detected this session but the mask is exactly what it was. */
        int regions = 0;
        /** True when the last detection was for THIS mask — which is the only way to tell
         *  "looked and found nothing" from "never run" (R-AISEG-26). */
        bool ranForThisMask = false;
        bool handled = true;        // false = nobody has a detector for this subject
        double coverage = 0.0;      // 0..1 of the frame, from the last detection
    };

    /** The Detect block: caption, Sensitivity, a Detect button, a progress bar and a line saying
     *  what happened — under its own section header.
     *
     *  A Segment of its own, and CLIPPED, because R-G-1 has no exemption for "the selected mask
     *  changed kind" (R-AISEG-12). Its height animates 0 -> kHeight while its children keep their
     *  real positions, so the block slides out from under the Feather row and everything below
     *  moves in step. The alternative — flipping the rows visible and letting Tone jump down a
     *  frame later — is the snap this project keeps re-shipping under a different name.
     *
     *  It draws its own section header, the caption, the status line and the progress bar rather
     *  than letting MaskPanel paint them, because anything the parent painted would not be
     *  clipped with the block and would hang in the air at every intermediate height.
     *
     *  There is no subject picker (R-AISEG-25): there is one subject, and a control with one
     *  option looks like a choice, invites a click and does nothing. */
    class DetectBlock : public artboard::Segment
    {
    public:
        static constexpr double kHeaderH = 27.95;      // == kSectionHeaderHeight
        static constexpr double kCaptionH = 13.0;
        static constexpr double kCaptionMB = 4.875;
        static constexpr double kButtonH = 22.75;
        static constexpr double kButtonMB = 4.875;
        /** The progress track: one spacing unit tall, in a row that is ALWAYS laid out. What
         *  changes is its opacity (R-AISEG-27) — growing the block instead would move everything
         *  below it twice for one button press. */
        static constexpr double kBarH = 3.25;
        static constexpr double kBarMB = 6.5;
        DetectBlock() { clipToBounds = true; }
        std::string caption;   // what the detector keys on (R-AISEG-11)
        /** Which segmenter is answering (R-AISEG-15) — the built-in's honest description of
         *  itself, or an installed model's name. */
        std::string header = "Detect skin (colour & texture)";
        /** The status sentence, one of the five R-AISEG-26 names. Composed by MaskPanel, drawn
         *  here, because it has to be clipped with everything else. */
        std::string status;
        /** How much of the bar is filled and how visible it is — both LIVE eased values, written
         *  by `MaskPanel::advance`. The bar is drawn from these and never from the target, which
         *  is what makes it a bar rather than five steps (R-AISEG-27). */
        double barFill = 0.0;
        double barShow = 0.0;
        /** Where the button ends, so the status text starts after it and is ellipsized against
         *  the space that is actually left (R5). */
        double statusX = 0.0;
    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
    };

    class MaskPanel : public artboard::Segment
    {
    public:
        MaskPanel();

        /** Refresh from the current image's masks + which one is focused
         *  (-1 = none). Does not fire callbacks. */
        void setMasks(const std::vector<MaskParams> &masks, int selected);
        /** Name the segmenter deciding these masks: "built-in", or a model's name. */
        void setSegmenter(const std::string &name);
        /** R-AISEG-26/27/28: what the detection is doing, or last did. Does not fire callbacks,
         *  and starts no tween — `advance()` does that, because a setter has no clock. */
        void setDetectStatus(const DetectStatus &s);
        void layout();
        void scrollBy(double delta);

        std::function<void(int type)> onAddMask;   // MaskParams::Type
        std::function<void(int index)> onSelectMask;  // choose which mask to edit
        std::function<void()> onToggleInvert;
        std::function<void()> onDeleteMask;
        std::function<void(double)> onFeatherChange;              // 0..1
        std::function<void(const LocalAdjust &)> onAdjustChange;  // selected mask's local adjust
        std::function<void(double)> onSensitivityChange;          // 0..1 (R-AISEG-5)
        std::function<void()> onDetect;                           // run it (R-AISEG-27)

        /** Where "add mask" chip `i` is, in widget-local coords (0 Radial, 1 Linear, 2 Brush,
         *  3 Draw, 4 Detect). Public for the same reason `XformPanel::aspectChipRect` is: a test
         *  or a shot that aims at a chip must ask the widget where it drew it, or it ends up
         *  aiming where the widget no longer does. */
        artboard::Rect addChipRect(int i) const;

        /** How far the Detect block is open, 0..1 — the LIVE eased value, not the target. Public
         *  so a test can assert the two differ mid-tween, which is the only kind of assertion
         *  that can tell an eased implementation from a snapping one (R-G-1, R-AISEG-12). */
        double detectOpenAmount() const { return mDetect ? mDetectOpen.value() : 0.0; }
        /** The same, for the two values the progress bar is drawn from (R-AISEG-27): how full it
         *  is and how visible. Public for the same reason — a bar that jumped to the service's
         *  fraction and a bar that eased toward it are the same still frame. */
        double detectBarFill() const { return mBarFill.value(); }
        double detectBarShow() const { return mBarShow.value(); }
        /** The status sentence currently on screen, so a test can assert the FIVE states of
         *  R-AISEG-26 rather than assert that something was drawn. */
        const std::string &detectStatusText() const;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        void advance(double nowMs) override;  // eases the scroll toward its target (R-G-1)

    private:
        static constexpr double kPadX = 9.75;

        std::vector<std::shared_ptr<PillButton>> mAddChips;
        std::shared_ptr<DetectBlock> mDetect;
        std::shared_ptr<PillButton> mDetectBtn;
        std::shared_ptr<SliderRow> mSensitivity;
        std::shared_ptr<artboard::ComboBox> mSelect;   // which mask to edit
        std::shared_ptr<PillButton> mInvBtn;
        std::shared_ptr<IconButton> mTrashBtn;
        std::shared_ptr<SliderRow> mFeather;
        std::shared_ptr<SliderRow> mExposure, mHighlights, mShadows;
        std::shared_ptr<SliderRow> mTemperature, mSaturation;
        std::shared_ptr<SliderRow> mDehaze;

        std::vector<MaskParams> mMasks;
        int mSelected = -1;
        LocalAdjust mEditing;   // working copy of the selected mask's adjust
        artboard::AnimatedProperty mScroll{0.0};
        double mScrollTarget = 0.0, mScrollLastTarget = 0.0;
        artboard::AnimatedProperty mDetectOpen{0.0};
        double mDetectTarget = 0.0, mDetectLastTarget = 0.0;
        // R-AISEG-27. Two properties and not one: the bar has to be able to finish filling while
        // it fades out, or the last thing a photographer sees of a detection is it vanishing at
        // 70% — which reads as a failure.
        artboard::AnimatedProperty mBarFill{0.0};
        artboard::AnimatedProperty mBarShow{0.0};
        double mBarFillTarget = 0.0, mBarFillLast = 0.0;
        double mBarShowTarget = 0.0, mBarShowLast = 0.0;
        DetectStatus mStatus;
        double mContentHeight = 0.0;

        // Cached header y's for onPaint: [0]="Add Mask", [1]="Tone", [2]="Colour",
        // [3]="Presence". The mask identity is now the ComboBox, not a header.
        double mHeaderY[4] = {0, 0, 0, 0};
        double mSelectRowY = 0.0;
    };
}
}
