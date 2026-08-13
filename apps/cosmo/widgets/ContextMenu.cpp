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
        double clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
    }

    void ContextMenu::open(std::vector<Item> items, double x, double y)
    {
        mItems = std::move(items);
        mX = x;
        mY = y;
        mRenaming = false;
        mRename.set(0.0);
        mOpen = !mItems.empty();
        if (mOpen) raise();  // topmost -> wins hit-testing over body widgets it overlaps
    }

    void ContextMenu::enterRenameMode(const std::string &currentName)
    {
        // Called by the "Rename Group" item's action (which already cleared mOpen): the
        // menu's items collapse to a "Rename" header, then the field grows below.
        mPreRenameItemsH = (double)mItems.size() * kItemH;  // shrink from the current list
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

    Rect ContextMenu::menuRect() const
    {
        double w = kMinW;
        if (mRenaming)
            w = kRenameW;
        else
            for (const auto &it : mItems) w = std::max(w, estimateTextWidth(it.label, kFontPx) + 2 * kPadX);
        const double contentH = mRenaming ? renameContentH() : (double)mItems.size() * kItemH;
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
        if (g.type == T::Move) { mHover.setHovered(itemAt(local)); return true; }  // track hovered item
        if (g.type == T::Down) return true;  // consume so the press doesn't fall through
        if (g.type == T::Click || g.type == T::RightClick)
        {
            const int i = itemAt(local);
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
                    const double y = r.y + kPadY + i * kItemH;
                    Color fg = palette::foreground(); fg.a *= itemsA;
                    t.setFill(fg);
                    t.drawText(mItems[i].label, r.x + kPadX, y + kItemH * 0.5 + kFontPx * 0.35, kFontPx, font::sans());
                }

            // "Rename" header fades in as the items fade out.
            const double headerA = clamp01((amt - 0.1) / 0.4) * appear;
            if (headerA > 0.01)
            {
                Color fg = palette::mutedForeground(); fg.a *= headerA;
                t.setFill(fg);
                t.drawText("Rename", r.x + kPadX, r.y + kPadY + kHeaderH * 0.5 + kFontPx * 0.35, kFontPx, font::sans());
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
            const double y = r.y + kPadY + i * kItemH;
            // Per-item hover wash: each item cross-fades independently (R-G-3).
            const double hv = mHover.amount(i) * appear;
            if (hv > 0.001)
                drawRoundedRect(t, Rect{r.x + 3, y, r.w - 6, kItemH}, radius::control(),
                                Paint::filled(palette::whiteAlpha(0.08 * hv)));
            Color fg = palette::foreground();
            fg.a *= appear;
            t.setFill(fg);
            t.drawText(mItems[i].label, r.x + kPadX, y + kItemH * 0.5 + kFontPx * 0.35, kFontPx, font::sans());
        }
    }
}
}
