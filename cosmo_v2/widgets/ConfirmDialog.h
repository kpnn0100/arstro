/*
 *  cosmo_v2 by arstro — ConfirmDialog: a small modal confirm/choose prompt with a
 *  title, a message line, and a right-aligned row of buttons (each optionally
 *  `primary` = accent or `destructive` = red). Same modal chrome + fade as the
 *  other dialogs (R-G-1). Used for the "save or discard?" prompt when leaving an
 *  edited project for the home screen (R-HOME). Clicking outside the card cancels.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "HoverFade.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class ConfirmDialog : public artboard::Segment
    {
    public:
        struct Button
        {
            std::string label;
            bool destructive = false;   // red (e.g. Discard)
            bool primary = false;       // accent (e.g. Save)
            std::function<void()> onClick;
        };

        explicit ConfirmDialog(const artboard::Color &accent);

        void show(const std::string &title, const std::string &message, std::vector<Button> buttons);
        bool isOpen() const { return mOpen && !mClosing; }
        /** Snap shut immediately (used when leaving the editor mid-prompt). */
        void close() { mOpen = false; mClosing = false; mAppear.set(0.0); }

    protected:
        void advance(double nowMs) override;
        void onOverlay(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return mOpen && !mClosing; }

    private:
        void beginClose();
        artboard::Rect cardRect() const;
        void buttonRects(std::vector<artboard::Rect> &out) const;  // right-aligned footer
        int buttonAt(const artboard::Point &local) const;

        artboard::Color mAccent;
        bool mOpen = false;
        bool mClosing = false;
        double mLastMs = 0.0;
        artboard::AnimatedProperty mAppear{0.0};
        std::string mTitle, mMessage;
        std::vector<Button> mButtons;
        HoverFade mHover;  // per-button hover cross-fade (R-G-3)
    };
}
}
