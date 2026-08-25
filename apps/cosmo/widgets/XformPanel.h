/*
 *  cosmo_v2 by arstro — XformPanel (App.tsx XformPanel): a rotation readout +
 *  quarter-turn buttons + reset, an aspect-ratio chip row, and Flip/Auto
 *  button rows. The engine has no flip or auto-horizon/geometry API today
 *  (EditEngine exposes crop/rotate/quarterTurns only), so those buttons are
 *  drawn pixel-exact but left unwired rather than faking behavior the engine
 *  can't perform -- see the onClick comments. The continuous rotation
 *  readout has no drag/dial control in the source (only quarter-turn
 *  buttons + a static example readout), so it's made horizontally
 *  scrubbable here as the least-invented way to set an arbitrary angle.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include "CropGeometry.h"
#include "PillButton.h"
#include "IconButton.h"
#include <functional>
#include <memory>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class XformPanel : public artboard::Segment
    {
    public:
        // Free, 1:1, 4:3, 16:9, 3:2, 5:4, Custom (R-CROP-4).
        static constexpr int kAspectCount = 7;
        static constexpr int kAspectFree = 0;
        static constexpr int kAspectCustom = 6;

        struct State
        {
            float rotation = 0;
            int quarterTurns = 0;
            float cropX = 0, cropY = 0, cropW = 1, cropH = 1;
            /** The photo's full-resolution shape, so a pixel aspect ratio can be turned into a
             *  normalised crop (R-CROP-1). {0,0} means unknown, and the ratio chips then have
             *  nothing honest to compute — so they say so by doing nothing rather than
             *  guessing a square, which is what the old code did. */
            int sourceWidth = 0, sourceHeight = 0;
        };

        XformPanel();

        void setState(const State &s);
        void layout();
        void advance(double nowMs) override;

        std::function<void(double)> onRotationChange;
        std::function<void(int)> onQuarterTurn;  // +1 or -1
        std::function<void()> onResetRotation;
        std::function<void(double x, double y, double w, double h)> onCropChange;
        /** The ratio lock changed: `pixelRatio` is w/h, or 0 for Free (R-CROP-2). Reported
         *  separately from the crop because it is a MODE, not a value — Free must be able to
         *  unlock without touching the rectangle, which is precisely what it could not do. */
        std::function<void(double pixelRatio)> onAspectLockChange;

        /** The locked pixel ratio, or 0 when Free. */
        double lockedRatio() const;
        /** Where aspect chip `i` is, in widget-local coords. Public for the same reason
         *  `CurvePanel::plotBox()` is: a test or a shot that aims at a chip must ask the widget
         *  where it drew it, or it ends up aiming where the widget no longer does. */
        artboard::Rect aspectChipRect(int i) const;
        int aspectSelected() const { return mAspectSelected; }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        artboard::Rect mReadoutRect;
        std::shared_ptr<PillButton> mMinus90, mPlus90;
        std::shared_ptr<IconButton> mResetBtn;
        std::vector<std::shared_ptr<PillButton>> mAspectChips;
        std::shared_ptr<PillButton> mFlipH, mFlipV;
        std::shared_ptr<PillButton> mAutoHorizon, mAutoGeometry;

        State mState;
        int mAspectSelected = kAspectFree;
        /** The Custom chip's ratio, w:h. Remembered for the session so re-picking Custom does
         *  not forget what the photographer typed (R-CROP-4). */
        double mCustomW = 16.0, mCustomH = 10.0;
        std::shared_ptr<artboard::TextBox> mCustomWField, mCustomHField;
        double mCustomRowY = 0.0;
        bool mCustomRowVisible() const { return mAspectSelected == kAspectCustom; }
        /** Apply the currently selected ratio to the crop already there, keeping its centre,
         *  and report both the new rectangle and the lock (R-CROP-3). */
        void applySelectedRatio();
        bool mDraggingReadout = false;
        double mDragStartX = 0.0;
        float mDragStartRotation = 0.0f;

        double mAspectHeaderY = 0.0, mFlipHeaderY = 0.0, mAutoHeaderY = 0.0;
    };
}
}
