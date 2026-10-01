#include "LoadingView.h"
#include "TextFit.h"
#include <cmath>

namespace arstro
{
namespace interstellar_v1
{
    using namespace artboard;

    namespace
    {
        constexpr double kBarW = 240.0;
        constexpr double kSegFrac = 0.32;
    }

    double LoadingView::sweep() const
    {
        if (reducedMotion()) return 0.5;
        // a PING-PONG over one period: out and back, so the segment never jumps from the right
        // end to the left one when the period wraps
        const double phase = std::fmod(mNowMs, motion::kPulseMs) / motion::kPulseMs;   // 0..1
        const double tri = phase < 0.5 ? phase * 2.0 : 2.0 - phase * 2.0;
        return applyEasing(Easing::EaseInOutCubic, tri);
    }

    void LoadingView::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, 0.0, Paint::filled(palette::background()));

        // the small wordmark, top-left, where the Edit top bar will put it
        const double sp = -0.03 * 13.0;
        const double base = textfit::baseline(shell::topBarH() * 0.5, 13.0);
        t.setFill(palette::foreground());
        t.drawText("interstellar", 9.75, base, 13.0, font::sansSemiBold(), sp);
        t.setFill(palette::primary());
        t.drawText(".", 9.75 + t.measureText("interstellar", 13.0, font::sansSemiBold(), sp), base, 13.0, font::sansSemiBold(), sp);

        const double cy = h * 0.45;
        const std::string name = textfit::ellipsize(t, mName.empty() ? std::string("Opening project") : mName, w - 64.0, 14.0, font::sansSemiBold());
        t.setFill(palette::foreground());
        t.drawText(name, (w - t.measureText(name, 14.0, font::sansSemiBold())) * 0.5, cy, 14.0, font::sansSemiBold());
        const std::string s = "Opening the rack and its versions\xE2\x80\xA6";
        t.setFill(palette::mutedForeground());
        t.drawText(s, (w - t.measureText(s, 11.0, font::sans())) * 0.5, cy + 22.0, 11.0, font::sans());

        // indeterminate: a segment sweeping the track
        const double bx = (w - kBarW) * 0.5, by = cy + 40.0;
        drawRoundedRect(t, Rect{bx, by, kBarW, 3.0}, radius::pill(), Paint::filled(palette::secondary()));
        const double segW = kBarW * kSegFrac;
        const double x = bx + (kBarW - segW) * sweep();
        drawRoundedRect(t, Rect{x, by, segW, 3.0}, radius::pill(), Paint::filled(palette::primary()));
    }
}
}
