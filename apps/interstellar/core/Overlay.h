/*
 *  interstellar_core — text over a frame: burn-ins (R-DLV-2) and burned-in captions (R-DLV-1).
 *
 *  The core NAMES the text and where it goes; the host DRAWS it (`Host::drawText`), with the app's own
 *  typeface (the app library's CairoTarget and the fonts compiled into the binary), so a render's
 *  burn-in reads like the rest of the product and the core carries no font rasteriser (law 8).
 */
#pragma once
#include <string>

namespace arstro
{
namespace interstellar
{
    struct OverlayText
    {
        std::string text;            // UTF-8, one line
        double x = 0, y = 0;         // the anchor, frame pixels
        int align = 0;               // 0 left of x · 1 centred on x · 2 right of x
        int valign = 0;              // 0 below y (y = the box's top) · 1 above y (y = its bottom)
        double px = 24.0;            // the text's size
        bool box = true;             // a translucent black plate behind it, for any picture
        bool mono = true;            // the numerals' face (timecode); false = the sans (a caption)
        double white = 1.0;          // the text's code value: graphics white, not the peak, in an HDR
                                     // render (BT.2408: 203 cd/m² = 0.58 PQ, 0.75 HLG)
    };
}
}
