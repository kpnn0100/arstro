#include "EditStackTabs.h"
#include "TextMetrics.h"
#include "../Theme.h"
#include <algorithm>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace { constexpr double kFontPx = 10.0; constexpr double kSlideMs = 200.0; constexpr double kFadeMs = 180.0; }

    EditStackTabs::EditStackTabs() { clipToBounds = false; }

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
            const double tw = width.value() / n;
            const double targetX = mSelected * tw, targetW = tw;
            if (!mInit) { mIndX.set(targetX); mIndW.set(targetW); mInit = true; }
            else if (mPending) { mIndX.animateTo(targetX, kSlideMs, Easing::EaseOutCubic, nowMs);
                                 mIndW.animateTo(targetW, kSlideMs, Easing::EaseOutCubic, nowMs);
                                 mFade.set(0.0);  // page swapped: cover it, then ease the cover away
                                 mFade.animateTo(1.0, kFadeMs, Easing::EaseOutCubic, nowMs);
                                 mPending = false; }
            mIndX.update(nowMs); mIndW.update(nowMs);
        }

        // Tab hover: drop it when the pointer isn't on the strip (this widget is no
        // longer the hover owner), then ease the highlight in/out (R-G-1).
        if (!isHovered()) mHoverIndex = -1;
        const bool hov = mHoverIndex >= 0;
        if (hov != mHoverPrev)
        {
            mHoverPrev = hov;
            mHoverAmt.animateTo(hov ? 1.0 : 0.0, interaction::kHoverMs, Easing::EaseOutCubic, nowMs);
        }
        mHoverAmt.update(nowMs);
        mFade.update(nowMs);

        Segment::advance(nowMs);
    }

    void EditStackTabs::onPaint(IRenderTarget &t) const
    {
        const int n = (int)mTitles.size();
        if (n == 0) return;
        const double w = width.value(), tw = w / n;

        // Whole strip: the darker background; then the active tab in card, filled
        // down past the strip so it welds into the card panel body below.
        drawRoundedRect(t, Rect{0, 0, w, tabHeight}, 0.0, Paint::filled(palette::background()));
        drawRoundedRect(t, Rect{mSelected * tw, 0, tw, tabHeight + 2.0}, 0.0, Paint::filled(palette::card()));

        // Faint wash on the hovered idle tab, eased by mHoverAmt so it never pops.
        const double hv = mHoverAmt.value();
        if (mHoverIndex >= 0 && mHoverIndex < n && mHoverIndex != mSelected && hv > 0.001)
            drawRoundedRect(t, Rect{mHoverIndex * tw, 0, tw, tabHeight}, 0.0,
                            Paint::filled(palette::whiteAlpha(0.06 * hv)));

        // Bottom hairline across the idle tabs only (broken under the active tab,
        // which merges with the body).
        t.beginPath(); t.moveTo(0, tabHeight); t.lineTo(mSelected * tw, tabHeight);
        t.moveTo((mSelected + 1) * tw, tabHeight); t.lineTo(w, tabHeight);
        t.setStroke(palette::border(), 1.0); t.strokePath();

        // Sliding accent bar along the top edge of the active tab.
        drawRoundedRect(t, Rect{mIndX.value(), 0.0, mIndW.value(), 1.5}, 0.0, Paint::filled(palette::primary()));

        for (int i = 0; i < n; ++i)
        {
            const bool active = (i == mSelected);
            Color col = active ? palette::primary() : palette::mutedForeground();
            if (!active && i == mHoverIndex)  // lift an idle label toward the active colour on hover
                col = lerpColor(col, palette::primary(), 0.5 * hv);
            const double textW = estimateTextWidth(mTitles[i], kFontPx);
            const double tx = i * tw + (tw - textW) * 0.5;
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
            int i = (n > 0 && local.y <= tabHeight) ? (int)(local.x / (width.value() / n)) : -1;
            mHoverIndex = (i >= 0 && i < n && i != mSelected) ? i : -1;
            return true;
        }
        if (g.type == Gesture::Type::Click && n > 0 && local.y <= tabHeight)
        {
            const int i = (int)(local.x / (width.value() / n));
            if (i >= 0 && i < n) { setSelectedIndex(i); return true; }
        }
        return Segment::handleGesture(g, local);
    }
}
}
