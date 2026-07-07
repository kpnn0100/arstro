#include "ContextMenu.h"
#include "TextMetrics.h"
#include "../Theme.h"
#include <algorithm>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace
    {
        constexpr double kItemH = 24.0;
        constexpr double kFontPx = 11.0;
        constexpr double kPadX = 11.0, kPadY = 4.0;
        constexpr double kMinW = 150.0;
    }

    void ContextMenu::open(std::vector<Item> items, double x, double y)
    {
        mItems = std::move(items);
        mX = x;
        mY = y;
        mOpen = !mItems.empty();
        if (mOpen) raise();  // topmost -> wins hit-testing over body widgets it overlaps
    }

    Rect ContextMenu::menuRect() const
    {
        double w = kMinW;
        for (const auto &it : mItems) w = std::max(w, estimateTextWidth(it.label, kFontPx) + 2 * kPadX);
        const double h = (double)mItems.size() * kItemH + 2 * kPadY;
        // keep it fully on-screen (nudge left/up if it would overflow the window)
        double x = std::min(mX, width.value() - w);
        double y = std::min(mY, height.value() - h);
        return Rect{std::max(0.0, x), std::max(0.0, y), w, h};
    }

    int ContextMenu::itemAt(const Point &local) const
    {
        const Rect r = menuRect();
        if (!r.contains(local)) return -1;
        const int i = (int)((local.y - (r.y + kPadY)) / kItemH);
        return (i >= 0 && i < (int)mItems.size()) ? i : -1;
    }

    void ContextMenu::advance(double nowMs)
    {
        Segment::advance(nowMs);
        mNowMs = nowMs;
        if (mOpen != mWasOpen)  // ease the popup in on open, out on close
        {
            mWasOpen = mOpen;
            mAppear.animateTo(mOpen ? 1.0 : 0.0, mOpen ? 130.0 : 100.0, Easing::EaseOutCubic, nowMs);
        }
        const bool hov = mOpen && mHoverIndex >= 0;
        if (hov != mHoverPrev)  // fade the item highlight in/out
        {
            mHoverPrev = hov;
            mHoverAmt.animateTo(hov ? 1.0 : 0.0, interaction::kHoverMs, Easing::EaseOutCubic, nowMs);
        }
        mAppear.update(nowMs);
        mHoverAmt.update(nowMs);
    }

    bool ContextMenu::handleGesture(const Gesture &g, const Point &local)
    {
        if (!mOpen) return false;
        using T = Gesture::Type;
        if (g.type == T::Move) { mHoverIndex = itemAt(local); return true; }  // track hovered item
        if (g.type == T::Down) return true;  // consume so the press doesn't fall through
        if (g.type == T::Click || g.type == T::RightClick)
        {
            const int i = itemAt(local);
            if (i >= 0)
            {
                auto action = mItems[i].action;  // copy before closing
                mOpen = false;
                mHoverIndex = -1;
                if (action) action();
            }
            else
            {
                mOpen = false;  // click outside cancels
            }
            return true;
        }
        return true;  // modal: swallow everything else while open
    }

    void ContextMenu::onOverlay(IRenderTarget &t) const
    {
        const double appear = mAppear.value();
        if (!mOpen && appear <= 0.001) return;  // fully closed
        Rect r = menuRect();
        r.y -= (1.0 - appear) * 6.0;  // rise into place as it fades in (R-G-1)

        Color body = palette::popover(), border = palette::border();
        body.a *= appear; border.a *= appear;
        drawRoundedRect(t, r, radius::control(), Paint::filledStroked(body, border, 1.0));

        // Hovered-item highlight (faded by mHoverAmt), tracked in mHoverIndex.
        const double hv = mHoverAmt.value() * appear;
        if (mHoverIndex >= 0 && mHoverIndex < (int)mItems.size() && hv > 0.001)
        {
            const double hy = r.y + kPadY + mHoverIndex * kItemH;
            drawRoundedRect(t, Rect{r.x + 3, hy, r.w - 6, kItemH}, radius::control(),
                            Paint::filled(palette::whiteAlpha(0.08 * hv)));
        }
        for (int i = 0; i < (int)mItems.size(); ++i)
        {
            const double y = r.y + kPadY + i * kItemH;
            Color fg = palette::foreground();
            fg = brighten(fg, (i == mHoverIndex ? 0.0 : 0.0));  // (keep label steady; bg carries hover)
            fg.a *= appear;
            t.setFill(fg);
            t.drawText(mItems[i].label, r.x + kPadX, y + kItemH * 0.5 + kFontPx * 0.35, kFontPx, font::sans());
        }
    }
}
}
