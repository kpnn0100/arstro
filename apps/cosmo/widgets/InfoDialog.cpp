#include "InfoDialog.h"
#include "TextMetrics.h"
#include "../Theme.h"
#include <algorithm>
#include <cctype>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace
    {
        // ConfirmDialog's scale, deliberately: two dialogs a click apart that pad differently
        // read as two applications.
        constexpr double kPad = 20.0;
        constexpr double kTitleH = 26.0;
        constexpr double kFooterGap = 16.0;
        constexpr double kBtnH = 28.0, kBtnPadX = 14.0;
        constexpr double kFontPx = 11.0;
        constexpr double kLabelW = 110.5;    // 34 spacing units — fits "Exposure bias" at 11px
        constexpr double kCloseBtn = 22.0;
        constexpr double kThumbW = 3.0;

        inline Color fade(Color c, double a) { return Color{c.r, c.g, c.b, c.a * a}; }

        /** Numerics and filenames get the mono face; words do not (§1.3's usage split). Decided
         *  from the VALUE rather than carried per row, because the service's job is to say what
         *  the file holds, not how a font should be picked — and a label list is exactly where a
         *  presentation flag would have leaked across R-SVC-4. */
        bool looksNumeric(const std::string &v)
        {
            if (v.empty()) return false;
            const unsigned char c0 = (unsigned char)v[0];
            if (std::isdigit(c0) || c0 == '+' || c0 == '-') return true;
            return v.rfind("f/", 0) == 0 || v.rfind("ISO ", 0) == 0 || v.rfind("R ", 0) == 0;
        }

        /** Fit `s` into `maxW` at `px`, ellipsising the END — or the START when `fromLeft`, which
         *  is what a path wants: "/home/…/2026/shoot/DSC_0041.ARW" keeps the half that identifies
         *  the file. R5: no text in this dialog may run past the card. */
        std::string fit(const std::string &s, double px, double maxW, bool fromLeft)
        {
            if (estimateTextWidth(s, px) <= maxW) return s;
            const std::string dots = "\xE2\x80\xA6";   // U+2026
            const double dotsW = estimateTextWidth("...", px);   // the estimate is per-char
            if (maxW <= dotsW) return dots;
            const std::size_t keep = (std::size_t)std::max(0.0, (maxW - dotsW) / (px * 0.6));
            if (keep >= s.size()) return s;
            return fromLeft ? dots + s.substr(s.size() - keep) : s.substr(0, keep) + dots;
        }
    }

    void InfoDialog::show(const std::string &title, std::vector<Row> rows)
    {
        mTitle = title;
        mRows = std::move(rows);
        mScroll.set(0.0);
        mScrollTarget = mScrollLastTarget = 0.0;
        mOpen = true;
        mClosing = false;
        mAppear.animateTo(1.0, 150.0, Easing::EaseOutCubic, mLastMs);
        raise();
    }

    std::string InfoDialog::valueOf(const std::string &label) const
    {
        for (const Row &r : mRows)
            if (r.first == label) return r.second;
        return {};
    }

    bool InfoDialog::handleKey(const KeyEvent &e)
    {
        if (!isOpen() || e.type != KeyEvent::Type::Down) return false;
        constexpr int kEsc = 0x1B, kReturn = 0x0D, kEnter = 0x0A;
        if (e.keyCode == kEsc || e.keyCode == kReturn || e.keyCode == kEnter)
        {
            beginClose();
            return true;
        }
        return true;   // a modal that lets keys through to the editor behind it is one in name only
    }

    void InfoDialog::scrollBy(double delta)
    {
        const double maxScroll = std::max(0.0, (double)mRows.size() * kRowH - bodyHeight());
        mScrollTarget = std::min(maxScroll, std::max(0.0, mScrollTarget - delta));
    }

    void InfoDialog::beginClose()
    {
        mClosing = true;
        mAppear.animateTo(0.0, 120.0, Easing::EaseOutCubic, mLastMs);
    }

    void InfoDialog::advance(double nowMs)
    {
        mLastMs = nowMs;
        mAppear.update(nowMs);
        if (mScrollTarget != mScrollLastTarget)
        {
            mScroll.animateTo(mScrollTarget, 180.0, Easing::EaseOutCubic, nowMs);
            mScrollLastTarget = mScrollTarget;
        }
        mScroll.update(nowMs);
        if (mClosing && !mAppear.isAnimating()) { mOpen = false; mClosing = false; }
        if (!isOpen()) mHover.clear();
        mHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    double InfoDialog::bodyHeight() const
    {
        const double want = (double)mRows.size() * kRowH;
        // Never taller than the window can hold either: the cap is what keeps the card on a
        // 720p screen, and a 15-row cap alone would not (R4).
        const double room = height.value() - 2 * kPad - kTitleH - kFooterGap - kBtnH - 2 * kPad;
        return std::max(kRowH, std::min(std::min(want, kMaxBodyH), std::max(kRowH, room)));
    }

    Rect InfoDialog::cardRect() const
    {
        const double h = kPad + kTitleH + bodyHeight() + kFooterGap + kBtnH + kPad;
        const double x = (width.value() - kCardW) * 0.5;
        const double y = (height.value() - h) * 0.5;
        return Rect{x < 4 ? 4 : x, y < 4 ? 4 : y, kCardW, h};
    }

    Rect InfoDialog::bodyRect() const
    {
        const Rect c = cardRect();
        return Rect{c.x + kPad, c.y + kPad + kTitleH, c.w - 2 * kPad, bodyHeight()};
    }

    Rect InfoDialog::closeRect() const
    {
        const Rect c = cardRect();
        return Rect{c.x + c.w - kPad - kCloseBtn, c.y + kPad - 3.0, kCloseBtn, kCloseBtn};
    }

    Rect InfoDialog::buttonRect() const
    {
        const Rect c = cardRect();
        const double w = estimateTextWidth("Close", kFontPx) + 2 * kBtnPadX;
        return Rect{c.x + c.w - kPad - w, c.y + c.h - kPad - kBtnH, w, kBtnH};
    }

    bool InfoDialog::handleGesture(const Gesture &g, const Point &local)
    {
        if (!mOpen || mClosing) return false;
        if (g.type == Gesture::Type::Move)
        {
            mHover.setHovered(closeRect().contains(local) ? 0 : (buttonRect().contains(local) ? 1 : -1));
            return true;
        }
        if (g.type != Gesture::Type::Click) return true;
        if (!cardRect().contains(local)) { beginClose(); return true; }      // outside = dismiss
        if (closeRect().contains(local) || buttonRect().contains(local)) beginClose();
        return true;
    }

    void InfoDialog::onOverlay(IRenderTarget &t) const
    {
        const double a = mAppear.value();
        if (!mOpen || a <= 0.001) return;

        drawRoundedRect(t, Rect{0, 0, width.value(), height.value()}, 0.0,
                        Paint::filled(fade(Color{0, 0, 0, 0.55}, a)));

        const Rect c = cardRect();
        drawRoundedRect(t, c, radius::control(),
                        Paint::filledStroked(fade(palette::popover(), a), fade(palette::border(), a), 1.0));

        // Title: the file's name, which is a filename — hence the mono face (§1.3).
        t.setFill(fade(palette::foreground(), a));
        t.drawText(fit(mTitle, 13.0, c.w - 2 * kPad - kCloseBtn - 6.0, true),
                   c.x + kPad, c.y + kPad + 13.0 * 0.35 + 8.0, 13.0, font::monoMedium());

        // The X, hover-brightened like every other icon button.
        const Rect x = closeRect();
        const double xhv = mHover.amount(0) * a;
        if (xhv > 0.001) drawRoundedRect(t, x, radius::control(), Paint::filled(palette::hoverWash(xhv)));
        t.setStroke(fade(brighten(palette::mutedForeground(), 0.4 * mHover.amount(0)), a), 1.3);
        t.beginPath();
        t.moveTo(x.x + 7, x.y + 7); t.lineTo(x.x + kCloseBtn - 7, x.y + kCloseBtn - 7);
        t.moveTo(x.x + kCloseBtn - 7, x.y + 7); t.lineTo(x.x + 7, x.y + kCloseBtn - 7);
        t.strokePath();

        const Rect b = bodyRect();
        t.save();
        t.clipRect(b.x, b.y, b.w, b.h);
        const double off = mScroll.value();
        const double valueX = b.x + kLabelW;
        const double valueW = b.w - kLabelW - (mRows.size() * kRowH > b.h ? kThumbW + 4.0 : 0.0);
        for (std::size_t i = 0; i < mRows.size(); ++i)
        {
            const double ry = b.y + (double)i * kRowH - off;
            if (ry + kRowH < b.y - 1 || ry > b.y + b.h + 1) continue;   // cull, as every list here does
            const double baseline = ry + kRowH * 0.5 + kFontPx * 0.35;
            t.setFill(fade(palette::mutedForeground(), a));
            t.drawText(fit(mRows[i].first, kFontPx, kLabelW - 6.0, false), b.x, baseline, kFontPx, font::sans());
            t.setFill(fade(palette::foreground(), a));
            const bool path = mRows[i].first == "Path";
            const std::string &v = mRows[i].second;
            t.drawText(fit(v, kFontPx, valueW, path), valueX, baseline, kFontPx,
                       looksNumeric(v) || path || mRows[i].first == "File" ? font::mono() : font::sans());
        }
        t.restore();

        // The thumb, only when there is something below the fold — a scrollbar on a list that
        // fits is furniture.
        const double content = (double)mRows.size() * kRowH;
        if (content > b.h)
        {
            const double frac = b.h / content;
            const double th = std::max(18.0, b.h * frac);
            const double maxScroll = content - b.h;
            const double ty = b.y + (maxScroll <= 0 ? 0.0 : (off / maxScroll) * (b.h - th));
            drawRoundedRect(t, Rect{b.x + b.w - kThumbW, ty, kThumbW, th}, radius::pill(),
                            Paint::filled(fade(palette::whiteAlpha(0.18), a)));
        }

        // Footer: one button, and it is the same shape as ConfirmDialog's cancel.
        const Rect btn = buttonRect();
        drawRoundedRect(t, btn, radius::control(),
                        Paint::filledStroked(fade(palette::secondary(), a), fade(palette::border(), a), 1.0));
        const double bhv = mHover.amount(1) * a;
        if (bhv > 0.001) drawRoundedRect(t, btn, radius::control(), Paint::filled(palette::hoverWash(bhv)));
        t.setFill(fade(palette::foreground(), a));
        const double tw = estimateTextWidth("Close", kFontPx);
        t.drawText("Close", btn.x + (btn.w - tw) * 0.5, btn.y + btn.h * 0.5 + kFontPx * 0.35, kFontPx,
                   font::sansMedium());
    }
}
}
