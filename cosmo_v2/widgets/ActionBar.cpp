#include "ActionBar.h"
#include "Icons.h"
#include "TextMetrics.h"
#include "../Theme.h"

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace
    {
        constexpr double kPadX = 9.75, kPadY = 8.125, kGap = 4.875, kBtnH = 20.75;  // 10px*1.3+2*4.875(py-1.5)
        const char *kLabels[3] = {"Save", "Import", "Export"};
    }

    ActionBar::ActionBar() { height.set(kHeight); }

    void ActionBar::layout()
    {
        const double w = width.value();
        const double totalGap = kGap * 2;
        const double btnW = (w - 2 * kPadX - totalGap) / 3.0;
        for (int i = 0; i < 3; ++i) { mBtns[i].x = kPadX + i * (btnW + kGap); mBtns[i].w = btnW; }
    }

    bool ActionBar::handleGesture(const Gesture &g, const Point &local)
    {
        if (g.type != Gesture::Type::Click) return Segment::handleGesture(g, local);
        const double y = (kHeight - kBtnH) * 0.5;
        for (int i = 0; i < 3; ++i)
        {
            if (local.x >= mBtns[i].x && local.x <= mBtns[i].x + mBtns[i].w && local.y >= y && local.y <= y + kBtnH)
            {
                if (i == 0 && onSave) onSave();
                else if (i == 1 && onImport) onImport();
                else if (i == 2 && onExport) onExport();
                return true;
            }
        }
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
            const bool primary = (i == 0);
            drawRoundedRect(t, btn, 2.0,
                            primary ? Paint::filled(palette::primary())
                                    : Paint::filledStroked(Color{0, 0, 0, 0}, palette::border(), 1.0));

            const Color fg = primary ? palette::white() : palette::mutedForeground();
            const std::string label = kLabels[i];
            const double iconSize = 10.0, gap = 4.875;
            const double labelW = estimateTextWidth(label, 10.0);
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
