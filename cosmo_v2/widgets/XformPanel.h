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
#include "../../Artboard/include/artboard/artboard.h"
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
        static constexpr int kAspectCount = 6;

        struct State
        {
            float rotation = 0;
            int quarterTurns = 0;
            float cropX = 0, cropY = 0, cropW = 1, cropH = 1;
        };

        XformPanel();

        void setState(const State &s);
        void layout();

        std::function<void(double)> onRotationChange;
        std::function<void(int)> onQuarterTurn;  // +1 or -1
        std::function<void()> onResetRotation;
        std::function<void(double x, double y, double w, double h)> onCropChange;

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
        int mAspectSelected = 0;  // "Free"
        bool mDraggingReadout = false;
        double mDragStartX = 0.0;
        float mDragStartRotation = 0.0f;

        double mAspectHeaderY = 0.0, mFlipHeaderY = 0.0, mAutoHeaderY = 0.0;
    };
}
}
