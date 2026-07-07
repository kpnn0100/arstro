#include "Breadcrumb.h"
#include "Icons.h"
#include "TextMetrics.h"
#include "../Theme.h"

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace { constexpr double kPadX = 9.75, kGap = 3.25, kFontPx = 10.0; }

    Breadcrumb::Breadcrumb() { height.set(kHeight); }

    void Breadcrumb::setPath(std::vector<std::string> crumbs) { mCrumbs = std::move(crumbs); }

    std::vector<Breadcrumb::Span> Breadcrumb::computeSpans() const
    {
        std::vector<Span> spans;
        double x = kPadX;
        for (size_t i = 0; i < mCrumbs.size(); ++i)
        {
            const double w = estimateTextWidth(mCrumbs[i], kFontPx);
            spans.push_back({x, w});
            x += w + kGap;
            if (i + 1 < mCrumbs.size()) x += 9.0 + kGap;  // chevron (9px) + its own gap
        }
        return spans;
    }

    int Breadcrumb::crumbAt(const Point &local) const
    {
        const auto spans = computeSpans();
        for (size_t i = 0; i + 1 < spans.size(); ++i)  // last crumb is inert
            if (local.x >= spans[i].x && local.x <= spans[i].x + spans[i].w)
                return (int)i;
        return -1;
    }

    void Breadcrumb::advance(double nowMs)
    {
        Segment::advance(nowMs);
        if (!isHovered()) mHoverIndex = -1;  // pointer left the strip
        const bool hov = mHoverIndex >= 0;
        if (hov != mHoverPrev)
        {
            mHoverPrev = hov;
            mHoverAmt.animateTo(hov ? 1.0 : 0.0, interaction::kHoverMs, Easing::EaseOutCubic, nowMs);
        }
        mHoverAmt.update(nowMs);
    }

    bool Breadcrumb::handleGesture(const Gesture &g, const Point &local)
    {
        if (g.type == Gesture::Type::Move) { mHoverIndex = crumbAt(local); return true; }
        if (g.type != Gesture::Type::Click) return Segment::handleGesture(g, local);
        const int i = crumbAt(local);
        if (i >= 0) { if (onCrumbClick) onCrumbClick(i); }
        return true;
    }

    void Breadcrumb::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = kHeight;
        drawRoundedRect(t, Rect{0, 0, w, h}, 0.0, Paint::filled(palette::leftRailBg()));
        t.beginPath(); t.moveTo(0, 0); t.lineTo(w, 0); t.setStroke(palette::border(), 1.0); t.strokePath();
        t.beginPath(); t.moveTo(0, h); t.lineTo(w, h); t.setStroke(palette::border(), 1.0); t.strokePath();

        const auto spans = computeSpans();
        const double baseline = h * 0.5 + kFontPx * 0.35;
        for (size_t i = 0; i < mCrumbs.size(); ++i)
        {
            const bool last = (i + 1 == mCrumbs.size());
            // A hovered (clickable) crumb lifts from muted toward foreground, eased.
            const double hv = ((int)i == mHoverIndex && !last) ? mHoverAmt.value() : 0.0;
            const Color crumbColor =
                last ? Color{palette::foreground().r, palette::foreground().g, palette::foreground().b, 0.8}
                     : lerpColor(palette::mutedForeground(), palette::foreground(), hv);
            t.setFill(crumbColor);
            t.drawText(mCrumbs[i], spans[i].x, baseline, kFontPx, font::sans());
            if (!last)
            {
                const double cx = spans[i].x + spans[i].w + kGap;
                icon::chevronRight(t, Rect{cx, h * 0.5 - 4.5, 9.0, 9.0},
                                    Color{palette::mutedForeground().r, palette::mutedForeground().g,
                                          palette::mutedForeground().b, 0.4},
                                    1.0);
            }
        }
    }
}
}
