/*
 *  interstellar_render — mattes: how much of a node's grade reaches each pixel (R-CLR-1, R-CLR-2).
 *
 *  A qualifier (an HSL key) and a window (a circle or a rectangle, feathered) are plugins in a node's
 *  stack (`qualifier.hsl`, `window.shape`), but they process no pixels: each multiplies a KEY, 0..1 per
 *  pixel, computed from the node's INPUT, and the node's grade is mixed in by it — `a + (b − a) × key`,
 *  the grade weight made spatial. Several mattes on one node intersect (the product); a matte's mix
 *  fades its limit (mix 0 limits nothing). Colour stays Cosmo's: a matte decides where, never what.
 *
 *  A window's `shape` is 0 for a circle (an ellipse in the picture) and 1 for a rectangle (its slider is
 *  labelled Rectangle; a value between reads as the nearer). Geometry is in the node's own picture: x 0..1 across its width, y 0..1 down its height; a window's
 *  size is a share of the width and of the height, its feather a share of the WIDTH, so a feather is
 *  round on any aspect. Hue is the hexcone's (degrees), saturation (max − min) / max, luma Rec.709's —
 *  all on the input's code values, as a colourist's qualifier reads the picture in front of it.
 */
#pragma once
#include "Effects.h"
#include "Raster.h"
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar
{
namespace render
{
    bool isMatte(const std::string &type);
    /** Multiply `key` (width × height, created at 1 when empty) by matte `m` read from `input`. */
    void applyMatte(const EffectRun &m, const Raster &input, std::vector<float> &key);
    /** `b` = a + (b − a) × key, per pixel (alpha kept); the two the same size, 8- or 16-bit alike. */
    void mixByKey(const Raster &a, Raster &b, const std::vector<float> &key);
    /** R-CLR-3: `out` += mix × (branch − base), per pixel, clamped — a parallel node's contribution. */
    void addDifference(const Raster &base, const Raster &branch, double mix, Raster &out);
    /** The key as a grey picture — the matte view (white = the grade reaches it). */
    void keyPicture(const std::vector<float> &key, int width, int height, Raster &out);
}
}
}
