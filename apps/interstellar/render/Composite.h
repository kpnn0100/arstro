/*
 *  interstellar_render — Composite: per-layer geometry and the bottom-to-top composite.
 *
 *      per layer   geom.crop -> fit -> scale -> rotate about the anchor -> translate
 *      composite   bottom track to top, per-layer blend mode and opacity
 *      output      one straight-alpha RGBA8 Raster at the project size — or RGBA16 when any
 *                  layer is deep (R-COLOR-1), the 8-bit layers widened beside it
 *
 *  Layers arrive already GRADED, so a composite is a composite of graded frames and never of raw
 *  ones. That ordering is why a clip's `geom.crop` (a reframe of the graded result, here) and a
 *  source's `xform.crop` (its framing, inside the grade) are different addresses (R-FX-4).
 *
 *  ── Why it is written the way it is ──
 *
 *  The first version inverse-mapped EVERY output pixel for EVERY layer and called cos/sin-derived
 *  arithmetic per pixel, so a 200-pixel picture-in-picture cost as much as a full-frame layer. This
 *  one builds the inverse affine once per layer, scans only the layer's destination bounding box,
 *  and solves each row's covered span analytically, so the inner loop has no coverage test at all —
 *  raw pointers, row-hoisted, `par::parallelFor` over row bands, one template instantiation per
 *  (blend, mode) pair so there is no per-pixel switch and no per-pixel virtual call.
 *
 *  Sampling is BILINEAR (8-bit fixed-point weights), clamped to the crop rectangle so a crop never
 *  bleeds the pixels it cut away. A layer that is an exact integer-offset, unit-scale, unrotated
 *  placement takes a copy path instead, which is both faster and exactly the source bytes. Coverage
 *  edges are hard (a pixel is in when its centre is in); there is no edge anti-aliasing, and a
 *  downscale beyond 2x aliases because there is no area filter. Both are stated, not implied.
 *
 *  Colour is composited over BLACK (the cleared raster) and alpha tracks coverage, which is what a
 *  video frame needs. Blend modes follow the W3C separable-blend rule — the mode only applies where
 *  there is a backdrop — so a Multiply title over an empty letterbox shows the title rather than
 *  multiplying it into black.
 *
 *  Straight (non-premultiplied) RGBA8 throughout, matching `Raster`, so a frame goes from the grade
 *  to here to a writer with no conversion.
 */
#pragma once
#include "Raster.h"
#include <vector>

namespace arstro
{
namespace interstellar
{
namespace render
{
    /** Per-clip geometry (R-FX-3). Units are the ones a user edits in. */
    struct Geom
    {
        double x = 0, y = 0;                // anchor offset from the raster centre, output pixels
        double scale = 1.0;                 // multiplies the fit scale (both axes)
        double rotation = 0;                // degrees; positive is clockwise on screen (y down)
        double anchorX = 0.5, anchorY = 0.5; // normalised on the DRAWN rectangle: 0.5,0.5 = centre
        double cropX = 0, cropY = 0, cropW = 1, cropH = 1; // normalised on the source frame
    };

    enum class Blend { Normal, Multiply, Screen, Overlay, Add, Subtract, Difference };

    /** How the cropped source meets the output raster. Explicit, because a user must be able to
     *  see which rule applied: Contain = whole picture visible, Cover = raster filled, Stretch =
     *  both axes fitted independently, None = one source pixel per output pixel. `Geom::scale`
     *  multiplies the result for every mode, Stretch included. */
    enum class Fit { Contain, Cover, Stretch, None };

    struct Layer
    {
        const Raster *src = nullptr;   // an already-graded frame; null or empty draws nothing
        Geom geom;
        Fit fit = Fit::Contain;
        double opacity = 1.0;          // 0..1; a transition weight is multiplied in by the caller
        Blend blend = Blend::Normal;
        /** True when this layer is the INCOMING half of a transition whose OUTGOING half is the
         *  layer immediately below it in the list. The two are then mixed against the SAME base —
         *  base + (A - base)*wA + (B - base)*wB — instead of B being laid over A.
         *
         *  Without it a dissolve cannot be right: A at opacity 1-f laid over black, then B at f laid
         *  over that, gives A(1-f)^2 + Bf, which is 75 % brightness at the midpoint — the picture
         *  dips through every cut, the very failure R-TL-4 exists to forbid. `activeAt` sets the
         *  matching flag on `Active`, so the caller copies it across. */
        bool dissolveWithPrevious = false;
    };

    /** One channel through a blend mode, both operands 0..1. The reference the 8-bit kernels are
     *  tested against, and the only place the maths is written in floating point. */
    double blendChannel(Blend mode, double base, double over);

    /** Place one layer into `out` (already allocated), honouring crop, fit, scale, rotation,
     *  anchor, translation, opacity and blend. */
    void placeLayer(const Layer &l, Raster &out);

    /** Bottom to top. `out` is allocated to `width` x `height` and cleared to transparent black —
     *  deep (16-bit) when any layer is, with every layer placed on the same pixels either way. */
    void compose(const std::vector<Layer> &bottomToTop, int width, int height, Raster &out);
}
}
}
