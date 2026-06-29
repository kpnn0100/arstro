#include "MenuBar.h"
#include "../CosmoTheme.h"

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    namespace
    {
        constexpr double kGap = 6.0;       // padding inside a title
        constexpr double kChar = 6.6;      // approx glyph advance at 12px
        constexpr double kTitlePx = 12.0;
        constexpr double kItemH = 24.0;
        constexpr double kDropW = 150.0;
    }

    MenuBar::MenuBar(const Color &accent) : mAccent(accent)
    {
        height.set(24.0);
        width.set(300.0);
    }

    void MenuBar::addMenu(Menu m) { mMenus.push_back(std::move(m)); }

    void MenuBar::setOpen(int idx)
    {
        if (idx == mOpen) return;
        mOpen = idx;
        if (onOpenChanged) onOpenChanged(mOpen);  // app shows/hides overlay menus
    }

    double MenuBar::titleW(int i) const { return (double)mMenus[i].title.size() * kChar + 2 * kGap + 6.0; }
    double MenuBar::titleX(int i) const
    {
        double x = 0.0;
        for (int k = 0; k < i; ++k) x += titleW(k);
        return x;
    }

    Rect MenuBar::dropdownRect(int i) const
    {
        const int n = (int)mMenus[i].items.size();
        return Rect{titleX(i), height.value(), kDropW, n * kItemH + 6.0};
    }

    int MenuBar::titleAt(const Point &p) const
    {
        if (p.y < 0 || p.y > height.value()) return -1;
        for (int i = 0; i < (int)mMenus.size(); ++i)
            if (p.x >= titleX(i) && p.x < titleX(i) + titleW(i)) return i;
        return -1;
    }

    int MenuBar::itemAt(int menu, const Point &p) const
    {
        if (menu < 0) return -1;
        const Rect d = dropdownRect(menu);
        if (!d.contains(p)) return -1;
        const int idx = (int)((p.y - (d.y + 3.0)) / kItemH);
        return (idx >= 0 && idx < (int)mMenus[menu].items.size()) ? idx : -1;
    }

    bool MenuBar::hitTestSelf(const Point &p) const
    {
        if (p.x >= 0 && p.x <= width.value() && p.y >= 0 && p.y <= height.value()) return true;
        // an open dropdown (items menu) is also hittable
        return mOpen >= 0 && !mMenus[mOpen].items.empty() && dropdownRect(mOpen).contains(p);
    }

    bool MenuBar::pointInActiveArea(const Point &local) const
    {
        if (local.x >= 0 && local.x <= width.value() && local.y >= 0 && local.y <= height.value()) return true;
        return mOpen >= 0 && !mMenus[mOpen].items.empty() && dropdownRect(mOpen).contains(local);
    }

    bool MenuBar::handleGesture(const Gesture &g, const Point &local)
    {
        if (g.type != Gesture::Type::Click && g.type != Gesture::Type::Down)
            return Segment::handleGesture(g, local);

        // click inside an open dropdown -> fire item, then close
        if (mOpen >= 0 && !mMenus[mOpen].items.empty())
        {
            const int it = itemAt(mOpen, local);
            if (it >= 0)
            {
                auto action = mMenus[mOpen].items[it].action;  // copy before closing
                setOpen(-1);
                if (g.type == Gesture::Type::Click && action) action();
                return true;
            }
        }
        // click on a title -> make it the single active menu (or toggle it closed)
        const int t = titleAt(local);
        if (t >= 0)
        {
            if (g.type == Gesture::Type::Click)
                setOpen(mOpen == t ? -1 : t);
            return true;
        }
        if (g.type == Gesture::Type::Click) setOpen(-1);  // bar gutter closes
        return true;
    }

    void MenuBar::onPaint(IRenderTarget &t) const
    {
        for (int i = 0; i < (int)mMenus.size(); ++i)
        {
            const bool active = (i == mOpen);
            if (active)
                drawRoundedRect(t, Rect{titleX(i), 0, titleW(i), height.value()}, radius::control(),
                                Paint::filled(palette::surface()));
            t.setFill(active ? palette::ink() : palette::muted());
            t.drawText(mMenus[i].title, titleX(i) + kGap + 3.0, height.value() * 0.5 + kTitlePx * 0.35, kTitlePx);
        }
        if (mOpen >= 0 && !mMenus[mOpen].items.empty())
        {
            const Rect d = dropdownRect(mOpen);
            drawRoundedRect(t, d, radius::control(),
                            Paint::filledStroked(palette::panel(), palette::line(), 1.0));
            for (int i = 0; i < (int)mMenus[mOpen].items.size(); ++i)
            {
                const double y = d.y + 3.0 + i * kItemH;
                t.setFill(palette::ink());
                t.drawText(mMenus[mOpen].items[i].label, d.x + 12.0, y + kItemH * 0.5 + 4.0, 12.0);
            }
        }
    }
}
}
