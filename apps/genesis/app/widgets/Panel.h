/*
 *  Genesis — shared drawing helpers for self-drawn panels.
 *
 *  A multi-region widget (a tree, a field list, a track list) paints many clickable rows in
 *  ONE Segment, so the framework's per-Segment hoverAmount() cannot tell them apart.
 *  RowHover gives each row its own eased 0..1 amount so moving between rows cross-fades
 *  instead of the highlight jumping — the same solution cosmo reached, kept here in one
 *  place so every Genesis panel hovers identically (design rule: one hover language).
 */
#pragma once
#include "../Theme.h"
#include "../TextUtil.h"
#include <algorithm>
#include <string>
#include <vector>

namespace genesis
{
namespace ui
{
    class RowHover
    {
    public:
        void setHovered(int id) { mTarget = id; }
        void clear() { mTarget = -1; }
        int hovered() const { return mTarget; }

        /** Ease every row toward its target (1 for the hovered id, 0 otherwise). */
        void advance(double nowMs, double durMs = artboard::interaction::kHoverMs)
        {
            if (mTarget >= (int)mAmount.size())
                mAmount.resize((size_t)mTarget + 1, 0.0);
            double step;
            if (artboard::reducedMotion() || durMs <= 0.0)
                step = 1.0;
            else
            {
                const double dt = mLastMs < 0.0 ? 0.0 : (nowMs - mLastMs);
                step = std::min(1.0, std::max(0.0, dt / durMs));
            }
            mLastMs = nowMs;
            for (int i = 0; i < (int)mAmount.size(); ++i)
            {
                const double target = (i == mTarget) ? 1.0 : 0.0;
                if (mAmount[(size_t)i] < target) mAmount[(size_t)i] = std::min(target, mAmount[(size_t)i] + step);
                else if (mAmount[(size_t)i] > target) mAmount[(size_t)i] = std::max(target, mAmount[(size_t)i] - step);
            }
        }

        /** Smoothstepped amount for a row. */
        double amount(int id) const
        {
            if (id < 0 || id >= (int)mAmount.size()) return 0.0;
            const double t = mAmount[(size_t)id];
            return t * t * (3.0 - 2.0 * t);
        }

    private:
        int mTarget = -1;
        double mLastMs = -1.0;
        std::vector<double> mAmount;
    };

    /*  A scrolling list, shared by every panel that clips (FR-47).
     *
     *  Content the user cannot reach and cannot see the existence of is a defect, so this
     *  bundles the three things that have to travel together: a clamped offset, wheel AND
     *  drag, and a scrollbar drawn only while there is something to scroll.
     */
    class ListScroll
    {
    public:
        /** Tell it the viewport and the content for this frame. */
        void measure(double viewportH, double contentH)
        {
            mViewport = viewportH;
            mContent = contentH;
            clamp();
        }
        double offset() const { return mOffset; }
        double maxOffset() const { return std::max(0.0, mContent - mViewport); }
        bool scrollable() const { return maxOffset() > 0.5; }

        /** Apply a wheel delta; false when there was nothing to scroll, so the gesture can
         *  bubble to a panel that can use it. */
        bool wheel(double deltaY)
        {
            if (!scrollable()) return false;
            mOffset += deltaY;
            clamp();
            return true;
        }
        /** Apply a drag; `dy` is the pointer's movement, so the content follows the finger. */
        void drag(double dy)
        {
            mOffset -= dy;
            clamp();
        }
        void reset() { mOffset = 0.0; }

        /** Draw the bar down the right edge of `area`. Nothing is drawn when everything fits —
         *  a permanent track would claim there is more when there is not. */
        void drawBar(artboard::IRenderTarget &t, const artboard::Rect &area) const
        {
            if (!scrollable()) return;
            const double w = 3.0;
            const double x = area.right() - w - 2.0;
            const double frac = mViewport / mContent;
            const double thumbH = std::max(24.0, area.h * frac);
            const double travel = area.h - thumbH;
            const double y = area.y + (maxOffset() > 0.0 ? travel * (mOffset / maxOffset()) : 0.0);
            artboard::drawRoundedRect(t, {x, area.y, w, area.h}, radius::pill(),
                                      artboard::Paint::filled(palette::whiteAlpha(0.05)));
            artboard::drawRoundedRect(t, {x, y, w, thumbH}, radius::pill(),
                                      artboard::Paint::filled(palette::whiteAlpha(0.20)));
        }

    private:
        void clamp() { mOffset = std::min(maxOffset(), std::max(0.0, mOffset)); }
        double mOffset = 0.0;
        double mViewport = 0.0;
        double mContent = 0.0;
    };

    /** Fill a panel surface with its border. */
    inline void drawSurface(artboard::IRenderTarget &t, const artboard::Rect &r,
                            const artboard::Color &fill, double radius = radius::panel())
    {
        artboard::drawRoundedRect(t, r, radius, artboard::Paint::filledStroked(fill, palette::border(), 1.0));
    }

    /** A section header: an uppercase micro label with generous tracking. */
    inline void drawSectionTitle(artboard::IRenderTarget &t, const std::string &label, double x, double y)
    {
        t.setFill(palette::mutedForeground());
        std::string upper = label;
        for (char &c : upper)
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        t.drawText(upper, x, y, type::micro(), font::sansSemiBold(), 1.1);
    }

    /** Left-aligned text, ellipsized to `maxW` so it can never spill (design rule R5). */
    inline void drawFitted(artboard::IRenderTarget &t, const std::string &s, double x, double baseline,
                           double maxW, double sizePx, const artboard::Color &c,
                           const char *family = nullptr, double tracking = 0.0)
    {
        t.setFill(c);
        t.drawText(ellipsize(s, maxW, sizePx, family, tracking), x, baseline, sizePx,
                   family ? family : font::sans(), tracking);
    }

    /** Right-aligned text within [x, x+maxW], ellipsized. */
    inline void drawFittedRight(artboard::IRenderTarget &t, const std::string &s, double x, double baseline,
                                double maxW, double sizePx, const artboard::Color &c,
                                const char *family = nullptr, double tracking = 0.0)
    {
        const std::string fit = ellipsize(s, maxW, sizePx, family, tracking);
        const double w = textWidth(fit, sizePx, family, tracking);
        t.setFill(c);
        t.drawText(fit, x + maxW - w, baseline, sizePx, family ? family : font::sans(), tracking);
    }

    /** A small filled chip with a label; returns its width. */
    inline double drawChip(artboard::IRenderTarget &t, const std::string &label, double x, double y,
                           double h, const artboard::Color &fill, const artboard::Color &fg)
    {
        const double padX = 7.0;
        const double w = textWidth(label, type::micro(), font::sansMedium()) + padX * 2.0;
        artboard::drawRoundedRect(t, artboard::Rect{x, y, w, h}, radius::hairline(),
                                  artboard::Paint::filled(fill));
        t.setFill(fg);
        t.drawText(label, x + padX, y + h * 0.5 + type::micro() * 0.36, type::micro(), font::sansMedium());
        return w;
    }

    /** The baseline that vertically centres `sizePx` text in a row of height `h` at `y`. */
    inline double centreBaseline(double y, double h, double sizePx) { return y + h * 0.5 + sizePx * 0.36; }
}
}
