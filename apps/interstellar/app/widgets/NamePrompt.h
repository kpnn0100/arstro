/*
 *  interstellar_v1 — NamePrompt: a one-field modal ("New version…") in cosmo's modal skeleton.
 *
 *  `timeline new <name> --base <tl>` needs a name, and a name needs a field. Cosmo's
 *  `ConfirmDialog` (reused as-is for "save changes?") has buttons but no field, so this is the
 *  same skeleton with one: an eased appear amount (150 ms in, 120 ms out), a scrim, a card on the
 *  8/16/32 modal rhythm, modal capture as `mOpen && !mClosing` (gotcha 2 — never a bare `true`),
 *  click-outside cancels, Escape cancels, Enter confirms, and the dialog CLOSES BEFORE its action
 *  runs (design rule §5), because the action may open something else.
 *
 *  Painted in `onPaint`, not `onOverlay`, on purpose: the field is a real artboard `TextBox`
 *  CHILD (caret, selection, clipboard), children draw after their parent's onPaint, and a card
 *  drawn in the overlay pass would land on top of its own text field. The prompt is the last
 *  child of the edit root, so its onPaint already runs after every sibling's body. Every colour
 *  is multiplied by the appear amount, and the field's own opacity follows it, so the whole
 *  modal fades as one.
 */
#pragma once
#include "../Theme.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <functional>
#include <memory>
#include <string>

namespace arstro
{
namespace interstellar_v1
{
    class NamePrompt : public artboard::Segment
    {
    public:
        NamePrompt();

        void show(const std::string &title, const std::string &message, const std::string &initial,
                  const std::string &confirmLabel, std::function<void(const std::string &)> onConfirm);
        bool isOpen() const { return mOpen && !mClosing; }
        double appearAmount() const { return mAppear.value(); }

        /** Escape cancels, Enter confirms, everything else goes to the field. Consumes every key
         *  while open — a modal that lets keys through is a modal in name only. */
        bool handleKey(const artboard::KeyEvent &e);
        void confirm();
        void cancel();
        std::shared_ptr<artboard::TextBox> field() { return mField; }

        void advance(double nowMs) override;
        void layout();

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &) const override { return mOpen && !mClosing; }

    private:
        artboard::Rect cardRect() const;
        artboard::Rect buttonRect(int i) const;   // 0 = cancel, 1 = confirm
        void beginClose();

        std::shared_ptr<artboard::TextBox> mField;
        std::string mTitle, mMessage, mConfirmLabel;
        std::function<void(const std::string &)> mOnConfirm;
        bool mOpen = false, mClosing = false, mStartPending = false;
        double mLastMs = 0.0;
        artboard::AnimatedProperty mAppear{0.0};
        cosmo_v2::HoverFade mHover;
    };
}
}
