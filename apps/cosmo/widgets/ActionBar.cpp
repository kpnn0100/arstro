#include "ActionBar.h"
#include "Icons.h"
#include "TextFit.h"
#include "../Theme.h"

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace
    {
        constexpr double kPadX = 9.75, kPadY = 8.125, kGap = 4.875, kBtnH = 20.75;  // 10px*1.3+2*4.875(py-1.5)
        // R-UX-4: these act on PRESETS. "Save / Import / Export" with a filled Save read as the
        // project's save and the photo's export, and were neither.
        const char *kLabels[3] = {"Save preset", "Import preset", "Export preset"};
    }

    ActionBar::ActionBar() { height.set(kHeight); }

    void ActionBar::layout()
    {
        const double w = width.value();
        const double totalGap = kGap * 2;
        const double btnW = (w - 2 * kPadX - totalGap) / 3.0;
        for (int i = 0; i < 3; ++i) { mBtns[i].x = kPadX + i * (btnW + kGap); mBtns[i].w = btnW; }
    }

    Rect ActionBar::buttonRect(int i) const
    {
        if (i < 0 || i > 2) return Rect{0, 0, 0, 0};
        return Rect{mBtns[i].x, (kHeight - kBtnH) * 0.5, mBtns[i].w, kBtnH};
    }

    int ActionBar::btnAt(const Point &local) const
    {
        const double y = (kHeight - kBtnH) * 0.5;
        if (local.y < y || local.y > y + kBtnH) return -1;
        for (int i = 0; i < 3; ++i)
            if (local.x >= mBtns[i].x && local.x <= mBtns[i].x + mBtns[i].w) return i;
        return -1;
    }

    void ActionBar::advance(double nowMs)
    {
        Segment::advance(nowMs);
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
    }

    bool ActionBar::handleGesture(const Gesture &g, const Point &local)
    {
        if (g.type == Gesture::Type::Move) { mHover.setHovered(btnAt(local)); return true; }
        if (g.type != Gesture::Type::Click) return Segment::handleGesture(g, local);
        const int i = btnAt(local);
        if (i == 0 && onSave) onSave();
        else if (i == 1 && onImport) onImport();
        else if (i == 2 && onExport) onExport();
        return true;
    }

    void ActionBar::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = kHeight;
        drawRoundedRect(t, Rect{0, 0, w, h}, 0.0, Paint::filled(palette::background()));
        t.beginPath(); t.moveTo(0, 0); t.lineTo(w, 0); t.setStroke(palette::border(), 1.0); t.strokePath();

        const double y = (h - kBtnH) * 0.5;
        for (int i = 0; i < 3; ++i)
        {
            const Rect btn{mBtns[i].x, y, mBtns[i].w, kBtnH};
            const double hv = mHover.amount(i);

            // R-UX-4: three outline chips, none filled - the accent fill is the editor's ONE
            // primary action, and that is exporting photos (the top bar), not saving a preset.
            // Hover pulls the border toward the accent with a faint wash (R-G-3, via HoverFade).
            const BoxStyle base{Paint::filledStroked(Color{0, 0, 0, 0}, palette::border(), 1.0), 2.0};
            BoxStyle drawn = hoverBox(base, palette::primary(), hv);
            if (hv > 0.0) drawn.paint.fill = palette::primaryAlpha(0.10 * hv);
            drawRoundedRect(t, btn, radius::control(), drawn.paint);

            const Color fg = lerpColor(palette::mutedForeground(), palette::foreground(), 0.6 * hv);
            const double iconSize = 10.0, gap = 4.875;
            // Measured, and ellipsized to the button if a narrow column ever squeezes it (R5).
            const std::string label = fitTextEnd(t, kLabels[i], btn.w - iconSize - gap - 2 * 4.875, 10.0, font::sansMedium());
            const double labelW = t.measureText(label, 10.0, font::sansMedium());
            const double groupW = iconSize + gap + labelW;
            const double gx = btn.x + (btn.w - groupW) * 0.5;
            const Rect iconBox{gx, btn.y + (kBtnH - iconSize) * 0.5, iconSize, iconSize};
            if (i == 0) icon::save(t, iconBox, fg);
            else if (i == 1) icon::upload(t, iconBox, fg);
            else icon::download(t, iconBox, fg);

            t.setFill(fg);
            t.drawText(label, gx + iconSize + gap, btn.y + kBtnH * 0.5 + 10.0 * 0.35, 10.0, font::sansMedium());
        }
    }
}
}
