/*
 *  Cosmo by arstro — EditControls: the Basic/Detail slider catalogue as DATA (R-NTWB-4).
 *
 *  Which sliders the edit column shows, in which sections, over which track range, writing
 *  which EditParams field, and how the track position converts into the unit the engine
 *  stores. A second front end needs every one of those answers to draw the same column —
 *  the NTWB web UI does, and it must not re-type them (a second hand-kept list drifts the
 *  first time one grows a slider) nor re-implement the conversions (temperature is linear
 *  in MIRED, R-WB-2, which is exactly the kind of formula that goes subtly wrong when
 *  copied into JavaScript).
 *
 *  So the catalogue lives here, host-side and header-only, next to UnitConversions.h whose
 *  functions it points at, and the NTWB adapter publishes it with each conversion SAMPLED
 *  (`controls` in the Cosmo API): the browser interpolates between samples computed by
 *  these very functions and never evaluates a formula of its own.
 *
 *  Values match RightColumn.cpp's sections as of 2026-09-30. RightColumn still keeps its
 *  own copy (it pairs each row with a widget callback); moving it onto this table is ledger
 *  task NTWB-T3 — until then a slider added there must be added here too.
 */
#pragma once
#include "widgets/UnitConversions.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    struct EditControl
    {
        const char *label;
        const char *field;                      // the EditParamsIO key `set` writes
        double min, max;                        // the TRACK's range (the Figma mock's scale)
        double (*toEngine)(double) = nullptr;   // track -> engine unit; nullptr = 1:1
        double (*fromEngine)(double) = nullptr; // engine unit -> track
        uint32_t rampFrom = 0, rampTo = 0;      // 0xRRGGBB colour ramp of the track, 0 = none
    };

    struct EditControlSection
    {
        const char *title;
        std::vector<EditControl> controls;
        bool whiteBalancePicker = false;        // the COLOUR header carries the eyedropper (R-WB-1)
    };

    /** The readout column wide enough for the longest formatted value ("+0.40 EV", 8 mono
     *  glyphs at 10 px ≈ 48 px): 15 units of the 3.25 px ladder. One width for every row, so
     *  every track in the column is the same length (R-UX-8). */
    inline constexpr double kEditReadoutWidth = 48.75;

    /** What a row's readout says for the track position `track` (R-UX-8): the value the ENGINE
     *  gets, in the unit a photographer thinks in — not where the thumb sits. Derived from the
     *  catalogue row itself (its conversion and its range), so there is no second list of
     *  "which control has which unit" to keep in step:
     *    exposure     track/80          "+0.40 EV"   two decimals, signed
     *    temperature  mired-linear      "6200 K"     kelvin, to the nearest 10
     *    radius       track/10          "1.0 px"
     *    unipolar     (min >= 0)        "40"         no sign: there is no negative to tell it from
     *    bipolar                        "+25" / "-25" / "0"   (what every row showed before) */
    inline std::string formatEditControl(const EditControl &c, double track)
    {
        char buf[32];
        if (c.toEngine == toEv)
        {
            const double ev = std::round(toEv(track) * 100.0) / 100.0;   // no "-0.00"
            std::snprintf(buf, sizeof(buf), ev > 0.0 ? "+%.2f EV" : "%.2f EV", ev == 0.0 ? 0.0 : ev);
        }
        else if (c.toEngine == toKelvin)
            std::snprintf(buf, sizeof(buf), "%d K", (int)(std::lround(toKelvin(track) / 10.0) * 10));
        else if (c.toEngine == toRadiusPx)
            std::snprintf(buf, sizeof(buf), "%.1f px", toRadiusPx(track));
        else
        {
            const double v = c.toEngine ? c.toEngine(track) : track;
            const int n = (int)std::lround(v);
            std::snprintf(buf, sizeof(buf), (c.min < 0.0 && n > 0) ? "+%d" : "%d", n);
        }
        return buf;
    }

    inline const std::vector<EditControlSection> &editControls()
    {
        static const std::vector<EditControlSection> s = {
            {"TONE", {
                {"Exposure", "exposure", -400, 400, toEv, fromEv},
                {"Contrast", "contrast", -100, 100},
                {"Highlights", "highlights", -100, 100},
                {"Shadows", "shadows", -100, 100},
                {"Whites", "whites", -100, 100},
                {"Blacks", "blacks", -100, 100},
            }},
            {"COLOUR", {
                {"Temperature", "temp", -100, 100, toKelvin, fromKelvin, 0x4A84E8, 0xF0B254},
                {"Tint", "tint", -100, 100, toTint, fromTint, 0x58C476, 0xCE68C4},
                {"Vibrance", "vibrance", -100, 100},
                {"Saturation", "saturation", -100, 100},
            }, true},
            {"PRESENCE", {
                {"Texture", "texture", -100, 100},
                {"Clarity", "clarity", -100, 100},
                {"Dehaze", "dehaze", 0, 100},
            }},
            {"EFFECTS", {
                {"Grain Amount", "grainAmount", 0, 100},
                {"Grain Size", "grainSize", 0, 100},
            }},
            {"SHARPENING", {
                {"Amount", "sharpenAmount", 0, 150},
                {"Radius", "sharpenRadius", 5, 30, toRadiusPx, fromRadiusPx},
                {"Masking", "sharpenMasking", 0, 100},
            }},
            {"NOISE REDUCTION", {
                {"Luminance", "nrLuminance", 0, 100},
                {"Colour", "nrColor", 0, 100},
            }},
            {"LENS", {
                {"Distortion", "lensDistortion", -100, 100},
                {"Defringe", "lensCA", 0, 100},
                {"Vignette", "lensVignette", -100, 100},
            }},
        };
        return s;
    }
}
}
