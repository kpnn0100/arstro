#include "LeftRail.h"
#include "TextMetrics.h"
#include "../Theme.h"
#include <algorithm>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    LeftRail::LeftRail()
    {
        clipToBounds = true;
        mTree = std::make_shared<PresetTree>();
        addChild(mTree);
    }

    void LeftRail::layout()
    {
        const double w = width.value(), h = height.value();
        mTree->x.set(0.0);
        mTree->y.set(kHeaderH + 3.25 /*py-1*/);
        mTree->width.set(w);
        mTree->height.set(std::max(0.0, h - kHeaderH - 2 * 3.25));
    }

    void LeftRail::onPaint(IRenderTarget &t) const
    {
        const double w = width.value();
        if (w <= 0.5) return;  // fully collapsed -- nothing to draw

        drawRoundedRect(t, Rect{0, 0, w, height.value()}, 0.0, Paint::filled(palette::leftRailBg()));

        // Header: "PRESETS", 9px SemiBold uppercase, tracking 0.13em, + bottom hairline.
        t.beginPath();
        t.moveTo(0, kHeaderH); t.lineTo(w, kHeaderH);
        t.setStroke(palette::border(), 1.0);
        t.strokePath();

        t.setFill(palette::mutedForeground());
        t.drawText("PRESETS", 9.75 /*px-3*/, kHeaderH * 0.5 + 9.0 * 0.35, 9.0, font::sansSemiBold(), 0.13 * 9.0);
    }
}
}
