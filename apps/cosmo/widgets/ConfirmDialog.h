/*
 *  cosmo_v2 by arstro — ConfirmDialog: a small modal confirm/choose prompt with a
 *  title, a message line, and a right-aligned row of buttons (each optionally
 *  `primary` = accent or `destructive` = red). Same modal chrome + fade as the
 *  other dialogs (R-G-1). Used for the "save or discard?" prompt when leaving an
 *  edited project for the home screen (R-HOME). Clicking outside the card cancels.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
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

        /** Press a button by index, as a click would — runs its action and closes.
         *  Public so the keyboard can reach it: a modal that can only be answered with a mouse
         *  is not answerable by everyone, and it is also how a headless test answers one. */
        bool activate(int index);
        /** Answer with the CANCEL button — the one that is neither primary nor destructive, which
         *  is what Escape means. Falls back to simply closing if the dialog has no such button. */
        bool cancel();
        /** Answer with the PRIMARY button, which is what Enter means. False if there is none. */
        bool confirmDefault();
        /** Answer with the DESTRUCTIVE button. Deliberately NOT bound to a key — "discard my work"
         *  should cost a deliberate click — but reachable so a test can take that branch. */
        bool confirmDestructive();
        int buttonCount() const { return (int)mButtons.size(); }

        /** Handle a key while open: Escape cancels, Enter/Return takes the primary action.
         *  Returns true when the key was consumed, so the caller stops routing it. */
        bool handleKey(const artboard::KeyEvent &e);

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
