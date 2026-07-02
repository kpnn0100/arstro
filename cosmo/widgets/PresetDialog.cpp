#include "PresetDialog.h"
#include "../CosmoTheme.h"

namespace arstro
{
namespace cosmo
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
    }

    PresetDialog::PresetDialog(const Color &accent) : mAccent(accent) {}

    void PresetDialog::show(const std::string &title, const std::string &confirmLabel,
                            std::vector<Row> rows, std::function<void(std::vector<std::string>)> onConfirm)
    {
        mTitle = title; mConfirmLabel = confirmLabel;
        mRows = std::move(rows); mOnConfirm = std::move(onConfirm);
        mOpen = true;
        raise();  // topmost for input + draws last (overlay)
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
        if (!mOpen) return false;
        if (g.type != Gesture::Type::Click) return true;  // consume everything else while modal

        const Point p = local;  // root child at origin -> local == world
        if (!cardRect().contains(p)) { mOpen = false; return true; }  // click outside cancels

        if (confirmRect().contains(p))
        {
            std::vector<std::string> sel;
            for (const auto &r : mRows) if (r.checked) sel.push_back(r.key);
            auto cb = mOnConfirm;  // copy: the callback may re-open the dialog
            mOpen = false;
            if (cb) cb(sel);
            return true;
        }
        if (cancelRect().contains(p)) { mOpen = false; return true; }

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
        if (!mOpen) return;
        // dim the whole screen behind the modal
        drawRoundedRect(t, Rect{0, 0, width.value(), height.value()}, 0.0, Paint::filled(Color{0, 0, 0, 0.45}));

        const Rect c = cardRect();
        drawRoundedRect(t, c, radius::panel(), Paint::filledStroked(palette::panel(), palette::line(), 1.0));

        // title
        t.setFill(palette::ink());
        for (double ox : {0.0, 0.4})  // faux-bold
            t.drawText(mTitle, c.x + kPad + ox, c.y + kPad + 16.0, 14.0);

        auto drawCheckRow = [&](const Rect &row, const std::string &label, bool checked, bool bold) {
            const Rect box{row.x, row.y + (row.h - kBox) * 0.5, kBox, kBox};
            drawRoundedRect(t, box, 3.0,
                            checked ? Paint::filled(mAccent)
                                    : Paint::filledStroked(palette::surface(), palette::line(), 1.0));
            if (checked)  // tick mark
            {
                t.beginPath();
                t.moveTo(box.x + 3.5, box.y + kBox * 0.55);
                t.lineTo(box.x + kBox * 0.42, box.y + kBox - 4.0);
                t.lineTo(box.x + kBox - 3.0, box.y + 4.0);
                t.setStroke(palette::bg(), 1.8);
                t.strokePath();
            }
            t.setFill(checked ? palette::ink() : palette::muted());
            const double ty = row.y + row.h * 0.5 + 4.0;
            t.drawText(label, box.x + kBox + 10.0, ty, 12.0);
            if (bold) t.drawText(label, box.x + kBox + 10.4, ty, 12.0);
        };

        // "Select all" master row
        drawCheckRow(selectAllRect(), "Select all", allChecked(), true);
        // hairline under it
        const Rect sa = selectAllRect();
        t.beginPath();
        t.moveTo(c.x + kPad, sa.y + kRowH + kGap * 0.5);
        t.lineTo(c.x + c.w - kPad, sa.y + kRowH + kGap * 0.5);
        t.setStroke(palette::line(), 1.0); t.strokePath();

        for (int i = 0; i < (int)mRows.size(); ++i)
            drawCheckRow(Rect{c.x + kPad, rowTop(i), c.w - 2 * kPad, kRowH}, mRows[i].label, mRows[i].checked, false);

        // footer buttons
        auto drawBtn = [&](const Rect &r, const std::string &label, bool primary) {
            drawRoundedRect(t, r, radius::control(),
                            primary ? Paint::filled(mAccent)
                                    : Paint::filledStroked(palette::surface(), palette::line(), 1.0));
            t.setFill(primary ? palette::bg() : palette::ink());
            const double tw = (double)label.size() * 6.6;  // rough centring
            t.drawText(label, r.x + (r.w - tw) * 0.5, r.y + r.h * 0.5 + 4.0, 12.0);
        };
        drawBtn(cancelRect(), "Cancel", false);
        drawBtn(confirmRect(), mConfirmLabel, true);
    }
}
}
