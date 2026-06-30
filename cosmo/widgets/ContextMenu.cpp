#include "ContextMenu.h"
#include "../CosmoTheme.h"
#include <algorithm>

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    namespace { constexpr double kItemH = 26.0, kMenuW = 184.0, kVPad = 5.0; }

    ContextMenu::ContextMenu(const Color &accent) : mAccent(accent) {}

    void ContextMenu::show(double x, double y, std::vector<Item> items)
    {
        mItems = std::move(items);
        mX = x; mY = y; mOpen = true;
        raise();  // topmost for input + draws last
    }

    Rect ContextMenu::menuRect() const
    {
        const double h = (double)mItems.size() * kItemH + 2 * kVPad;
        double x = mX, y = mY;
        if (x + kMenuW > width.value()) x = width.value() - kMenuW - 4;   // clamp on-screen
        if (y + h > height.value()) y = height.value() - h - 4;
        if (x < 4) x = 4; if (y < 4) y = 4;
        return Rect{x, y, kMenuW, h};
    }

    int ContextMenu::itemAt(const Point &world) const
    {
        const Rect r = menuRect();
        if (!r.contains(world)) return -1;
        const int i = (int)((world.y - (r.y + kVPad)) / kItemH);
        return (i >= 0 && i < (int)mItems.size()) ? i : -1;
    }

    bool ContextMenu::handleGesture(const Gesture &g, const Point &local)
    {
        if (!mOpen) return false;
        if (g.type == Gesture::Type::Click)
        {
            const int i = itemAt(local);  // root child at origin -> local == world
            mOpen = false;
            if (i >= 0 && mItems[i].enabled && mItems[i].action) mItems[i].action();
            return true;
        }
        return true;  // consume everything else while open (modal)
    }

    void ContextMenu::onOverlay(IRenderTarget &t) const
    {
        if (!mOpen) return;
        const Rect r = menuRect();
        drawRoundedRect(t, Rect{r.x - 1, r.y - 1, r.w + 2, r.h + 2}, radius::control() + 1,
                        Paint::filled(palette::bg()));  // opaque backing
        drawRoundedRect(t, r, radius::control(), Paint::filledStroked(palette::panel(), palette::line(), 1.0));
        for (int i = 0; i < (int)mItems.size(); ++i)
        {
            const double y = r.y + kVPad + i * kItemH;
            t.setFill(mItems[i].enabled ? palette::ink() : palette::faint());
            t.drawText(mItems[i].label, r.x + 12, y + kItemH * 0.5 + 4.0, 12.0);
        }
    }
}
}
