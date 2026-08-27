#include "ConfirmDialog.h"
#include "TextMetrics.h"
#include "../Theme.h"

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace
    {
        constexpr double kCardW = 400.0;
        constexpr double kPad = 20.0;
        constexpr double kTitleH = 26.0;
        constexpr double kMsgH = 22.0;
        constexpr double kBtnH = 28.0, kBtnGap = 8.0, kBtnPadX = 14.0;
        constexpr double kFooterGap = 22.0;
        constexpr double kFontPx = 12.0;

        inline Color fade(Color c, double a) { return Color{c.r, c.g, c.b, c.a * a}; }
    }

    ConfirmDialog::ConfirmDialog(const Color &accent) : mAccent(accent) {}

    void ConfirmDialog::show(const std::string &title, const std::string &message, std::vector<Button> buttons)
    {
        mTitle = title; mMessage = message; mButtons = std::move(buttons);
        mOpen = true; mClosing = false;
        mAppear.animateTo(1.0, 150.0, Easing::EaseOutCubic, mLastMs);
        raise();
    }

    bool ConfirmDialog::activate(int index)
    {
        if (!isOpen() || index < 0 || index >= (int)mButtons.size()) return false;
        // The action is copied out and the dialog closed FIRST: an action may open another modal
        // (Save can raise a file chooser), and running it while this one still considers itself
        // open would leave two dialogs believing they own the input.
        auto action = mButtons[index].onClick;
        beginClose();
        if (action) action();
        return true;
    }

    bool ConfirmDialog::cancel()
    {
        for (int i = 0; i < (int)mButtons.size(); ++i)
            if (!mButtons[i].primary && !mButtons[i].destructive) return activate(i);
        if (!isOpen()) return false;
        beginClose();   // no explicit cancel button: dismissing IS the cancel
        return true;
    }

    bool ConfirmDialog::confirmDefault()
    {
        for (int i = 0; i < (int)mButtons.size(); ++i)
            if (mButtons[i].primary) return activate(i);
        return false;
    }

    bool ConfirmDialog::confirmDestructive()
    {
        for (int i = 0; i < (int)mButtons.size(); ++i)
            if (mButtons[i].destructive) return activate(i);
        return false;
    }

    bool ConfirmDialog::handleKey(const artboard::KeyEvent &e)
    {
        if (!isOpen() || e.type != artboard::KeyEvent::Type::Down) return false;
        // Escape cancels and Enter confirms — the two bindings every modal has had for forty
        // years. The DESTRUCTIVE action is deliberately not bound: "discard my work" should cost
        // a deliberate click, not a stray Return on a dialog the user has not read.
        constexpr int kEsc = 0x1B, kReturn = 0x0D, kEnter = 0x0A;
        if (e.keyCode == kEsc) { cancel(); return true; }
        if (e.keyCode == kReturn || e.keyCode == kEnter)
        {
            if (!confirmDefault()) cancel();   // nothing primary: Enter dismisses
            return true;
        }
        // Swallow everything else: a modal that lets keys through to the editor behind it is a
        // modal in name only.
        return true;
    }

    void ConfirmDialog::beginClose()
    {
        mClosing = true;
        mAppear.animateTo(0.0, 120.0, Easing::EaseOutCubic, mLastMs);
    }

    void ConfirmDialog::advance(double nowMs)
    {
        mLastMs = nowMs;
        mAppear.update(nowMs);
        if (mClosing && !mAppear.isAnimating()) { mOpen = false; mClosing = false; }
        if (!isOpen()) mHover.clear();
        mHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    int ConfirmDialog::buttonAt(const Point &local) const
    {
        std::vector<Rect> rects; buttonRects(rects);
        for (size_t i = 0; i < rects.size() && i < mButtons.size(); ++i)
            if (rects[i].contains(local)) return (int)i;
        return -1;
    }

    Rect ConfirmDialog::cardRect() const
    {
        const double h = kPad + kTitleH + kMsgH + kFooterGap + kBtnH + kPad;
        const double x = (width.value() - kCardW) * 0.5;
        const double y = (height.value() - h) * 0.5;
        return Rect{x < 4 ? 4 : x, y < 4 ? 4 : y, kCardW, h};
    }

    void ConfirmDialog::buttonRects(std::vector<Rect> &out) const
    {
        out.clear();
        const Rect c = cardRect();
        const double y = c.y + c.h - kPad - kBtnH;
        double right = c.x + c.w - kPad;
        // Lay buttons right-to-left in declared order (last declared sits leftmost).
        for (auto it = mButtons.rbegin(); it != mButtons.rend(); ++it)
        {
            const double w = estimateTextWidth(it->label, kFontPx) + 2 * kBtnPadX;
            out.push_back(Rect{right - w, y, w, kBtnH});
            right -= w + kBtnGap;
        }
        // out is in reverse (rightmost first); reverse so out[i] matches mButtons[i].
        std::reverse(out.begin(), out.end());
    }

    bool ConfirmDialog::handleGesture(const Gesture &g, const Point &local)
    {
        if (!mOpen || mClosing) return false;
        if (g.type == Gesture::Type::Move) { mHover.setHovered(buttonAt(local)); return true; }
        if (g.type != Gesture::Type::Click) return true;

        if (!cardRect().contains(local)) { beginClose(); return true; }  // outside = cancel

        std::vector<Rect> rects; buttonRects(rects);
        for (size_t i = 0; i < rects.size() && i < mButtons.size(); ++i)
            if (rects[i].contains(local))
            {
                auto cb = mButtons[i].onClick;   // copy: callback may re-open a dialog
                beginClose();
                if (cb) cb();
                return true;
            }
        return true;  // inside card, no button
    }

    void ConfirmDialog::onOverlay(IRenderTarget &t) const
    {
        const double a = mAppear.value();
        if (!mOpen || a <= 0.001) return;

        drawRoundedRect(t, Rect{0, 0, width.value(), height.value()}, 0.0, Paint::filled(fade(Color{0, 0, 0, 0.55}, a)));

        const Rect c = cardRect();
        drawRoundedRect(t, c, radius::control(),
                        Paint::filledStroked(fade(palette::popover(), a), fade(palette::border(), a), 1.0));

        t.setFill(fade(palette::foreground(), a));
        t.drawText(mTitle, c.x + kPad, c.y + kPad + 16.0, 14.0, font::sansSemiBold());
        t.setFill(fade(palette::mutedForeground(), a));
        t.drawText(mMessage, c.x + kPad, c.y + kPad + kTitleH + 12.0, kFontPx, font::sans());

        std::vector<Rect> rects; buttonRects(rects);
        for (size_t i = 0; i < rects.size() && i < mButtons.size(); ++i)
        {
            const Button &b = mButtons[i];
            const Rect &r = rects[i];
            Color fill = b.destructive ? palette::destructive() : (b.primary ? mAccent : Color{0, 0, 0, 0});
            if (b.destructive || b.primary)
                drawRoundedRect(t, r, radius::control(), Paint::filled(fade(fill, a)));
            else
                drawRoundedRect(t, r, radius::control(), Paint::filledStroked(fade(palette::secondary(), a), fade(palette::border(), a), 1.0));
            // Hover: an eased white wash on the button under the pointer (fades with the dialog).
            const double hv = mHover.amount((int)i) * a;
            if (hv > 0.001)
                drawRoundedRect(t, r, radius::control(), Paint::filled(palette::hoverWash(hv)));
            const Color fg = (b.destructive || b.primary) ? palette::primaryForeground() : palette::foreground();
            t.setFill(fade(fg, a));
            const double tw = estimateTextWidth(b.label, kFontPx);
            t.drawText(b.label, r.x + (r.w - tw) * 0.5, r.y + r.h * 0.5 + 4.0, kFontPx, font::sansMedium());
        }
    }
}
}
