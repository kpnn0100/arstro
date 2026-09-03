/*
 *  Arstro ImageProcessing Library
 *
 *  Contour: turn a scalar coverage/likelihood plane into GEOMETRY — closed loops of points
 *  in normalised 0..1 coordinates — and thin those loops without changing their shape.
 *
 *  It lives in `analysis/` and not in `engine/` because it is a statement about a plane of
 *  numbers and knows nothing about masks, parameters or rendering. It was in `MaskStack.cpp`
 *  while the only caller was the render tracing a semantic mask's boundary on every frame;
 *  under R-AISEG-21 the render does not trace anything and the caller is `Detection`, which
 *  runs once and stores what it found.
 *
 *  Geometry, not pixels. That distinction is why this file exists at all: a few hundred
 *  normalised points are the same kind of thing every mask's geometry already is, and can
 *  therefore travel into `EditParams`, into a project file and out to a view — none of which
 *  a coverage plane is allowed to do.
 */
#pragma once
#include "../base/Pixel.h"
#include <utility>
#include <vector>

namespace arstro
{
    /** A closed loop of points in normalised 0..1 coordinates. */
    using ContourLoop = std::vector<std::pair<float, float>>;

    /** Trace the `threshold` contour of a plane into loops (marching squares).
     *
     *  `maxEdge` coarsens the grid that is walked, as a SHORT-EDGE cell count; **0 (the default)
     *  walks every cell**, which is what a trace that happens once and is then stored wants
     *  (R-AISEG-14 as amended — thin the result with `simplifyLoop`, which preserves shape,
     *  rather than by sampling less of it, which does not). `minLoopPoints` drops specks.
     *
     *  **`closeAtBorder` treats everything outside the plane as BELOW the threshold**, so a
     *  region running off the edge of the frame comes back as a loop closed along that edge
     *  instead of as an open chain. It is the default, and it has to be, because an open chain
     *  is not a shape: its enclosed area is ~0, so a caller measuring loops discards it, and a
     *  caller filling loops fills nothing. A portrait cropped at the shoulders is the ordinary
     *  case, not the corner case — and with the border open, that portrait's mask came back
     *  empty. Pass false for the older behaviour, in which the frame edge is not a boundary and
     *  an all-covered plane therefore has no boundary at all. */
    std::vector<ContourLoop>
    traceCoverageOutline(const std::vector<Pixel> &cov, int w, int h, float threshold = 0.5f,
                         int maxEdge = 0, int minLoopPoints = 6, bool closeAtBorder = true);

    /** Douglas-Peucker: drop the points of `loop` that lie within `tolerance` of the chord their
     *  neighbours already describe, in the same normalised units the loop is in.
     *
     *  This and not a coarser trace is how a stored contour is made small (R-AISEG-14 as
     *  amended). The two are not interchangeable: a coarse grid spends its budget uniformly, so
     *  it blunts a jawline and a knuckle to buy detail on a straight run of forehead that needed
     *  two points; this spends every point where the boundary actually turns. On a real skin
     *  contour it removes ~85% of the points and moves none of the rest.
     *
     *  Closed-loop aware: the first point is pinned and the loop is never left with fewer than
     *  three points, because fewer than three has no area and would silently become no mask. */
    ContourLoop simplifyLoop(const ContourLoop &loop, float tolerance);

    /** The area a loop encloses, by the shoelace formula, in normalised units — so 1.0 is the
     *  whole frame. Unsigned: winding is not tracked (see the marching-squares comment), and
     *  every caller here wants "how big is this region". */
    float loopArea(const ContourLoop &loop);
}
