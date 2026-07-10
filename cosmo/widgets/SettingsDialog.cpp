#include "SettingsDialog.h"
#include "TextMetrics.h"
#include "../Theme.h"
#include <string>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    const std::vector<int> SettingsDialog::kEdges{1000, 1600, 2400};
    const std::vector<int> SettingsDialog::kThreads{0, 2, 4, 8};

    namespace
    {
        constexpr double kCardW = 360.0;
        constexpr double kPad = 18.0;
        constexpr double kHeaderH = 34.0;
        constexpr double kLabelH = 18.0;
        constexpr double kChipH = 24.0;
        constexpr double kRowGap = 16.0;     // between the two setting blocks
        constexpr double kChipGap = 6.0;
        constexpr double kChipPadX = 12.0;
        constexpr double kFooterH = 40.0;
        constexpr double kBtnW = 92.0, kBtnH = 26.0;
        constexpr double kFontPx = 12.0;
        constexpr double kBlockH = kLabelH + 6.0 + kChipH;  // label + gap + chips

        inline Color fade(Color c, double a) { return Color{c.r, c.g, c.b, c.a * a}; }

        std::string edgeLabel(int i) { return i == 0 ? "Draft" : i == 1 ? "Standard" : "High"; }
        std::string threadLabel(int v) { return v == 0 ? "Auto" : std::to_string(v); }
        double blockTop(const Rect &c, int row) { return c.y + kPad + kHeaderH + row * (kBlockH + kRowGap); }
    }

    SettingsDialog::SettingsDialog(const Color &accent) : mAccent(accent) {}

    void SettingsDialog::show(int previewEdge, int threads)
    {
        mEdge = previewEdge; mThreadCount = threads;
        mOpen = true; mClosing = false;
        mAppear.animateTo(1.0, 150.0, Easing::EaseOutCubic, mLastMs);
        raise();
    }

    void SettingsDialog::beginClose()
    {
        mClosing = true;
        mAppear.animateTo(0.0, 120.0, Easing::EaseOutCubic, mLastMs);
    }

    void SettingsDialog::advance(double nowMs)
    {
        mLastMs = nowMs;
        mAppear.update(nowMs);
        if (mClosing && !mAppear.isAnimating()) { mOpen = false; mClosing = false; }
        if (!isOpen()) mHover.clear();
        mHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    void SettingsDialog::hitTargets(const Point &p, int &row, int &chip, bool &done) const
    {
        row = -1; chip = -1; done = false;
        if (doneRect().contains(p)) { done = true; return; }
        for (int r = 0; r < 2; ++r)
        {
            std::vector<Rect> chips; chipRects(r, chips);
            for (int i = 0; i < (int)chips.size(); ++i)
                if (chips[i].contains(p)) { row = r; chip = i; return; }
        }
    }

    Rect SettingsDialog::cardRect() const
    {
        const double h = kPad + kHeaderH + 2 * kBlockH + kRowGap + kRowGap + kFooterH + kPad;
        const double x = (width.value() - kCardW) * 0.5;
        const double y = (height.value() - h) * 0.5;
        return Rect{x < 4 ? 4 : x, y < 4 ? 4 : y, kCardW, h};
    }

    Rect SettingsDialog::doneRect() const
    {
        const Rect c = cardRect();
        return Rect{c.x + c.w - kPad - kBtnW, c.y + c.h - kPad - kBtnH, kBtnW, kBtnH};
    }

    void SettingsDialog::chipRects(int row, std::vector<Rect> &rects) const
    {
        rects.clear();
        const Rect c = cardRect();
        const int n = row == 0 ? (int)kEdges.size() : (int)kThreads.size();
        double x = c.x + kPad;
        const double y = blockTop(c, row) + kLabelH + 6.0;
        for (int i = 0; i < n; ++i)
        {
            const std::string lbl = row == 0 ? edgeLabel(i) : threadLabel(kThreads[i]);
            const double w = estimateTextWidth(lbl, kFontPx) + 2 * kChipPadX;
            rects.push_back(Rect{x, y, w, kChipH});
            x += w + kChipGap;
        }
    }

    bool SettingsDialog::handleGesture(const Gesture &g, const Point &local)
    {
        if (!mOpen || mClosing) return false;
        if (g.type == Gesture::Type::Move)
        {
            int row, chip; bool done; hitTargets(local, row, chip, done);
            mHover.setHovered(done ? doneId() : (chip >= 0 ? chipId(row, chip) : -1));
            return true;
        }
        if (g.type != Gesture::Type::Click) return true;  // modal: consume everything else

        const Point p = local;
        if (!cardRect().contains(p)) { beginClose(); return true; }  // click outside closes
        if (doneRect().contains(p)) { beginClose(); return true; }

        std::vector<Rect> chips;
        chipRects(0, chips);
        for (int i = 0; i < (int)chips.size(); ++i)
            if (chips[i].contains(p)) { mEdge = kEdges[i]; if (onPreviewEdge) onPreviewEdge(mEdge); return true; }
        chipRects(1, chips);
        for (int i = 0; i < (int)chips.size(); ++i)
            if (chips[i].contains(p)) { mThreadCount = kThreads[i]; if (onThreads) onThreads(mThreadCount); return true; }
        return true;  // inside card, no control
    }

    void SettingsDialog::onOverlay(IRenderTarget &t) const
    {
        const double a = mAppear.value();
        if (!mOpen || a <= 0.001) return;

        drawRoundedRect(t, Rect{0, 0, width.value(), height.value()}, 0.0, Paint::filled(fade(Color{0, 0, 0, 0.55}, a)));

        const Rect c = cardRect();
        drawRoundedRect(t, c, radius::control(),
                        Paint::filledStroked(fade(palette::popover(), a), fade(palette::border(), a), 1.0));

        t.setFill(fade(palette::foreground(), a));
        t.drawText("Settings", c.x + kPad, c.y + kPad + 16.0, 14.0, font::sansSemiBold());

        const char *rowLabels[2] = {"Preview quality", "CPU threads"};
        const int rowSel[2] = {mEdge, mThreadCount};
        for (int row = 0; row < 2; ++row)
        {
            const double ly = blockTop(c, row);
            t.setFill(fade(palette::mutedForeground(), a));
            t.drawText(rowLabels[row], c.x + kPad, ly + 12.0, 11.0, font::sansMedium(), 0.06 * 11.0);

            std::vector<Rect> chips;
            chipRects(row, chips);
            const int n = row == 0 ? (int)kEdges.size() : (int)kThreads.size();
            for (int i = 0; i < n; ++i)
            {
                const int val = row == 0 ? kEdges[i] : kThreads[i];
                const bool sel = val == rowSel[row];
                const std::string lbl = row == 0 ? edgeLabel(i) : threadLabel(kThreads[i]);
                drawRoundedRect(t, chips[i], radius::control(),
                                sel ? Paint::filled(fade(mAccent, a))
                                    : Paint::filledStroked(fade(palette::secondary(), a), fade(palette::border(), a), 1.0));
                const double chv = mHover.amount(chipId(row, i)) * a;
                if (chv > 0.001)  // eased hover wash on the chip under the pointer (cross-fades)
                    drawRoundedRect(t, chips[i], radius::control(), Paint::filled(palette::hoverWash(chv)));
                t.setFill(fade(sel ? palette::primaryForeground() : palette::foreground(), a));
                const double tw = estimateTextWidth(lbl, kFontPx);
                t.drawText(lbl, chips[i].x + (chips[i].w - tw) * 0.5, chips[i].y + chips[i].h * 0.5 + 4.0, kFontPx,
                           sel ? font::sansMedium() : font::sans());
            }
        }

        // footer: Done (primary)
        const Rect d = doneRect();
        drawRoundedRect(t, d, radius::control(), Paint::filled(fade(mAccent, a)));
        const double dhv = mHover.amount(doneId()) * a;
        if (dhv > 0.001)
            drawRoundedRect(t, d, radius::control(), Paint::filled(palette::hoverWash(dhv)));
        t.setFill(fade(palette::primaryForeground(), a));
        const double tw = estimateTextWidth("Done", kFontPx);
        t.drawText("Done", d.x + (d.w - tw) * 0.5, d.y + d.h * 0.5 + 4.0, kFontPx, font::sansMedium());
    }
}
}
