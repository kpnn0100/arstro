/*
 *  arstro-android-shell — icon path-table types (M2.3).
 *
 *  An icon is a flattened vector path: a list of Move/Line/Cubic/Close ops in a square
 *  coordinate system of side `viewSize` (typically 24). The build-time codegen
 *  (assets/icons-svg-to-header.py) turns each SVG into a static `IconPath` of these ops —
 *  arcs and quadratics flattened to cubics, since Artboard's HAL has only cubics. `IconDrawable`
 *  replays the table into a target rect. This header is hand-written and committed; the per-icon
 *  tables (GeneratedIcons.h) are generated into the build dir.
 */
#pragma once

namespace arstro
{
namespace androidshell
{
namespace icons
{
    struct IconOp
    {
        enum Kind { Move, Line, Cubic, Close } kind;
        // Move/Line use v[0..1] = x,y; Cubic uses v[0..5] = c1x,c1y,c2x,c2y,x,y; Close: unused.
        float v[6];
    };

    struct IconPath
    {
        const IconOp *ops;
        int count;
        float viewSize;  // coordinate-system extent (icon is viewSize x viewSize)
        bool stroke;     // true = stroke the path (line icon), false = fill it (solid icon)
    };

} // namespace icons
} // namespace androidshell
} // namespace arstro
