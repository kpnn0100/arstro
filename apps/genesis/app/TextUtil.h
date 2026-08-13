/*
 *  Genesis — text measurement and fitting (design rule R5: text never overflows).
 *
 *  Artboard's HAL exposes `measureText` (the read side of `drawText`), and the Cairo
 *  adapter answers it with real font metrics. A widget only holds a target inside
 *  onPaint, but layout runs before that — so the host publishes the live target once per
 *  frame and every widget measures against the SAME metrics it will draw with. Without a
 *  target (headless tests, first frame) this falls back to Artboard's own size-based
 *  estimate, so a measurement is never simply wrong.
 */
#pragma once
#include <artboard/artboard.h>
#include <string>

namespace genesis
{
namespace ui
{
    /** Publish the target used for measurement this frame (the host calls this). */
    void setMeasureTarget(artboard::IRenderTarget *t);

    /** Advance width in px, using the live target's metrics when there is one. */
    double textWidth(const std::string &text, double sizePx,
                     const char *family = nullptr, double tracking = 0.0);

    /** `text` if it fits in `maxPx`, else the longest prefix plus "…". Empty when even
     *  the ellipsis does not fit, so nothing ever spills past its box. */
    std::string ellipsize(const std::string &text, double maxPx, double sizePx,
                          const char *family = nullptr, double tracking = 0.0);
}
}
