#include "EditStackTabs.h"
#include "TextMetrics.h"
#include "../Theme.h"
#include <algorithm>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace { constexpr double kFontPx = 10.0; constexpr double kSlideMs = 200.0; constexpr double kFadeMs = 180.0; constexpr double kMinTabPad = 6.0; }

    EditStackTabs::EditStackTabs() { clipToBounds = false; }

    // Even padding added to each label so the tabs together fill the strip width;
    // falls back to a minimum pad if the labels are wider than the strip.
    double EditStackTabs::tabW(int i) const
    {
        const int n = (int)mTitles.size();
        if (n <= 0) return 0.0;
        double sumText = 0.0;
        for (const auto &s : mTitles) sumText += estimateTextWidth(s, kFontPx);
        const double pad = std::max(kMinTabPad, (width.value() - sumText) / (2.0 * n));
        return estimateTextWidth(mTitles[i], kFontPx) + 2.0 * pad;
    }

    double EditStackTabs::tabX(int i) const
    {
        double x = 0.0;
        for (int k = 0; k < i; ++k) x += tabW(k);
        return x;
    }

    int EditStackTabs::tabAt(double localX) const
    {
        for (int i = 0; i < (int)mTitles.size(); ++i)
            if (localX >= tabX(i) && localX < tabX(i) + tabW(i)) return i;
        return -1;
    }

    void EditStackTabs::addPage(const std::string &title, std::shared_ptr<Segment> page)
    {
        mTitles.push_back(title);
        mPages.push_back(page);
        addChild(page);
        layoutPages();
    }

    void EditStackTabs::setSelectedIndex(int index)
    {
        if (index < 0 || index >= (int)mTitles.size() || index == mSelected) return;
        mSelected = index;
        mPending = true;   // advance() starts the accent-bar slide
        layoutPages();
        if (onChange) onChange(index);
    }

    void EditStackTabs::layoutPages()
    {
        const double pageH = std::max(0.0, height.value() - tabHeight);
        for (int i = 0; i < (int)mPages.size(); ++i)
        {
            mPages[i]->x.set(0.0);
            mPages[i]->y.set(tabHeight);
            mPages[i]->width.set(width.value());
            mPages[i]->height.set(pageH);
            mPages[i]->visible = (i == mSelected);
        }
    }

    void EditStackTabs::advance(double nowMs)
    {
        const int n = (int)mTitles.size();
        if (n > 0)
        {
            const double targetX = tabX(mSelected), targetW = tabW(mSelected);
            if (!mInit) { mIndX.set(targetX); mIndW.set(targetW); mInit = true; }
            else if (mPending) { mIndX.animateTo(targetX, kSlideMs, Easing::EaseOutCubic, nowMs);
                                 mIndW.animateTo(targetW, kSlideMs, Easing::EaseOutCubic, nowMs);
                                 mFade.set(0.0);  // page swapped: cover it, then ease the cover away
                                 mFade.animateTo(1.0, kFadeMs, Easing::EaseOutCubic, nowMs);
                                 mPending = false; }
            mIndX.update(nowMs); mIndW.update(nowMs);
        }

        // Tab hover: drop it when the pointer isn't on the strip, then cross-fade
        // each tab's highlight independently (R-G-3).
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        mFade.update(nowMs);

        Segment::advance(nowMs);
    }

    void EditStackTabs::onPaint(IRenderTarget &t) const
    {
        const int n = (int)mTitles.size();
        if (n == 0) return;
        const double w = width.value();

        // Whole strip: the darker background; then the active tab in card, filled
        // down past the strip so it welds into the card panel body below.
        drawRoundedRect(t, Rect{0, 0, w, tabHeight}, 0.0, Paint::filled(palette::background()));
        drawRoundedRect(t, Rect{tabX(mSelected), 0, tabW(mSelected), tabHeight + 2.0}, 0.0, Paint::filled(palette::card()));

        // Faint wash on each hovered idle tab — each cross-fades independently.
        for (int i = 0; i < n; ++i)
        {
            if (i == mSelected) continue;
            const double hv = mHover.amount(i);
            if (hv > 0.001)
                drawRoundedRect(t, Rect{tabX(i), 0, tabW(i), tabHeight}, 0.0, Paint::filled(palette::whiteAlpha(0.06 * hv)));
        }

        // Bottom hairline across the idle tabs only (broken under the active tab,
        // which merges with the body).
        const double selL = tabX(mSelected), selR = selL + tabW(mSelected);
        t.beginPath(); t.moveTo(0, tabHeight); t.lineTo(selL, tabHeight);
        t.moveTo(selR, tabHeight); t.lineTo(w, tabHeight);
        t.setStroke(palette::border(), 1.0); t.strokePath();

        // Sliding accent bar along the top edge of the active tab.
        drawRoundedRect(t, Rect{mIndX.value(), 0.0, mIndW.value(), 1.5}, 0.0, Paint::filled(palette::primary()));

        for (int i = 0; i < n; ++i)
        {
            const bool active = (i == mSelected);
            Color col = active ? palette::primary() : palette::mutedForeground();
            if (!active)  // lift an idle label toward the active colour on hover (cross-fades)
                col = lerpColor(col, palette::primary(), 0.5 * mHover.amount(i));
            const double textW = estimateTextWidth(mTitles[i], kFontPx);
            const double tx = tabX(i) + (tabW(i) - textW) * 0.5;
            t.setFill(col);
            t.drawText(mTitles[i], tx, tabHeight * 0.5 + kFontPx * 0.35, kFontPx, font::sansMedium());
        }
    }

    void EditStackTabs::onOverlay(IRenderTarget &t) const
    {
        const double f = mFade.value();
        if (f >= 0.999) return;  // no page swap in progress
        const double w = width.value();
        const double pageH = std::max(0.0, height.value() - tabHeight);
        if (w <= 0.0 || pageH <= 0.0) return;

        // Cross-fade the swap: at the switch the incoming page (already drawn beneath
        // this overlay) is fully covered by the panel's own card surface, which then
        // eases away to reveal it — so tabs change without a hard pop (R-G-1). The
        // scrim spans only the page area, leaving the tab strip + accent bar visible.
        Color wash = palette::card();
        wash.a *= (1.0 - f);
        drawRoundedRect(t, Rect{0, tabHeight, w, pageH}, 0.0, Paint::filled(wash));
    }

    bool EditStackTabs::handleGesture(const Gesture &g, const Point &local)
    {
        const int n = (int)mTitles.size();
        if (g.type == Gesture::Type::Move)  // track the hovered tab (strip only; skip the active tab)
        {
            const int i = (n > 0 && local.y <= tabHeight) ? tabAt(local.x) : -1;
            mHover.setHovered((i >= 0 && i != mSelected) ? i : -1);
            return true;
        }
        if (g.type == Gesture::Type::Click && n > 0 && local.y <= tabHeight)
        {
            const int i = tabAt(local.x);
            if (i >= 0) { setSelectedIndex(i); return true; }
        }
        return Segment::handleGesture(g, local);
    }
}
}
