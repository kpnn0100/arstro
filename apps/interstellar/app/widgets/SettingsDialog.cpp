#include "SettingsDialog.h"
#include "TextFit.h"
#include "../../../cosmo/widgets/Icons.h"

namespace arstro
{
namespace interstellar_v1
{
    using namespace artboard;

    namespace
    {
        constexpr double kCardW = 400.0, kCardH = 176.0, kPad = 16.0;
        Color fade(Color c, double a) { c.a *= a; return c; }
    }

    SettingsDialog::SettingsDialog()
    {
        // cosmo's toggle style, with the OFF track on `switchBackground`: on the popover surface the
        // default `secondary` track is 0x252525 on 0x222222 — a thumb floating on nothing.
        ToggleStyle ts = sharedTheme().toggle;
        ts.trackOff = {Paint::filled(palette::switchBackground()), radius::pill()};
        mToggle = std::make_shared<ToggleSwitch>(ts);
        mToggle->width.set(30.0);
        mToggle->height.set(16.0);
        mToggle->setOn(reducedMotion());
        mToggle->onChange = [](bool on) { setReducedMotion(on); };
        mToggle->opacity.set(0.0);
        addChild(mToggle);
    }

    void SettingsDialog::show()
    {
        mToggle->setOn(reducedMotion());
        mOpen = true;
        mClosing = false;
        mStartPending = true;
    }

    void SettingsDialog::beginClose()
    {
        if (!mOpen || mClosing) return;
        mClosing = true;
        mAppear.animateTo(0.0, motion::kModalCloseMs, Easing::EaseOutCubic, mLastMs);
    }

    bool SettingsDialog::handleKey(const KeyEvent &e)
    {
        if (!isOpen()) return false;
        if (e.type == KeyEvent::Type::Down && (e.keyCode == 27 || e.keyCode == 13)) beginClose();
        return true;
    }

    Rect SettingsDialog::cardRect() const { return Rect{(width.value() - kCardW) * 0.5, (height.value() - kCardH) * 0.5 - 24.0, kCardW, kCardH}; }
    Rect SettingsDialog::closeRect() const { const Rect c = cardRect(); return Rect{c.right() - kPad - 20.0, c.y + kPad - 3.0, 20.0, 20.0}; }

    void SettingsDialog::advance(double nowMs)
    {
        mLastMs = nowMs;
        if (mStartPending) { mAppear.animateTo(1.0, motion::kModalOpenMs, Easing::EaseOutCubic, nowMs); mStartPending = false; }
        mAppear.update(nowMs);
        if (mClosing && !mAppear.isAnimating() && mAppear.value() <= 0.001) { mOpen = false; mClosing = false; }
        const Rect c = cardRect();
        mToggle->x.set(c.right() - kPad - 30.0);
        mToggle->y.set(c.y + 78.0);
        mToggle->opacity.set(mAppear.value());
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    bool SettingsDialog::handleGesture(const Gesture &g, const Point &local)
    {
        if (!isOpen()) return false;
        if (g.type == Gesture::Type::Move) { mHover.setHovered(closeRect().contains(local) ? 0 : -1); return true; }
        if (g.type == Gesture::Type::Click && (closeRect().contains(local) || !cardRect().contains(local))) { beginClose(); return true; }
        return true;
    }

    void SettingsDialog::onPaint(IRenderTarget &t) const
    {
        const double a = mAppear.value();
        if (a <= 0.001) return;
        drawRoundedRect(t, Rect{0, 0, width.value(), height.value()}, 0.0, Paint::filled(surface::scrim(0.55 * a)));
        const Rect c = cardRect();
        drawRoundedRect(t, Rect{c.x, c.y + 6, c.w, c.h}, radius::control(), Paint::filled(Color{0, 0, 0, 0.35 * a}));
        drawRoundedRect(t, c, radius::control(), Paint::filledStroked(fade(palette::popover(), a), fade(palette::border(), a), 1.0));
        t.setFill(fade(palette::foreground(), a));
        t.drawText("Settings", c.x + kPad, c.y + kPad + 13.0, 14.0, font::sansSemiBold());
        const Rect cr = closeRect();
        const double hv = mHover.amount(0);
        if (hv > 0.001) drawRoundedRect(t, cr, radius::control(), Paint::filled(palette::hoverWash(hv * a)));
        cosmo_v2::icon::close(t, Rect{cr.x + 5, cr.y + 5, 10, 10}, fade(lerpColor(palette::mutedForeground(), palette::foreground(), hv), a));
        t.setStroke(fade(palette::border(), a), 1.0);
        t.beginPath(); t.moveTo(c.x + kPad, c.y + 52.0); t.lineTo(c.right() - kPad, c.y + 52.0); t.strokePath();
        t.setFill(fade(palette::mutedForeground(), a));
        t.drawText("ACCESSIBILITY", c.x + kPad, c.y + 70.0, 9.0, font::sansSemiBold(), 0.13 * 9.0);
        t.setFill(fade(palette::foreground(), a));
        t.drawText("Reduce motion", c.x + kPad, c.y + 90.0, 12.0, font::sans());
        t.setFill(fade(palette::mutedForeground(), a));
        const std::string d = "Transitions land at once instead of easing.";
        t.drawText(textfit::ellipsize(t, d, c.w - 2 * kPad - 44.0, 11.0, font::sans()), c.x + kPad, c.y + 108.0, 11.0, font::sans());
        t.drawText("Interface preferences live with this app, not in the project.", c.x + kPad, c.bottom() - kPad, 10.0, font::sans());
    }
}
}
