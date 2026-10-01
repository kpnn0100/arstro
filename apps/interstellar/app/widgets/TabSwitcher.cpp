#include "TabSwitcher.h"
#include "TextFit.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace interstellar_v1
{
    using namespace artboard;

    namespace { constexpr double kFontPx = 10.0; }

    TabSwitcher::TabSwitcher(std::vector<std::string> labels) : mLabels(std::move(labels))
    {
        height.set(kHeight);
        width.set(preferredWidth());
    }

    void TabSwitcher::setSelected(int index)
    {
        if (index < 0 || index >= (int)mLabels.size() || index == mSelected) return;
        mSelected = index;
        mPending = true;
    }

    void TabSwitcher::advance(double nowMs)
    {
        if (!mInit) { mPos.set(mSelected); mInit = true; mPending = false; }
        else if (mPending)
        {
            mPos.animateTo(mSelected, motion::kSlideMs, Easing::EaseOutCubic, nowMs);
            mPending = false;
        }
        mPos.update(nowMs);
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    int TabSwitcher::segmentAt(const Point &local) const
    {
        for (int i = 0; i < (int)mLabels.size(); ++i)
            if (segmentRect(i).contains(local)) return i;
        return -1;
    }

    bool TabSwitcher::handleGesture(const Gesture &g, const Point &local)
    {
        if (g.type == Gesture::Type::Move)
        {
            const int i = segmentAt(local);
            mHover.setHovered(i == mSelected ? -1 : i);
            return true;
        }
        if (g.type == Gesture::Type::Down) return true;   // capture, so the click comes back here
        if (g.type == Gesture::Type::Click)
        {
            const int i = segmentAt(local);
            if (i >= 0)
            {
                if (onSelect) onSelect(i);
                return true;
            }
        }
        return Segment::handleGesture(g, local);
    }

    void TabSwitcher::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, radius::control(),
                        Paint::filledStroked(palette::segmentedBg(), palette::border(), 1.0));

        const int n = (int)mLabels.size();
        if (n == 0) return;
        const double p = std::clamp(mPos.value(), 0.0, (double)(n - 1));
        // Hover washes under the highlight, per segment, cross-fading.
        for (int i = 0; i < n; ++i)
        {
            const double hv = mHover.amount(i);
            if (hv > 0.001) drawRoundedRect(t, segmentRect(i), radius::hairline(), Paint::filled(palette::hoverWash(hv)));
        }
        // The travelling highlight: one rect at the eased position.
        const Rect hi{kPad + p * kSegW, kPad, kSegW, h - 2 * kPad};
        drawRoundedRect(t, hi, radius::hairline(), Paint::filled(palette::primary()));

        for (int i = 0; i < n; ++i)
        {
            // Coverage of segment i by the highlight, 0..1, from the EASED position — so the label
            // colour slides with the fill rather than switching when it lands.
            const double cover = std::max(0.0, 1.0 - std::fabs(p - i));
            Color c = lerpColor(palette::mutedForeground(), palette::foreground(), 0.55 * mHover.amount(i));
            c = lerpColor(c, palette::primaryForeground(), cover);
            const Rect r = segmentRect(i);
            const std::string label = textfit::ellipsize(t, mLabels[i], r.w - 8.0, kFontPx, font::sansMedium());
            const double tw = t.measureText(label, kFontPx, font::sansMedium());
            t.setFill(c);
            t.drawText(label, r.x + (r.w - tw) * 0.5, textfit::baseline(r.y + r.h * 0.5, kFontPx), kFontPx, font::sansMedium());
        }
    }
}
}
