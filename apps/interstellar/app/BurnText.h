/*
 *  interstellar_v1 — BurnText: text over a rendered frame, in the app's typeface (R-DLV-1, R-DLV-2).
 *
 *  The host's `drawText` (core/Overlay.h): each item is drawn with Artboard's CairoTarget and the
 *  fonts compiled into the binary — JetBrains Mono for numerals, Roboto for words — on a translucent
 *  plate, into a small Cairo surface, then laid over the frame (8- or 16-bit) by its alpha. The CLI
 *  and the window both install it, so a burn-in is the same in a script's render and a click's.
 */
#pragma once
#include "../core/Raster.h"
#include "../core/Overlay.h"
#include <vector>

namespace arstro
{
namespace interstellar_v1
{
    bool drawOverlayText(interstellar::Raster &frame, const std::vector<interstellar::OverlayText> &items);
}
}
