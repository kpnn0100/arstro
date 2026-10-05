/*
 *  interstellar_render — Prescale: shrink a decoded frame BEFORE the grade, for a preview (R-PLAY-2).
 *
 *  The grade engine takes an 8-bit frame, converts every pixel to linear float, then reduces to the
 *  preview edge. For a 4K source graded at 640 px, that conversion of 8.3 million pixels is most of
 *  the frame's cost (82 ms against 27 ms for a 1080p source — measured, interstellar_play_bench).
 *  So a preview frame is first box-reduced by an INTEGER factor that keeps at least twice the
 *  preview edge — the engine still does its own final filtering — and the averaging happens in
 *  LINEAR light through two lookup tables, so it is gamma-correct, not the darkening an 8-bit
 *  average of encoded values would be.
 *
 *  Only a preview (longEdge > 0) is prescaled; a render at full size never is, so an export and the
 *  Cosmo still identity (R-RENDER-5) are untouched.
 */
#pragma once
#include "Raster.h"

namespace arstro
{
namespace interstellar
{
namespace render
{
    /** The integer factor `prescale` would use for a `w`×`h` frame at `longEdge` (1 = none). */
    int prescaleFactor(int w, int h, int longEdge);
    /** Box-reduce `in` by prescaleFactor in linear light into `out`; false (and `out` untouched)
     *  when the factor is 1. */
    bool prescale(const Raster &in, int longEdge, Raster &out);
}
}
}
