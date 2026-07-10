/*
 *  cosmo_v2 by arstro — conversions between the Figma mock's own slider
 *  ranges and the real engine's units, where the two differ. See
 *  RightColumn.cpp's doc comment for the full rationale: most sliders are
 *  the engine's own units 1:1; these few use a friendlier UI-only scale.
 */
#pragma once

namespace arstro
{
namespace cosmo_v2
{
    inline double toEv(double figmaValue) { return figmaValue / 80.0; }        // -400..400 -> -5..+5 EV
    inline double fromEv(double ev) { return ev * 80.0; }
    inline double toKelvin(double figmaValue) { return 6500.0 + figmaValue / 100.0 * 3500.0; }  // -100..100 -> 2000..50000K
    inline double fromKelvin(double kelvin) { return (kelvin - 6500.0) / 3500.0 * 100.0; }
    inline double toTint(double figmaValue) { return figmaValue * 1.5; }       // -100..100 -> -150..150
    inline double fromTint(double tint) { return tint / 1.5; }
    inline double toRadiusPx(double figmaValue) { return figmaValue / 10.0; }  // 5..30 -> 0.5..3px
    inline double fromRadiusPx(double px) { return px * 10.0; }
}
}
