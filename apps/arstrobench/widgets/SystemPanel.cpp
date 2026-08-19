#include "SystemPanel.h"
#include "../Theme.h"

namespace arstro
{
namespace arstrobench
{
    using namespace artboard;

    namespace
    {
        constexpr double kPad = 24.0;
        constexpr double kTitleBaseline = 30.0;
        constexpr double kFirstRow = 72.0;
        constexpr double kRowStep = 44.0;
        constexpr double kLabelX = 24.0;
        constexpr double kValueX = 190.0;
        constexpr double kEnterRise = 14.0;
        constexpr double kEnterMs = motion::kDurationMedium4;

        const char *kLabels[SystemPanel::kRows] = {"Chip", "Memory", "Operating system"};
    }

    SystemPanel::SystemPanel() { opacity.set(0.0); }

    double SystemPanel::rowBaseline(int i) { return kFirstRow + kRowStep * i; }

    void SystemPanel::enter(double finalY, double delayMs, double nowMs)
    {
        y.set(finalY + kEnterRise);
        y.animate(Tween::range(finalY + kEnterRise, finalY, kEnterMs)
                      .withEasing(Easing::EmphasizedDecel).after(delayMs), nowMs);
        opacity.animate(Tween::range(0.0, 1.0, kEnterMs).withEasing(Easing::EaseOutCubic).after(delayMs), nowMs);
    }

    void SystemPanel::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, radius::control(),
                        Paint::filledStroked(palette::card(), palette::border(), 1.0));

        t.setFill(palette::mutedForeground());
        t.drawText("SYSTEM", kPad, kTitleBaseline, 10.0, font::sansMedium(), 0.8);

        const std::string values[kRows] = {mInfo.chipText(), mInfo.ramText(), mInfo.os};
        for (int i = 0; i < kRows; ++i)
        {
            const double baseline = rowBaseline(i);
            if (i > 0)  // hairline between rows, never above the first one
                drawRoundedRect(t, Rect{kPad, baseline - kRowStep + 12.0, w - kPad * 2.0, 1.0},
                                0.0, Paint::filled(palette::border()));
            t.setFill(palette::mutedForeground());
            t.drawText(kLabels[i], kLabelX, baseline, 10.0, font::sans());
            t.setFill(palette::foreground());
            t.drawText(values[i], kValueX, baseline, 11.0, font::mono());
        }
    }
}
}
