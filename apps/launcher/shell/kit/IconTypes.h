/*
 *  arstro kit — icon path-table types.
 *
 *  An icon is a flattened vector path in a square coordinate system of side `viewSize` (24).
 *  kit/icons-svg-to-header.py turns each SVG in kit/icons-src into a static table of these ops at
 *  build time; arcs and quadratics become cubics because the render HAL has only cubics. One
 *  weight, one grid, stroked — so every icon in the shell and in every app is the same family.
 */
#pragma once
namespace arstro
{
namespace kit
{
namespace icons
{
    struct IconOp
    {
        enum Kind { Move, Line, Cubic, Close } kind;
        float v[6];  // Move/Line: x,y · Cubic: c1x,c1y,c2x,c2y,x,y
    };
    struct IconPath
    {
        const IconOp *ops;
        int count;
        float viewSize;
        bool stroke;  // true = line icon, false = solid
    };
}
}
}
