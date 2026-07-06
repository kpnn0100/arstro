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

    bool ContextMenu::handleGesture(const Gesture &g, const Point &local)
    {
        if (!mOpen) return false;
        using T = Gesture::Type;
        if (g.type == T::Down) return true;  // consume so the press doesn't fall through
        if (g.type == T::Click || g.type == T::RightClick)
        {
            const int i = itemAt(local);
            if (i >= 0)
            {
                auto action = mItems[i].action;  // copy before closing
                mOpen = false;
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
        if (!mOpen) return;
        const Rect r = menuRect();
        drawRoundedRect(t, r, radius::control(), Paint::filledStroked(palette::popover(), palette::border(), 1.0));
        for (int i = 0; i < (int)mItems.size(); ++i)
        {
            const double y = r.y + kPadY + i * kItemH;
            t.setFill(palette::foreground());
            t.drawText(mItems[i].label, r.x + kPadX, y + kItemH * 0.5 + kFontPx * 0.35, kFontPx, font::sans());
        }
    }
}
}
