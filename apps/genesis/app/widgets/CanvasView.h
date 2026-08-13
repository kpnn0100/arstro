/*
 *  Genesis — CanvasView: the live preview stage.
 *
 *  Shows the REAL component (Runtime builds the authored base class and its shape tree),
 *  inside a frame the author can drag to any size — because a component that only works at
 *  its design size is broken, and the only way to know is to resize it here.
 *
 *  Also carries the transport: the base-specific way to drive the component (start/stop a
 *  loop, push progress, press a button), plus a reduced-motion switch, because every
 *  authored component has to be checked with motion off too.
 */
#pragma once
#include "../Theme.h"
#include "Panel.h"
#include <artboard/artboard.h>
#include <memory>
#include <vector>

namespace genesis
{
namespace ui
{
    class App;

    class CanvasView : public artboard::Segment
    {
    public:
        explicit CanvasView(App &app);
        void layout(double w, double h);
        void refresh();          // the document changed: rebuild the transport for its base
        void advance(double nowMs) override;
        void render(artboard::IRenderTarget &t,
                    const artboard::Transform &parent = artboard::Transform::identity()) const override;

        /** The preview frame's current size, in component pixels. */
        double frameW() const { return mFrameW.value(); }
        double frameH() const { return mFrameH.value(); }
        void setFrameSize(double w, double h);
        /** The preview's simulated-hover switch — the same state the Hover checkbox holds,
         *  exposed so a shortcut or a headless test can drive it. */
        void setHoverSim(bool on);
        bool hoverSim() const;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        void onOverlay(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &localPoint) override;

    private:
        artboard::Rect stageRect() const;
        artboard::Rect frameRect() const;
        artboard::Point handlePos() const;

        App &mApp;
        double mNowMs = 0.0;
        artboard::Property mFrameW{160.0};
        artboard::Property mFrameH{160.0};
        bool mResizing = false;
        double mResizeW0 = 0, mResizeH0 = 0;
        artboard::Property mHandleGlow{0.0};
        artboard::Property mSelectFade{0.0};

        std::vector<std::shared_ptr<artboard::Button>> mTransport;
        std::shared_ptr<artboard::Slider> mDrive;      // progress / slider value
        std::shared_ptr<artboard::Checkbox> mReduced;
        std::shared_ptr<artboard::Checkbox> mHoverSim;
    };
}
}
