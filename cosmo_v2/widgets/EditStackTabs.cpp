#include "EditStackTabs.h"
#include "TextMetrics.h"
#include "../Theme.h"
#include <algorithm>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace { constexpr double kFontPx = 10.0; constexpr double kSlideMs = 200.0; }

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
                                 mIndW.animateTo(targetW, kSlideMs, Easing::EaseOutCubic, nowMs); mPending = false; }
            mIndX.update(nowMs); mIndW.update(nowMs);
        }
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
            const Color col = active ? palette::primary() : palette::mutedForeground();
            const double textW = estimateTextWidth(mTitles[i], kFontPx);
            const double tx = i * tw + (tw - textW) * 0.5;
            t.setFill(col);
            t.drawText(mTitles[i], tx, tabHeight * 0.5 + kFontPx * 0.35, kFontPx, font::sansMedium());
        }
    }

    bool EditStackTabs::handleGesture(const Gesture &g, const Point &local)
    {
        const int n = (int)mTitles.size();
        if (g.type == Gesture::Type::Click && n > 0 && local.y <= tabHeight)
        {
            const int i = (int)(local.x / (width.value() / n));
            if (i >= 0 && i < n) { setSelectedIndex(i); return true; }
        }
        return Segment::handleGesture(g, local);
    }
}
}
