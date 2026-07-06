#include "MenuStrip.h"
#include "TextMetrics.h"
#include "../Theme.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace
    {
        constexpr double kFontPx = 11.0;
        constexpr double kPadX = 8.125;   // px-2.5
        constexpr double kGap = 1.0;      // gap-[1px]
        constexpr double kItemH = 24.0;
        constexpr double kItemFontPx = 11.0;
        constexpr double kDropPadX = 11.0;
        constexpr double kGrowMs = 190.0;
        constexpr double kExpandMs = 160.0;  // dropdown grow-open duration
    }

    MenuStrip::MenuStrip() { height.set(21.0); }

    void MenuStrip::addMenu(Menu m) { mMenus.push_back(std::move(m)); }

    double MenuStrip::titleW(int i) const { return estimateTextWidth(mMenus[i].title, kFontPx) + 2 * kPadX; }

    double MenuStrip::titleX(int i) const
    {
        double x = 0.0;
        for (int k = 0; k < i; ++k) x += titleW(k) + kGap;
        return x;
    }

    double MenuStrip::contentWidth() const
    {
        double w = 0.0;
        for (int i = 0; i < (int)mMenus.size(); ++i) w += titleW(i) + (i ? kGap : 0.0);
        return w;
    }

    Rect MenuStrip::dropdownRect(int i) const
    {
        const int n = (int)mMenus[i].items.size();
        double w = 152.0;
        for (const auto &it : mMenus[i].items)
            w = std::max(w, estimateTextWidth(it.label, kItemFontPx) + 2 * kDropPadX);
        return Rect{titleX(i), height.value() + 3.0, w, n * kItemH + 8.0};
    }

    void MenuStrip::setOpen(int idx)
    {
        if (idx == mOpen) return;
        mOpenFromClosed = (mOpen < 0 && idx >= 0);
        mOpen = idx;
        if (mOpen >= 0)
        {
            raise();
            if (mOpenFromClosed) mDropRevealPending = true;  // grow the dropdown open
            else mDropReveal.set(1.0);                        // switch: keep it open, swap contents
        }
        else
        {
            mDropReveal.set(0.0);                             // closed: reset so the next open grows fresh
        }
        mAnimPending = true;
        if (onOpenChanged) onOpenChanged(mOpen);
    }

    void MenuStrip::close() { setOpen(-1); }

    void MenuStrip::advance(double nowMs)
    {
        if (mAnimPending)
        {
            if (mOpen >= 0)
            {
                const double cx = titleX(mOpen) + titleW(mOpen) * 0.5, w = titleW(mOpen);
                if (mOpenFromClosed) mHiCenter.set(cx);              // expand from the tab: pin centre, grow width
                else mHiCenter.animateTo(cx, kGrowMs, Easing::EaseOutCubic, nowMs);  // switch: slide centre
                mHiW.animateTo(w, kGrowMs, Easing::EaseOutCubic, nowMs);
            }
            else
            {
                mHiW.animateTo(0.0, kGrowMs, Easing::EaseOutCubic, nowMs);  // close: shrink in place
            }
            mAnimPending = false;
        }
        if (mDropRevealPending)
        {
            mDropReveal.set(0.0);
            mDropReveal.animateTo(1.0, kExpandMs, Easing::EaseOutCubic, nowMs);
            mDropRevealPending = false;
        }
        mHiCenter.update(nowMs);
        mHiW.update(nowMs);
        mDropReveal.update(nowMs);
        Segment::advance(nowMs);
    }

    int MenuStrip::titleAt(const Point &p) const
    {
        if (p.y < 0 || p.y > height.value()) return -1;
        for (int i = 0; i < (int)mMenus.size(); ++i)
            if (p.x >= titleX(i) && p.x < titleX(i) + titleW(i)) return i;
        return -1;
    }

    int MenuStrip::itemAt(int menu, const Point &p) const
    {
        if (menu < 0 || mMenus[menu].items.empty()) return -1;
        const Rect d = dropdownRect(menu);
        if (!d.contains(p)) return -1;
        const int idx = (int)((p.y - (d.y + 4.0)) / kItemH);
        return (idx >= 0 && idx < (int)mMenus[menu].items.size()) ? idx : -1;
    }

    bool MenuStrip::pointInActiveArea(const Point &local) const
    {
        if (local.x >= 0 && local.x <= width.value() && local.y >= 0 && local.y <= height.value()) return true;
        return mOpen >= 0 && !mMenus[mOpen].items.empty() && dropdownRect(mOpen).contains(local);
    }

    bool MenuStrip::hitTestSelf(const Point &p) const { return pointInActiveArea(p); }

    bool MenuStrip::handleGesture(const Gesture &g, const Point &local)
    {
        if (g.type == Gesture::Type::Down)
            return pointInActiveArea(local);  // consume so the press doesn't dismiss before the Click
        if (g.type != Gesture::Type::Click)
            return Segment::handleGesture(g, local);

        if (mOpen >= 0 && !mMenus[mOpen].items.empty())
        {
            const int it = itemAt(mOpen, local);
            if (it >= 0)
            {
                auto action = mMenus[mOpen].items[it].action;  // copy before closing
                setOpen(-1);
                if (action) action();
                return true;
            }
        }
        const int t = titleAt(local);
        if (t >= 0) { setOpen(mOpen == t ? -1 : t); return true; }
        setOpen(-1);
        return true;
    }

    void MenuStrip::onPaint(IRenderTarget &t) const
    {
        // Highlight: expands from the tab when opening, slides between tabs when
        // switching, shrinks in place when closing (centre + width both animated).
        const double hw = mHiW.value();
        if (hw > 0.5)
        {
            const double cx = mHiCenter.value();
            drawRoundedRect(t, Rect{cx - hw * 0.5, 0.0, hw, height.value()}, radius::control(),
                            Paint::filled(palette::primary()));
        }
        for (int i = 0; i < (int)mMenus.size(); ++i)
        {
            const bool active = (i == mOpen);
            const Color col = active ? palette::white() : palette::mutedForeground();
            const double tw = estimateTextWidth(mMenus[i].title, kFontPx);
            const double tx = titleX(i) + (titleW(i) - tw) * 0.5;
            t.setFill(col);
            t.drawText(mMenus[i].title, tx, height.value() * 0.5 + kFontPx * 0.35, kFontPx, font::sansMedium());
        }
    }

    void MenuStrip::onOverlay(IRenderTarget &t) const
    {
        if (mOpen < 0 || mMenus[mOpen].items.empty()) return;
        const Rect d = dropdownRect(mOpen);

        // Grow the panel downward out of the title: the rounded background is
        // drawn at the currently-revealed height, and the items are clipped to it
        // so they appear top-down as the panel expands.
        const double reveal = std::min(1.0, std::max(0.0, mDropReveal.value()));
        const double revH = d.h * reveal;
        if (revH < 1.0) return;

        drawRoundedRect(t, Rect{d.x, d.y, d.w, revH}, radius::control(),
                        Paint::filledStroked(palette::popover(), palette::border(), 1.0));
        t.save();
        t.clipRect(d.x, d.y, d.w, revH);
        for (int i = 0; i < (int)mMenus[mOpen].items.size(); ++i)
        {
            const double y = d.y + 4.0 + i * kItemH;
            t.setFill(palette::foreground());
            t.drawText(mMenus[mOpen].items[i].label, d.x + kDropPadX, y + kItemH * 0.5 + kItemFontPx * 0.35,
                       kItemFontPx, font::sans());
        }
        t.restore();
    }
}
}
