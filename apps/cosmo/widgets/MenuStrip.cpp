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
        // R-FAMILY-1: the shortcut column and the separator row.
        constexpr double kShortcutPx = 10.0;   // the ramp's mono readout size, one step under the label
        constexpr double kShortcutGap = 26.0;  // space::u(8): widest label -> the shortcut column
        constexpr double kSepH = 9.75;         // space::u(3): a separator row
        constexpr double kMinDropW = 152.0;
        constexpr int kKeyEnter = 13, kKeyEsc = 27, kKeyEnd = 35, kKeyHome = 36,
                      kKeyLeft = 37, kKeyUp = 38, kKeyRight = 39, kKeyDown = 40;
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

    Rect MenuStrip::titleRect(int i) const { return Rect{titleX(i), 0.0, titleW(i), height.value()}; }

    double MenuStrip::itemHeight(int menu, int item) const
    {
        return mMenus[(size_t)menu].items[(size_t)item].separator ? kSepH : kItemH;
    }

    double MenuStrip::itemTop(int menu, int item) const
    {
        // With no separator this is item * kItemH exactly (a sum of 24.0s has no rounding), so a
        // menu of plain rows sits where it always did.
        double y = 0.0;
        for (int k = 0; k < item; ++k) y += itemHeight(menu, k);
        return y;
    }

    bool MenuStrip::hasShortcuts(int menu) const
    {
        for (const auto &it : mMenus[(size_t)menu].items)
            if (!it.separator && !it.shortcut.empty()) return true;
        return false;
    }

    Rect MenuStrip::itemRect(int menu, int item) const
    {
        const Rect d = dropdownRect(menu);
        return Rect{d.x, d.y + 4.0 + itemTop(menu, item), d.w, itemHeight(menu, item)};   // itemAt's own arithmetic, inverted
    }

    Rect MenuStrip::shortcutRect(int menu, int item) const
    {
        if (menu != mMeasuredMenu || item < 0 || item >= (int)mShortcutRects.size()) return Rect{};
        return mShortcutRects[(size_t)item];
    }

    void MenuStrip::setItems(int menu, std::vector<Item> items)
    {
        if (menu < 0 || menu >= (int)mMenus.size()) return;
        if (mOpen == menu) close();
        if (mMeasuredMenu == menu) mMeasuredMenu = -1;   // measured for rows that are gone
        mMenus[(size_t)menu].items = std::move(items);
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
        double w = kMinDropW;
        if (!hasShortcuts(i))
        {
            // No shortcut anywhere: the width every existing menu has always had.
            for (const auto &it : mMenus[i].items)
                w = std::max(w, estimateTextWidth(it.label, kItemFontPx) + 2 * kDropPadX);
        }
        else
        {
            // R-FAMILY-1: widest label + widest shortcut, MEASURED. Measurement is only valid during
            // a paint, so the paint records it; a question asked before the first paint (a click
            // aimed straight after opening) gets the estimate, which the paint then replaces before
            // anything is drawn at it.
            double lw = 0.0, sw = 0.0;
            if (mMeasuredMenu == i) { lw = mMeasuredLabelW; sw = mMeasuredShortcutW; }
            else
                for (const auto &it : mMenus[i].items)
                {
                    if (it.separator) continue;
                    lw = std::max(lw, estimateTextWidth(it.label, kItemFontPx));
                    sw = std::max(sw, estimateTextWidth(it.shortcut, kShortcutPx));
                }
            w = std::max(w, lw + kShortcutGap + sw + 2 * kDropPadX);
        }
        return Rect{titleX(i), height.value() + 3.0, w, itemTop(i, n) + 8.0};
    }

    int MenuStrip::stepCursor(int from, int dir) const
    {
        if (mOpen < 0) return -1;
        const auto &items = mMenus[(size_t)mOpen].items;
        const int n = (int)items.size();
        int i = from < 0 ? (dir > 0 ? -1 : n) : from;
        for (int tries = 0; tries < n; ++tries)
        {
            i = ((i + dir) % n + n) % n;
            if (items[(size_t)i].selectable()) return i;   // separators and disabled rows are stepped over
        }
        return -1;
    }

    bool MenuStrip::menuKey(const KeyEvent &e)
    {
        if (mOpen < 0 || e.type != KeyEvent::Type::Down) return false;
        auto stand = [this](int row) {
            mCursor = row;
            mCursorByKey = true;
            mItemHover.setHovered(row);   // the same eased wash the pointer drives: it cross-fades
        };
        const int menus = (int)mMenus.size();
        switch (e.keyCode)
        {
        case kKeyDown: stand(stepCursor(mCursor, +1)); return true;
        case kKeyUp: stand(stepCursor(mCursor, -1)); return true;
        case kKeyHome: stand(stepCursor(-1, +1)); return true;
        case kKeyEnd: stand(stepCursor(-1, -1)); return true;
        case kKeyLeft:
        case kKeyRight:
            setOpen(((mOpen + (e.keyCode == kKeyRight ? 1 : -1)) % menus + menus) % menus);
            stand(stepCursor(-1, +1));
            return true;
        case kKeyEnter:
            if (mCursor >= 0 && mMenus[(size_t)mOpen].items[(size_t)mCursor].selectable())
            {
                auto action = mMenus[(size_t)mOpen].items[(size_t)mCursor].action;  // copy before closing
                setOpen(-1);
                if (action) action();
            }
            return true;
        case kKeyEsc: setOpen(-1); return true;
        default: return false;
        }
    }

    void MenuStrip::setOpen(int idx)
    {
        if (idx == mOpen) return;
        mOpenFromClosed = (mOpen < 0 && idx >= 0);
        mOpen = idx;
        mCursor = -1;                                  // a cursor belongs to the menu it was in
        if (mCursorByKey) { mItemHover.clear(); mCursorByKey = false; }
        mMeasuredMenu = -1;                            // the next paint measures the menu now open
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
        // Hover fades: clear the tracked ids when the pointer leaves, then ease each.
        // The keyboard's cursor (R-FAMILY-1) stays lit while the pointer is elsewhere.
        if (!isHovered()) { mTitleHover.clear(); if (!mCursorByKey) { mItemHover.clear(); mCursor = -1; } }
        mTitleHover.advance(nowMs);
        mItemHover.advance(nowMs);

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
        // Row by row, because a separator is shorter than an item. For a menu of plain rows this is
        // (int)((p.y - top) / kItemH), as before.
        double y = d.y + 4.0;
        if (p.y < y) return -1;
        for (int i = 0; i < (int)mMenus[menu].items.size(); ++i)
        {
            const double h = itemHeight(menu, i);
            if (p.y < y + h) return i;
            y += h;
        }
        return -1;
    }

    bool MenuStrip::pointInActiveArea(const Point &local) const
    {
        if (local.x >= 0 && local.x <= width.value() && local.y >= 0 && local.y <= height.value()) return true;
        return mOpen >= 0 && !mMenus[mOpen].items.empty() && dropdownRect(mOpen).contains(local);
    }

    bool MenuStrip::hitTestSelf(const Point &p) const { return pointInActiveArea(p); }

    bool MenuStrip::handleGesture(const Gesture &g, const Point &local)
    {
        if (g.type == Gesture::Type::Move)  // track hovered title + (open) dropdown item
        {
            mTitleHover.setHovered(titleAt(local));
            // A separator and a disabled row take no hover (R-FAMILY-1).
            int row = mOpen >= 0 ? itemAt(mOpen, local) : -1;
            if (row >= 0 && !mMenus[mOpen].items[row].selectable()) row = -1;
            // The pointer takes the cursor from the keyboard only by standing on a row.
            if (row >= 0 || !mCursorByKey)
            {
                mCursor = row;
                mCursorByKey = false;
                mItemHover.setHovered(row);
            }
            return true;
        }
        if (g.type == Gesture::Type::Down)
            return pointInActiveArea(local);  // consume so the press doesn't dismiss before the Click
        if (g.type != Gesture::Type::Click)
            return Segment::handleGesture(g, local);

        if (mOpen >= 0 && !mMenus[mOpen].items.empty())
        {
            const int it = itemAt(mOpen, local);
            if (it >= 0)
            {
                // A separator or a disabled row: the click is taken and does nothing — the menu
                // stays open, as it does in every desktop menu (R-FAMILY-1).
                if (!mMenus[mOpen].items[it].selectable()) return true;
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
            const double lift = active ? 0.0 : mTitleHover.amount(i);  // active title already white
            const Color col = active ? palette::white()
                                     : lerpColor(palette::mutedForeground(), palette::white(), lift);
            const double tw = estimateTextWidth(mMenus[i].title, kFontPx);
            const double tx = titleX(i) + (titleW(i) - tw) * 0.5;
            t.setFill(col);
            t.drawText(mMenus[i].title, tx, height.value() * 0.5 + kFontPx * 0.35, kFontPx, font::sansMedium());
        }
    }

    void MenuStrip::onOverlay(IRenderTarget &t) const
    {
        if (mOpen < 0 || mMenus[mOpen].items.empty()) return;
        const auto &items = mMenus[mOpen].items;
        const bool shortcuts = hasShortcuts(mOpen);
        if (shortcuts)
        {
            // Measure BEFORE asking for the rect, so the first frame is already at the real width.
            double lw = 0.0, sw = 0.0;
            for (const auto &it : items)
            {
                if (it.separator) continue;
                lw = std::max(lw, t.measureText(it.label, kItemFontPx, font::sans()));
                if (!it.shortcut.empty()) sw = std::max(sw, t.measureText(it.shortcut, kShortcutPx, font::mono()));
            }
            mMeasuredLabelW = lw;
            mMeasuredShortcutW = sw;
            mMeasuredMenu = mOpen;
            mShortcutRects.assign(items.size(), Rect{});
        }
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
        double y = d.y + 4.0;
        for (int i = 0; i < (int)items.size(); ++i)
        {
            const Item &it = items[(size_t)i];
            const double rowH = itemHeight(mOpen, i);
            const double rowY = y;
            y += rowH;
            if (it.separator)
            {
                // A hairline across the row's middle, inset like the hover wash. A filled 1 px rect
                // on a whole pixel, so it is one crisp line at 100 % rather than two faint ones.
                drawRoundedRect(t, Rect{d.x + 3.0, std::floor(rowY + rowH * 0.5), d.w - 6.0, 1.0}, 0.0,
                                Paint::filled(palette::border()));
                continue;
            }
            // Per-item hover wash — each item cross-fades independently (R-G-3). A disabled row
            // never has one: Move and the keyboard both refuse to stand on it.
            const double ih = mItemHover.amount(i) * reveal;
            if (ih > 0.001 && it.enabled)
                drawRoundedRect(t, Rect{d.x + 3.0, rowY, d.w - 6.0, kItemH}, radius::control(),
                                Paint::filled(palette::hoverWash(ih)));
            t.setFill(it.enabled ? palette::foreground() : palette::mutedForeground());
            t.drawText(it.label, d.x + kDropPadX, rowY + kItemH * 0.5 + kItemFontPx * 0.35,
                       kItemFontPx, font::sans());
            if (!it.shortcut.empty())
            {
                // Right-aligned by its own measured width; mono, muted (and fainter when disabled).
                const double sw = t.measureText(it.shortcut, kShortcutPx, font::mono());
                const double sx = d.x + d.w - kDropPadX - sw;
                Color sc = palette::mutedForeground();
                if (!it.enabled) sc.a *= 0.55;
                t.setFill(sc);
                t.drawText(it.shortcut, sx, rowY + kItemH * 0.5 + kShortcutPx * 0.35, kShortcutPx, font::mono());
                mShortcutRects[(size_t)i] = Rect{sx, rowY, sw, kItemH};
            }
        }
        t.restore();
    }
}
}
