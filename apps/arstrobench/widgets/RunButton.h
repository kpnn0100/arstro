/*
 *  Arstrobench by arstro — RunButton: the app's one primary action.
 *
 *  A filled accent pill with the framework's eased hover treatment (R-G-3) and an eased
 *  press wash (R-G-1) -- nothing about it flips in a single frame. Disabled while a run
 *  is in flight; the framework's animated disabledAmount() fades it there rather than
 *  greying it instantly (R-UI-7).
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include <functional>
#include <string>

namespace arstro
{
namespace arstrobench
{
    class RunButton : public artboard::Segment
    {
    public:
        RunButton();

        /** Cross-fades to the new label (fade out, swap at the trough, fade in) so the
         *  caption never pops (R-G-1). A no-op when the label is already current. */
        void setLabel(const std::string &label, double nowMs);
        const std::string &label() const { return mLabel; }

        std::function<void()> onClick;

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        std::string mLabel = "Run benchmark";
        std::string mPending;               ///< label swapped in at the fade trough
        artboard::Property mPress{0.0};     ///< eased press wash, 0..1
        artboard::Property mLabelFade{1.0}; ///< label cross-fade, 0..1
        double mNowMs = 0.0;                ///< last frame time, so a gesture can start a tween
    };
}
}
