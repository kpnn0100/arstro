#include "RunButton.h"
#include "../Theme.h"

namespace arstro
{
namespace arstrobench
{
    using namespace artboard;

    namespace
    {
        constexpr double kPressMs = motion::kDurationShort2;   // 100 ms down/up wash
        constexpr double kFadeOutMs = motion::kDurationShort2; // label cross-fade halves
        constexpr double kFadeInMs = motion::kDurationShort3;
        constexpr double kPressWash = 0.22;                    // how far the fill darkens at full press
    }

    RunButton::RunButton() { focusable = false; }

    void RunButton::setLabel(const std::string &label, double nowMs)
    {
        if (label == mLabel || label == mPending) return;
        mPending = label;
        // Fade the caption out, swap it where nothing is visible, fade the new one in.
        mLabelFade.animate(Tween::range(mLabelFade.value(), 0.0, kFadeOutMs)
                               .withEasing(Easing::EaseInQuad),
                           nowMs, [this, nowMs] {
                               mLabel = mPending;
                               mPending.clear();
                               mLabelFade.animate(Tween::range(0.0, 1.0, kFadeInMs)
                                                      .withEasing(Easing::EaseOutCubic),
                                                  nowMs + kFadeOutMs);
                           });
    }

    void RunButton::advance(double nowMs)
    {
        Segment::advance(nowMs);
        mNowMs = nowMs;  // gestures carry no timestamp; the frame clock is the one we have
        mPress.update(nowMs);
        mLabelFade.update(nowMs);
    }

    bool RunButton::handleGesture(const Gesture &g, const Point &local)
    {
        // A disabled button consumes nothing -- the gesture belongs to whatever is behind it.
        if (!enabled) return false;
        switch (g.type)
        {
        case Gesture::Type::Down:
            mPress.animate(Tween::range(mPress.value(), 1.0, kPressMs).withEasing(Easing::EaseOutCubic), mNowMs);
            return true;
        case Gesture::Type::Up:
        case Gesture::Type::Drop:
            mPress.animate(Tween::range(mPress.value(), 0.0, kPressMs).withEasing(Easing::EaseOutCubic), mNowMs);
            return true;
        case Gesture::Type::Click:
            if (onClick) onClick();
            return true;
        default:
            return Segment::handleGesture(g, local);
        }
    }

    void RunButton::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        const double hv = hoverAmount(), press = mPress.value(), dis = disabledAmount();

        // One shared hover treatment (artboard::hoverBox) so this reads identically to
        // every other Arstro control; the press darkens on top of it, and the framework's
        // disabled fade dims the result.
        BoxStyle box{Paint::filled(palette::primary()), radius::control()};
        box = hoverBox(box, palette::white(), hv);
        box.paint.fill = lerpColor(box.paint.fill, palette::background(), kPressWash * press);
        box = dimBox(box, dis);
        drawRoundedRect(t, Rect{0, 0, w, h}, box.cornerRadius, box.paint);

        const std::string &text = mLabel;
        const double size = 11.0;
        const double tw = t.measureText(text, size, font::sansMedium());
        Color ink = palette::primaryForeground();
        ink = dimColor(ink, dis);
        ink.a *= mLabelFade.value();
        t.setFill(ink);
        t.drawText(text, (w - tw) * 0.5, h * 0.5 + size * 0.36, size, font::sansMedium());
    }
}
}
