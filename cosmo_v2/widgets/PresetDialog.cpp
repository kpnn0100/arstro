#include "PresetDialog.h"
#include "TextMetrics.h"
#include "../Theme.h"

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace
    {
        constexpr double kCardW = 340.0;
        constexpr double kPad = 18.0;
        constexpr double kHeaderH = 34.0;   // title band
        constexpr double kRowH = 26.0;      // a checkbox row
        constexpr double kGap = 8.0;        // between sections
        constexpr double kFooterH = 40.0;   // Cancel / Confirm band
        constexpr double kBtnW = 92.0, kBtnH = 26.0;
        constexpr double kBox = 15.0;        // checkbox side
        constexpr double kFontPx = 12.0;

        // Fade a colour's alpha by `a` (the appear progress) so the whole modal
        // cross-fades in/out rather than popping (R-G-1).
        inline Color fade(Color c, double a) { return Color{c.r, c.g, c.b, c.a * a}; }
    }

    PresetDialog::PresetDialog(const Color &accent) : mAccent(accent) {}

    void PresetDialog::show(const std::string &title, const std::string &confirmLabel,
                            std::vector<Row> rows, std::function<void(std::vector<std::string>)> onConfirm)
    {
        mTitle = title; mConfirmLabel = confirmLabel;
        mRows = std::move(rows); mOnConfirm = std::move(onConfirm);
        mOpen = true; mClosing = false;
        mAppear.animateTo(1.0, 150.0, Easing::EaseOutCubic, mLastMs);
        raise();  // topmost for input + draws last (overlay)
    }

    void PresetDialog::beginClose()
    {
        mClosing = true;
        mAppear.animateTo(0.0, 120.0, Easing::EaseOutCubic, mLastMs);
    }

    void PresetDialog::advance(double nowMs)
    {
        mLastMs = nowMs;
        mAppear.update(nowMs);
        if (mClosing && !mAppear.isAnimating()) { mOpen = false; mClosing = false; }
        Segment::advance(nowMs);
    }

    Rect PresetDialog::cardRect() const
    {
        const double n = (double)mRows.size();
        const double h = kPad + kHeaderH + kRowH /*select all*/ + kGap + n * kRowH + kGap + kFooterH + kPad;
        const double x = (width.value() - kCardW) * 0.5;
        const double y = (height.value() - h) * 0.5;
        return Rect{x < 4 ? 4 : x, y < 4 ? 4 : y, kCardW, h};
    }

    Rect PresetDialog::selectAllRect() const
    {
        const Rect c = cardRect();
        return Rect{c.x + kPad, c.y + kPad + kHeaderH, c.w - 2 * kPad, kRowH};
    }

    double PresetDialog::rowTop(int i) const
    {
        const Rect a = selectAllRect();
        return a.y + kRowH + kGap + i * kRowH;
    }

    Rect PresetDialog::confirmRect() const
    {
        const Rect c = cardRect();
        return Rect{c.x + c.w - kPad - kBtnW, c.y + c.h - kPad - kBtnH, kBtnW, kBtnH};
    }
    Rect PresetDialog::cancelRect() const
    {
        const Rect c = cardRect();
        return Rect{c.x + c.w - kPad - 2 * kBtnW - 8.0, c.y + c.h - kPad - kBtnH, kBtnW, kBtnH};
    }

    bool PresetDialog::allChecked() const
    {
        for (const auto &r : mRows) if (!r.checked) return false;
        return true;
    }

    bool PresetDialog::handleGesture(const Gesture &g, const Point &local)
    {
        if (!mOpen || mClosing) return false;
        if (g.type != Gesture::Type::Click) return true;  // consume everything else while modal

        const Point p = local;  // root child at origin -> local == world
        if (!cardRect().contains(p)) { beginClose(); return true; }  // click outside cancels

        if (confirmRect().contains(p))
        {
            std::vector<std::string> sel;
            for (const auto &r : mRows) if (r.checked) sel.push_back(r.key);
            auto cb = mOnConfirm;  // copy: the callback may re-open the dialog
            beginClose();
            if (cb) cb(sel);
            return true;
        }
        if (cancelRect().contains(p)) { beginClose(); return true; }

        if (selectAllRect().contains(p))
        {
            const bool want = !allChecked();  // if any unticked -> tick all; else untick all
            for (auto &r : mRows) r.checked = want;
            return true;
        }
        for (int i = 0; i < (int)mRows.size(); ++i)
        {
            const Rect row{cardRect().x + kPad, rowTop(i), kCardW - 2 * kPad, kRowH};
            if (row.contains(p)) { mRows[i].checked = !mRows[i].checked; return true; }
        }
        return true;  // inside the card but not on a control: consume
    }

    void PresetDialog::onOverlay(IRenderTarget &t) const
    {
        const double a = mAppear.value();
        if (!mOpen || a <= 0.001) return;

        // dim the whole screen behind the modal (fades with the card)
        drawRoundedRect(t, Rect{0, 0, width.value(), height.value()}, 0.0, Paint::filled(fade(Color{0, 0, 0, 0.55}, a)));

        const Rect c = cardRect();
        drawRoundedRect(t, c, radius::control(),
                        Paint::filledStroked(fade(palette::popover(), a), fade(palette::border(), a), 1.0));

        // title
        t.setFill(fade(palette::foreground(), a));
        t.drawText(mTitle, c.x + kPad, c.y + kPad + 16.0, 14.0, font::sansSemiBold());

        auto drawCheckRow = [&](const Rect &row, const std::string &label, bool checked, bool bold) {
            const Rect box{row.x, row.y + (row.h - kBox) * 0.5, kBox, kBox};
            drawRoundedRect(t, box, 3.0,
                            checked ? Paint::filled(fade(mAccent, a))
                                    : Paint::filledStroked(fade(palette::secondary(), a), fade(palette::border(), a), 1.0));
            if (checked)  // tick mark
            {
                t.beginPath();
                t.moveTo(box.x + 3.5, box.y + kBox * 0.55);
                t.lineTo(box.x + kBox * 0.42, box.y + kBox - 4.0);
                t.lineTo(box.x + kBox - 3.0, box.y + 4.0);
                t.setStroke(fade(palette::primaryForeground(), a), 1.8);
                t.strokePath();
            }
            t.setFill(fade(checked ? palette::foreground() : palette::mutedForeground(), a));
            const double ty = row.y + row.h * 0.5 + 4.0;
            t.drawText(label, box.x + kBox + 10.0, ty, kFontPx, bold ? font::sansMedium() : font::sans());
        };

        // "Select all" master row
        drawCheckRow(selectAllRect(), "Select all", allChecked(), true);
        // hairline under it
        const Rect sa = selectAllRect();
        t.beginPath();
        t.moveTo(c.x + kPad, sa.y + kRowH + kGap * 0.5);
        t.lineTo(c.x + c.w - kPad, sa.y + kRowH + kGap * 0.5);
        t.setStroke(fade(palette::border(), a), 1.0); t.strokePath();

        for (int i = 0; i < (int)mRows.size(); ++i)
            drawCheckRow(Rect{c.x + kPad, rowTop(i), c.w - 2 * kPad, kRowH}, mRows[i].label, mRows[i].checked, false);

        // footer buttons
        auto drawBtn = [&](const Rect &r, const std::string &label, bool primary) {
            drawRoundedRect(t, r, radius::control(),
                            primary ? Paint::filled(fade(mAccent, a))
                                    : Paint::filledStroked(fade(palette::secondary(), a), fade(palette::border(), a), 1.0));
            t.setFill(fade(primary ? palette::primaryForeground() : palette::foreground(), a));
            const double tw = estimateTextWidth(label, kFontPx);
            t.drawText(label, r.x + (r.w - tw) * 0.5, r.y + r.h * 0.5 + 4.0, kFontPx, font::sansMedium());
        };
        drawBtn(cancelRect(), "Cancel", false);
        drawBtn(confirmRect(), mConfirmLabel, true);
    }
}
}
