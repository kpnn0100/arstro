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
    // ── Colour temperature: the slider is linear in MIRED, not in kelvin (R-WB-2) ────────
    //
    // The old mapping was `6500 + v/100 * 3500`, i.e. **3000..10000 K** — while its own comment
    // claimed 2000..50000 K, which nothing implemented. Two things were wrong with it:
    //
    //  * **Too narrow to be useful.** A photo shot in shade needs ~12000 K to correct; the slider
    //    stopped at 10000, so the white-balance picker solved the right answer and then had it
    //    clamped away, correcting about half the cast (D-54). Deep shade and tungsten are the two
    //    ends people actually reach for and neither was reachable.
    //  * **Linear in kelvin, which is the wrong axis.** 3000 K -> 4000 K is a large visible shift;
    //    9000 K -> 10000 K is almost nothing. A slider linear in kelvin therefore does nearly all
    //    its work in the first third of its travel. **Mired** (1e6/K) is the unit that makes equal
    //    slider distances equal visible steps, and it is what colour-temperature scales have used
    //    since long before software.
    //
    // The range is 2000..19500 K, and the top is not a taste: `kelvinToRgbGain` clamps its own
    // `w` to 2.0, so 6500·3 = 19500 K is the warmest gain the engine can express and anything
    // past it would be a dead length of slider.
    inline double kelvinNeutral() { return 6500.0; }
    inline double kelvinMinK() { return 2000.0; }
    inline double kelvinMaxK() { return 19500.0; }
    /** -100..100 -> 2000..19500 K, linear in mired on each side of neutral so 0 stays 6500 K. */
    inline double toKelvin(double figmaValue)
    {
        const double mNeutral = 1e6 / kelvinNeutral();
        const double mWarm = 1e6 / kelvinMinK();     // 500 mired at 2000 K
        const double mCool = 1e6 / kelvinMaxK();     // ~51 mired at 19500 K
        const double v = figmaValue < -100.0 ? -100.0 : (figmaValue > 100.0 ? 100.0 : figmaValue);
        // Negative = warmer (more mired), positive = cooler (fewer), matching the track's
        // blue-to-amber ramp being drawn left-to-right cool-to-warm.
        const double mired = v <= 0.0 ? mNeutral + (-v / 100.0) * (mWarm - mNeutral)
                                      : mNeutral - (v / 100.0) * (mNeutral - mCool);
        return 1e6 / mired;
    }
    inline double fromKelvin(double kelvin)
    {
        const double k = kelvin < kelvinMinK() ? kelvinMinK()
                                              : (kelvin > kelvinMaxK() ? kelvinMaxK() : kelvin);
        const double mNeutral = 1e6 / kelvinNeutral();
        const double mWarm = 1e6 / kelvinMinK();
        const double mCool = 1e6 / kelvinMaxK();
        const double mired = 1e6 / k;
        return mired >= mNeutral ? -100.0 * (mired - mNeutral) / (mWarm - mNeutral)
                                 :  100.0 * (mNeutral - mired) / (mNeutral - mCool);
    }
    inline double toTint(double figmaValue) { return figmaValue * 1.5; }       // -100..100 -> -150..150
    inline double fromTint(double tint) { return tint / 1.5; }
    inline double toRadiusPx(double figmaValue) { return figmaValue / 10.0; }  // 5..30 -> 0.5..3px
    inline double fromRadiusPx(double px) { return px * 10.0; }
}
}
