/*
 *  arstro-android-shell — Shape (M2.2): the Material 3 corner-radius scale (plan §2.2).
 *
 *  ONE radius scale for the whole shell (consistency lock): a control picks a named step, it
 *  never hand-picks a radius. `radiusFull(h)` is the pill/circle radius for a control of height h.
 */
#pragma once

namespace arstro
{
namespace androidshell
{
namespace shape
{
    constexpr double kRadiusXS = 4.0;
    constexpr double kRadiusSM = 8.0;
    constexpr double kRadiusMD = 12.0;
    constexpr double kRadiusLG = 16.0;
    constexpr double kRadiusXL = 28.0;   // notification / QS / launcher cards (plan §2.2)

    // Full ("pill"/circle) radius for a control of the given height.
    inline double radiusFull(double heightPx) { return heightPx * 0.5; }

} // namespace shape
} // namespace androidshell
} // namespace arstro
