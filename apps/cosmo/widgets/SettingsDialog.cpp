#include "SettingsDialog.h"
#include "../core/AppSettings.h"   // the ONE list of legal scales (R-SCALE-1)
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
    // R-CPU-3. 50 is the default and the reason the row exists; 100 is offered so a
    // machine that is doing nothing else can still be given fully to a big import.
    const std::vector<int> SettingsDialog::kCpuPercents{25, 50, 75, 100};
    // Bound to the core list rather than copied: the scales that are persisted and snapped to
    // are the scales that may be offered, and two lists would drift into a chip that loads back
    // as something else (R-SCALE-1).
    const std::vector<int> &SettingsDialog::kScales = cosmo::AppSettings::uiScales();

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
        constexpr double kChipRowGap = 5.0;   // between wrapped lines of chips within one row
        constexpr double kBlockH = kLabelH + 6.0 + kChipH;  // label + gap + ONE line of chips

        inline Color fade(Color c, double a) { return Color{c.r, c.g, c.b, c.a * a}; }

        std::string edgeLabel(int i) { return i == 0 ? "Draft" : i == 1 ? "Standard" : "High"; }
        std::string scaleLabel(int v) { return std::to_string(v) + "%"; }
        std::string threadLabel(int v) { return v == 0 ? "Auto" : std::to_string(v); }
        std::string cpuLabel(int v) { return std::to_string(v) + "%"; }
        std::string gpuLabel(int i) { return i == 0 ? "Off" : "On"; }

    }

    // One label mapping, read by both chipRects (which sizes the chip to its text) and
    // onOverlay (which draws it) -- two copies would drift and mis-place every chip
    // after the first in a row.
    std::string SettingsDialog::chipLabel(int row, int i)
    {
        return row == kRowScale   ? scaleLabel(kScales[i])
             : row == kRowQuality ? edgeLabel(i)
             : row == kRowThreads ? threadLabel(kThreads[i])
             : row == kRowCpu     ? cpuLabel(kCpuPercents[i])
                                  : gpuLabel(i);
    }

    SettingsDialog::SettingsDialog(const Color &accent) : mAccent(accent) {}

    void SettingsDialog::show(int uiScale, int maxScale, int previewEdge, int threads,
                              int cpuPercent, bool useGpu, bool gpuAvailable)
    {
        mUiScale = cosmo::AppSettings::clampUiScale(uiScale);
        mMaxScale = maxScale;
        mEdge = previewEdge; mThreadCount = threads; mCpuPercent = cpuPercent;
        mGpuAvailable = gpuAvailable; mUseGpu = useGpu && gpuAvailable;
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
        for (int r = 0; r < kRows; ++r)
        {
            std::vector<Rect> chips; chipRects(r, chips);
            for (int i = 0; i < (int)chips.size(); ++i)
                if (chips[i].contains(p)) { row = r; chip = i; return; }
        }
    }

    Rect SettingsDialog::cardRect() const
    {
        // Summed from the rows rather than kRows * kBlockH: a wrapped row is taller, and a card
        // sized for the unwrapped height would clip its own Done button.
        double rows = 0.0;
        for (int r = 0; r < kRows; ++r) rows += rowBlockH(r) + kRowGap;
        const double h = kPad + kHeaderH + rows + kFooterH + kPad;
        const double x = (width.value() - kCardW) * 0.5;
        const double y = (height.value() - h) * 0.5;
        return Rect{x < 4 ? 4 : x, y < 4 ? 4 : y, kCardW, h};
    }

    Rect SettingsDialog::doneRect() const
    {
        const Rect c = cardRect();
        return Rect{c.x + c.w - kPad - kBtnW, c.y + c.h - kPad - kBtnH, kBtnW, kBtnH};
    }

    // ── chips WRAP, and the row grows to hold them (R5/R6) ──────────────────────────────
    // Chips used to be laid out on one unbounded line: fine for four, and silently off the
    // card's right edge for seven, which is what Screen scale became when the range reached
    // 200%. Widening the card for one row, or thinning the padding, would trade one row's
    // problem for the whole dialog's; wrapping fixes every future row at once and costs the
    // card only the height it actually needs.
    //
    // ONE walk, two callers, and the split matters: the first version had `rowLines` ask
    // `chipRects`, which asked `cardRect`, which summed `rowBlockH`, which asked `rowLines` —
    // mutual recursion between "how big is the card" and "where do the chips go", which
    // segfaulted on a 74000-frame stack the moment the dialog was drawn. The wrap decision
    // depends only on the card's WIDTH, which is a constant; only the chips' absolute
    // positions need to know where the card sits. So `layoutChips` does the single walk, and a
    // caller that only wants the line count passes no origin and no output vector.
    int SettingsDialog::layoutChips(int row, double left, double top, std::vector<Rect> *out) const
    {
        if (out) out->clear();
        const double inner = kCardW - 2 * kPad;
        const int n = rowChipCount(row);
        double x = 0.0, y = 0.0;
        int lines = 1;
        for (int i = 0; i < n; ++i)
        {
            const double w = estimateTextWidth(chipLabel(row, i), kFontPx) + 2 * kChipPadX;
            if (i > 0 && x + w > inner)   // never the first chip of a line: a chip wider than
            {                             // the card must overflow visibly, not vanish
                x = 0.0;
                y += kChipH + kChipRowGap;
                ++lines;
            }
            if (out) out->push_back(Rect{left + x, top + y, w, kChipH});
            x += w + kChipGap;
        }
        return lines;
    }

    void SettingsDialog::chipRects(int row, std::vector<Rect> &rects, int *lines) const
    {
        const Rect c = cardRect();
        const int used = layoutChips(row, c.x + kPad, blockTop(c, row) + kLabelH + 6.0, &rects);
        if (lines) *lines = used;
    }

    int SettingsDialog::rowLines(int row) const { return layoutChips(row, 0.0, 0.0, nullptr); }

    double SettingsDialog::rowBlockH(int row) const
    {
        const int lines = rowLines(row);
        return kLabelH + 6.0 + lines * kChipH + (lines - 1) * kChipRowGap;
    }

    double SettingsDialog::blockTop(const Rect &c, int row) const
    {
        double y = c.y + kPad + kHeaderH;
        for (int r = 0; r < row; ++r) y += rowBlockH(r) + kRowGap;
        return y;
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
        for (int row = 0; row < kRows; ++row)
        {
            chipRects(row, chips);
            for (int i = 0; i < (int)chips.size(); ++i)
                if (chips[i].contains(p))
                {
                    if (row == kRowScale)
                    {
                        // A scale this display cannot give a window for is inert, like the GPU
                        // "On" chip with no backend — clicking it must not set a scale the
                        // shell would then have to crop itself into (R-SCALE-3).
                        if (kScales[i] > mMaxScale) return true;
                        mUiScale = kScales[i];
                        if (onUiScale) onUiScale(mUiScale);
                    }
                    else if (row == kRowQuality) { mEdge = kEdges[i]; if (onPreviewEdge) onPreviewEdge(mEdge); }
                    else if (row == kRowThreads) { mThreadCount = kThreads[i]; if (onThreads) onThreads(mThreadCount); }
                    else if (row == kRowCpu) { mCpuPercent = kCpuPercents[i]; if (onCpuPercent) onCpuPercent(mCpuPercent); }
                    else  // GPU row: chip 0 = Off, chip 1 = On (On is inert with no backend)
                    {
                        if (i == 1 && !mGpuAvailable) return true;
                        mUseGpu = (i == 1); if (onUseGpu) onUseGpu(mUseGpu);
                    }
                    return true;
                }
        }
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

        const char *rowLabels[kRows] = {"Screen scale", "Preview quality", "CPU threads",
                                        "CPU limit", "GPU acceleration"};
        for (int row = 0; row < kRows; ++row)
        {
            const double ly = blockTop(c, row);
            t.setFill(fade(palette::mutedForeground(), a));
            std::string rowLbl = rowLabels[row];
            // Middot suffixes. "Auto uses this" is why the two CPU rows sit together:
            // without it the Auto chip above reads as if it still meant every core (R-CPU-2).
            // Middot suffixes name the consequence, because neither row's effect is where the
            // user is looking: the scale changes the WINDOW's minimum, not just the type size.
            if (row == kRowScale)
            {
                // Name the limit rather than leaving a dimmed chip unexplained. "smaller fits
                // more" is the whole point of the row on a cramped screen; the cap is only
                // mentioned when there IS one, so a big display is not told about a problem it
                // does not have.
                rowLbl += "  \xc2\xb7  smaller fits more";
                if (!kScales.empty() && mMaxScale < kScales.back())
                    rowLbl += ", up to " + std::to_string(mMaxScale) + "% here";
            }
            if (row == kRowCpu) rowLbl += "  \xc2\xb7  Auto uses this";
            if (row == kRowGpu && !mGpuAvailable) rowLbl += "  \xc2\xb7  unavailable";
            t.drawText(rowLbl, c.x + kPad, ly + 12.0, 11.0, font::sansMedium(), 0.06 * 11.0);

            std::vector<Rect> chips;
            chipRects(row, chips);
            const int n = rowChipCount(row);
            for (int i = 0; i < n; ++i)
            {
                const bool sel = row == kRowScale   ? (kScales[i] == mUiScale)
                               : row == kRowQuality ? (kEdges[i] == mEdge)
                               : row == kRowThreads ? (kThreads[i] == mThreadCount)
                               : row == kRowCpu     ? (kCpuPercents[i] == mCpuPercent)
                                                    : ((i == 1) == mUseGpu);
                const bool disabled = (row == kRowGpu && i == 1 && !mGpuAvailable)     // "On", no backend
                                   || (row == kRowScale && kScales[i] > mMaxScale);   // no room (R-SCALE-3)
                const double da = disabled ? 0.4 : 1.0;
                const std::string lbl = chipLabel(row, i);
                drawRoundedRect(t, chips[i], radius::control(),
                                sel ? Paint::filled(fade(mAccent, a))
                                    : Paint::filledStroked(fade(palette::secondary(), a * da), fade(palette::border(), a * da), 1.0));
                if (!disabled)
                {
                    const double chv = mHover.amount(chipId(row, i)) * a;
                    if (chv > 0.001)  // eased hover wash on the chip under the pointer (cross-fades)
                        drawRoundedRect(t, chips[i], radius::control(), Paint::filled(palette::hoverWash(chv)));
                }
                t.setFill(fade(sel ? palette::primaryForeground() : palette::foreground(), a * da));
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
