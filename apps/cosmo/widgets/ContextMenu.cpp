#include "ContextMenu.h"
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
        constexpr double kItemH = 24.0;
        constexpr double kFontPx = 11.0;
        constexpr double kPadX = 11.0, kPadY = 4.0;
        constexpr double kMinW = 150.0;
        // Rename mode (DR-TREE-5): a "Rename" header row, then a light textbox below.
        constexpr double kHeaderH = 22.0;
        constexpr double kFieldH = 30.0;    // the light entry field's height once grown
        constexpr double kFieldGap = 6.0;   // header -> field vertical gap
        constexpr double kFieldMx = 8.0;    // field horizontal inset within the menu
        constexpr double kRenameW = 208.0;  // menu width in rename mode (room for a name)
        // R-FAMILY-1: the shortcut column and the separator row — MenuStrip's values.
        constexpr double kShortcutPx = 10.0;
        constexpr double kShortcutGap = 26.0;  // space::u(8)
        constexpr double kSepH = 9.75;         // space::u(3)
        constexpr int kKeyEnter = 13, kKeyEsc = 27, kKeyEnd = 35, kKeyHome = 36, kKeyUp = 38, kKeyDown = 40;
        double clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
    }

    void ContextMenu::open(std::vector<Item> items, double x, double y)
    {
        mItems = std::move(items);
        mX = x;
        mY = y;
        mRenaming = false;
        mRename.set(0.0);
        mCursorByKey = false;
        mMeasured = false;      // measured for the rows that were here before
        mOpen = !mItems.empty();
        if (mOpen) raise();  // topmost -> wins hit-testing over body widgets it overlaps
    }

    void ContextMenu::enterRenameMode(const std::string &currentName)
    {
        // Called by the "Rename Group" item's action (which already cleared mOpen): the
        // menu's items collapse to a "Rename" header, then the field grows below.
        mPreRenameItemsH = itemTop((int)mItems.size());  // shrink from the current list
        mOpen = true;
        mRenaming = true;
        mRenameText = currentName.empty() ? "Group" : currentName;
        mSelectAll = true;
        mRename.animateTo(1.0, 260.0, Easing::EaseInOutCubic, mNowMs);
        requestFocus();
        raise();
    }

    void ContextMenu::openRename(const std::string &currentName, double x, double y)
    {
        mItems.clear();          // straight to rename (no item list to collapse)
        mX = x;
        mY = y;
        mPreRenameItemsH = kHeaderH;
        mOpen = true;
        mRenaming = true;
        mRenameText = currentName.empty() ? "Group" : currentName;
        mSelectAll = true;
        mRename.set(0.0);
        mRename.animateTo(1.0, 260.0, Easing::EaseInOutCubic, mNowMs);
        requestFocus();
        raise();
    }

    double ContextMenu::renameContentH() const
    {
        const double amt = mRename.value();
        const double collapse = clamp01(amt * 2.0);          // items -> header, first half
        const double grow = clamp01((amt - 0.5) * 2.0);      // field grows, second half
        const double itemsH = mPreRenameItemsH + (kHeaderH - mPreRenameItemsH) * collapse;
        return itemsH + (kFieldGap + kFieldH) * grow;
    }

    double ContextMenu::itemHeight(int i) const { return mItems[(size_t)i].separator ? kSepH : kItemH; }

    double ContextMenu::itemTop(int i) const
    {
        double y = 0.0;   // with no separator: i * kItemH exactly, as before
        for (int k = 0; k < i && k < (int)mItems.size(); ++k) y += itemHeight(k);
        return y;
    }

    bool ContextMenu::hasShortcuts() const
    {
        for (const auto &it : mItems)
            if (!it.separator && !it.shortcut.empty()) return true;
        return false;
    }

    int ContextMenu::stepCursor(int from, int dir) const
    {
        const int n = (int)mItems.size();
        int i = from < 0 ? (dir > 0 ? -1 : n) : from;
        for (int tries = 0; tries < n; ++tries)
        {
            i = ((i + dir) % n + n) % n;
            if (mItems[(size_t)i].selectable()) return i;
        }
        return -1;
    }

    bool ContextMenu::menuKey(const KeyEvent &e)
    {
        if (!mOpen || mRenaming || e.type != KeyEvent::Type::Down) return false;
        auto stand = [this](int row) { mCursorByKey = true; mHover.setHovered(row); };
        switch (e.keyCode)
        {
        case kKeyDown: stand(stepCursor(mHover.hovered(), +1)); return true;
        case kKeyUp: stand(stepCursor(mHover.hovered(), -1)); return true;
        case kKeyHome: stand(stepCursor(-1, +1)); return true;
        case kKeyEnd: stand(stepCursor(-1, -1)); return true;
        case kKeyEnter:
        {
            const int i = mHover.hovered();
            if (i >= 0 && i < (int)mItems.size() && mItems[(size_t)i].selectable())
            {
                auto action = mItems[(size_t)i].action;  // copy before closing
                mOpen = false;
                mHover.clear();
                if (action) action();
            }
            return true;
        }
        case kKeyEsc: mOpen = false; return true;
        default: return false;
        }
    }

    Rect ContextMenu::menuRect() const
    {
        double w = kMinW;
        if (mRenaming)
            w = kRenameW;
        else if (!hasShortcuts())
            for (const auto &it : mItems) w = std::max(w, estimateTextWidth(it.label, kFontPx) + 2 * kPadX);
        else
        {
            // R-FAMILY-1: widest label + widest shortcut, measured in the paint; the estimate until then.
            double lw = 0.0, sw = 0.0;
            if (mMeasured) { lw = mMeasuredLabelW; sw = mMeasuredShortcutW; }
            else
                for (const auto &it : mItems)
                {
                    if (it.separator) continue;
                    lw = std::max(lw, estimateTextWidth(it.label, kFontPx));
                    sw = std::max(sw, estimateTextWidth(it.shortcut, kShortcutPx));
                }
            w = std::max(w, lw + kShortcutGap + sw + 2 * kPadX);
        }
        const double contentH = mRenaming ? renameContentH() : itemTop((int)mItems.size());
        const double h = contentH + 2 * kPadY;
        // keep it fully on-screen (nudge left/up if it would overflow the window)
        double x = std::min(mX, width.value() - w);
        double y = std::min(mY, height.value() - h);
        return Rect{std::max(0.0, x), std::max(0.0, y), w, h};
    }

    Rect ContextMenu::fieldRect() const
    {
        const Rect r = menuRect();
        const double grow = clamp01((mRename.value() - 0.5) * 2.0);
        const double top = r.y + kPadY + kHeaderH + kFieldGap * grow;
        return Rect{r.x + kFieldMx, top, r.w - 2 * kFieldMx, kFieldH * grow};
    }

    Rect ContextMenu::itemRect(int i) const
    {
        const Rect r = menuRect();
        if (i < 0 || i >= (int)mItems.size()) return Rect{r.x, r.y + kPadY + i * kItemH, r.w, kItemH};
        return Rect{r.x, r.y + kPadY + itemTop(i), r.w, itemHeight(i)};   // itemAt's arithmetic, inverted
    }

    int ContextMenu::itemAt(const Point &local) const
    {
        const Rect r = menuRect();
        if (!r.contains(local)) return -1;
        double y = r.y + kPadY;   // row by row: a separator is shorter than an item
        if (local.y < y) return -1;
        for (int i = 0; i < (int)mItems.size(); ++i)
        {
            const double h = itemHeight(i);
            if (local.y < y + h) return i;
            y += h;
        }
        return -1;
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
        if (!mOpen) mHover.clear();  // nothing hovered once closed
        mAppear.update(nowMs);
        mRename.update(nowMs);
        mHover.advance(nowMs);  // per-item hover cross-fade
    }

    bool ContextMenu::handleKey(const KeyEvent &e)
    {
        if (!mRenaming) return false;  // only the rename field consumes keys
        if (e.type == KeyEvent::Type::Text && !e.text.empty())
        {
            if (mSelectAll) { mRenameText.clear(); mSelectAll = false; }  // typing replaces the selection
            mRenameText += e.text;
            return true;
        }
        if (e.type == KeyEvent::Type::Down)
        {
            if (e.keyCode == 8)  // Backspace
            {
                if (mSelectAll) { mRenameText.clear(); mSelectAll = false; }
                else if (!mRenameText.empty()) mRenameText.pop_back();
                return true;
            }
            if (e.keyCode == 13)  // Enter -> commit
            {
                std::string name = mRenameText;
                close();
                if (onRename && !name.empty()) onRename(name);
                return true;
            }
            if (e.keyCode == 27) { close(); return true; }  // Escape -> cancel
        }
        return true;  // swallow every other key while editing
    }

    bool ContextMenu::handleGesture(const Gesture &g, const Point &local)
    {
        if (!mOpen) return false;
        using T = Gesture::Type;
        if (mRenaming)
        {
            if (g.type == T::Down) return true;
            if (g.type == T::Click || g.type == T::RightClick)
            {
                if (menuRect().contains(local)) { if (fieldRect().contains(local)) mSelectAll = false; return true; }
                close();  // click-away cancels
                return true;
            }
            return true;  // modal: swallow everything else while editing
        }
        if (g.type == T::Move)  // track hovered item
        {
            int row = itemAt(local);
            if (row >= 0 && !mItems[(size_t)row].selectable()) row = -1;   // a separator / disabled row takes no hover
            // The pointer takes the cursor from the keyboard only by standing on a row (R-FAMILY-1).
            if (row >= 0 || !mCursorByKey) { mHover.setHovered(row); mCursorByKey = false; }
            return true;
        }
        if (g.type == T::Down) return true;  // consume so the press doesn't fall through
        if (g.type == T::Click || g.type == T::RightClick)
        {
            const int i = itemAt(local);
            if (i >= 0 && !mItems[(size_t)i].selectable()) return true;  // separator / disabled: nothing, and it stays open
            if (i >= 0)
            {
                auto action = mItems[i].action;  // copy before closing
                mOpen = false;
                mHover.clear();
                if (action) action();  // a "Rename Group" action re-opens us in rename mode
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
        const bool shortcuts = !mRenaming && hasShortcuts();
        if (shortcuts)
        {
            // Measure BEFORE asking for the rect, so the first frame is already at the real width.
            double lw = 0.0, sw = 0.0;
            for (const auto &it : mItems)
            {
                if (it.separator) continue;
                lw = std::max(lw, t.measureText(it.label, kFontPx, font::sans()));
                if (!it.shortcut.empty()) sw = std::max(sw, t.measureText(it.shortcut, kShortcutPx, font::mono()));
            }
            mMeasuredLabelW = lw;
            mMeasuredShortcutW = sw;
            mMeasured = true;
            mShortcutRects.assign(mItems.size(), Rect{});
        }
        Rect r = menuRect();
        r.y -= (1.0 - appear) * 6.0;  // rise into place as it fades in (R-G-1)

        Color body = palette::popover(), border = palette::border();
        body.a *= appear; border.a *= appear;
        drawRoundedRect(t, r, radius::control(), Paint::filledStroked(body, border, 1.0));

        if (mRenaming)
        {
            const double amt = mRename.value();
            const double collapse = clamp01(amt * 2.0);
            const double grow = clamp01((amt - 0.5) * 2.0);

            // The original items fade out as the menu collapses to the header.
            const double itemsA = (1.0 - collapse) * appear;
            if (itemsA > 0.01)
                for (int i = 0; i < (int)mItems.size(); ++i)
                {
                    if (mItems[(size_t)i].separator) continue;   // the words fade out; a rule is not a word
                    const double y = r.y + kPadY + itemTop(i);
                    Color fg = mItems[(size_t)i].enabled ? palette::foreground() : palette::mutedForeground(); fg.a *= itemsA;
                    t.setFill(fg);
                    t.drawText(mItems[i].label, r.x + kPadX, y + kItemH * 0.5 + kFontPx * 0.35, kFontPx, font::sans());
                }

            // "Rename" header fades in as the items fade out.
            const double headerA = clamp01((amt - 0.1) / 0.4) * appear;
            if (headerA > 0.01)
            {
                Color fg = palette::mutedForeground(); fg.a *= headerA;
                t.setFill(fg);
                t.drawText(renameTitle, r.x + kPadX, r.y + kPadY + kHeaderH * 0.5 + kFontPx * 0.35, kFontPx, font::sans());
            }

            // The elegant light field grows in below the header.
            if (grow > 0.001)
            {
                const Rect fr = fieldRect();
                Color fbg = palette::inputLight(); fbg.a *= appear;
                drawRoundedRect(t, fr, radius::control(), Paint::filled(fbg));

                const double innerPad = 8.0;
                const double innerW = fr.w - 2 * innerPad;
                // Scroll to keep the tail (caret end) visible if the name overflows (R5).
                std::string shown = mRenameText;
                while (estimateTextWidth(shown, kFontPx) > innerW && shown.size() > 1) shown = shown.substr(1);
                const double textW = estimateTextWidth(shown, kFontPx);
                const double tx = fr.x + innerPad;
                const double ty = fr.y + fr.h * 0.5 + kFontPx * 0.35;

                if (mSelectAll && !shown.empty())  // whole-field selection wash
                {
                    Color sel = palette::primary(); sel.a = 0.26 * appear * grow;
                    drawRoundedRect(t, Rect{tx - 2, fr.y + 5, textW + 4, fr.h - 10}, radius::hairline(), Paint::filled(sel));
                }
                Color txt = palette::inputLightText(); txt.a *= appear;
                t.setFill(txt);
                t.drawText(shown, tx, ty, kFontPx, font::sans());

                // Accent caret at the text end (hidden while the field is fully selected), gentle blink.
                const double blink = 0.55 + 0.45 * std::cos(mNowMs * 0.006);
                Color caret = palette::primary();
                caret.a = grow * appear * (mSelectAll ? 0.0 : blink);
                if (caret.a > 0.01)
                    drawRoundedRect(t, Rect{tx + textW + 1.0, fr.y + 5, 1.6, fr.h - 10}, 0.0, Paint::filled(caret));
            }
            return;
        }

        for (int i = 0; i < (int)mItems.size(); ++i)
        {
            const Item &it = mItems[(size_t)i];
            const double y = r.y + kPadY + itemTop(i);
            if (it.separator)
            {
                // A hairline across the row's middle, inset like the hover wash; a filled 1 px rect
                // on a whole pixel so it is one crisp line (it rides the menu's rise, like the rows).
                Color rule = palette::border(); rule.a *= appear;
                drawRoundedRect(t, Rect{r.x + 3, std::floor(y + kSepH * 0.5), r.w - 6, 1.0}, 0.0, Paint::filled(rule));
                continue;
            }
            // Per-item hover wash: each item cross-fades independently (R-G-3). Never on a disabled row.
            const double hv = mHover.amount(i) * appear;
            if (hv > 0.001 && it.enabled)
                drawRoundedRect(t, Rect{r.x + 3, y, r.w - 6, kItemH}, radius::control(),
                                Paint::filled(palette::whiteAlpha(0.08 * hv)));
            Color fg = it.enabled ? palette::foreground() : palette::mutedForeground();
            fg.a *= appear;
            t.setFill(fg);
            t.drawText(it.label, r.x + kPadX, y + kItemH * 0.5 + kFontPx * 0.35, kFontPx, font::sans());
            if (!it.shortcut.empty())
            {
                // Right-aligned by its own measured width; mono, muted (fainter when disabled).
                const double sw = t.measureText(it.shortcut, kShortcutPx, font::mono());
                const double sx = r.x + r.w - kPadX - sw;
                Color sc = palette::mutedForeground();
                sc.a *= appear * (it.enabled ? 1.0 : 0.55);
                t.setFill(sc);
                t.drawText(it.shortcut, sx, y + kItemH * 0.5 + kShortcutPx * 0.35, kShortcutPx, font::mono());
                // Published at rest (without the rise), in the menu's own coordinates.
                mShortcutRects[(size_t)i] = Rect{sx, y + (1.0 - appear) * 6.0, sw, kItemH};
            }
        }
    }
}
}
