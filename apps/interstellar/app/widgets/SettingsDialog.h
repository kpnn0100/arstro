/*
 *  interstellar_v1 — SettingsDialog: the Home sidebar's Settings (R-UI-1), one honest setting.
 *
 *  The model and the command grammar carry no preferences yet, so this dialog holds the one
 *  setting that is the front end's own business and that the design law says every app must make
 *  reachable: REDUCE MOTION (design rule §2.6 — an accessibility switch nobody can turn on is not a
 *  feature). It calls `artboard::setReducedMotion`, which every tween in the app already honours
 *  through the primitives. Cosmo's modal skeleton: eased appear, scrim, card, modal capture as
 *  `mOpen && !mClosing`, click-outside and Escape close.
 */
#pragma once
#include "../Theme.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <memory>

namespace arstro
{
namespace interstellar_v1
{
    class SettingsDialog : public artboard::Segment
    {
    public:
        SettingsDialog();
        void show();
        bool isOpen() const { return mOpen && !mClosing; }
        double appearAmount() const { return mAppear.value(); }
        bool handleKey(const artboard::KeyEvent &e);
        void close() { beginClose(); }
        std::shared_ptr<artboard::ToggleSwitch> reduceMotion() { return mToggle; }
        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &) const override { return mOpen && !mClosing; }

    private:
        artboard::Rect cardRect() const;
        artboard::Rect closeRect() const;
        void beginClose();
        std::shared_ptr<artboard::ToggleSwitch> mToggle;
        bool mOpen = false, mClosing = false, mStartPending = false;
        double mLastMs = 0.0;
        artboard::AnimatedProperty mAppear{0.0};
        cosmo_v2::HoverFade mHover;
    };
}
}
