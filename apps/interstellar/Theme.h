/*
 *  interstellar — Theme: ALIASES cosmo's token namespaces. It does not copy them.
 *
 *  `arstro.design.rule` §2 states the precedent as a scar: genesis forked cosmo's palette and it
 *  has already drifted, while `arstrobench` aliases the namespaces and compiles `cosmo/Theme.cpp`
 *  and has cost nothing. Interstellar is the second app to take the cheap path. So there is one
 *  accent, one radius ladder and one type ramp in the suite, and a change to cosmo's tokens
 *  reaches this app by rebuilding rather than by remembering (R-G-2).
 *
 *  Only Interstellar's OWN surface literals are declared here — the ones cosmo has no equivalent
 *  for because it has no time axis. Every one of them is a named rung, because a bare number in
 *  a widget is the bug.
 */
#pragma once
#include "../../core/Artboard/include/artboard/artboard.h"
#include "../cosmo/Theme.h"

namespace arstro
{
namespace interstellar_v1
{
    // One vocabulary, aliased (never redeclared).
    namespace palette = ::arstro::cosmo_v2::palette;
    namespace radius = ::arstro::cosmo_v2::radius;
    namespace font = ::arstro::cosmo_v2::font;
    namespace metrics = ::arstro::cosmo_v2::metrics;
    inline const artboard::Theme &sharedTheme() { return ::arstro::cosmo_v2::sharedTheme(); }

    /** Interstellar's own per-surface literals. A time axis has surfaces cosmo does not. */
    namespace surface
    {
        /** The monitor's surround: the same near-black as cosmo's photo stage, deliberately, so
         *  a frame looks the same in both apps. */
        inline artboard::Color monitorBg() { return palette::canvasBg(); }
        inline artboard::Color deckBg() { return artboard::Color::hex(0x121212); }      // == filmstripBg
        inline artboard::Color rulerBg() { return artboard::Color::hex(0x161616); }     // == leftRailBg
        inline artboard::Color trackBg() { return artboard::Color::hex(0x171717); }
        inline artboard::Color trackBgAlt() { return artboard::Color::hex(0x1A1A1A); }  // banded lanes
        inline artboard::Color clipFill() { return artboard::Color::hex(0x2A3550); }    // a graded blue-grey
        inline artboard::Color clipFillSel() { return palette::primaryAlpha(0.42); }
        inline artboard::Color laneFill() { return artboard::Color::hex(0x14181F); }
        /** A curve on an automation lane, and the playhead. One accent, so both are `primary`. */
        inline artboard::Color curve() { return palette::primary(); }
        inline artboard::Color playhead() { return artboard::Color::hex(0xE5534B); }    // == destructive
    }

    /** Metrics for the time axis. On cosmo's 3.25 px ladder where the value is chrome-sized, and
     *  on a coarser rhythm where a finger-or-pointer target needs one (the deck rows). */
    namespace time
    {
        inline double rulerHeight() { return 22.75; }   // 7 rungs — cosmo's Breadcrumb height
        inline double trackHeight() { return 48.0; }
        inline double laneHeight() { return 34.0; }
        inline double laneHeightOpen() { return 96.0; }
        inline double clipMinWidth() { return 6.0; }    // below this a clip is a tick, still hittable
        /** The left header column — track names in Cut, lane addresses in Mix. ONE constant,
         *  read by both deck views AND by both of their time origins, because two copies of it
         *  drift and the symptom is a lane three pixels out of step with the cut above it
         *  (gotcha 15). Found by looking: with the lanes' axis starting at 0 instead, every link
         *  in the first 190 px sat underneath the label gutter and was culled — the automation
         *  was there and invisible. */
        inline double headerWidth() { return 190.0; }
        inline double deckHeight() { return 220.0; }
        inline double transportHeight() { return 39.0; }  // cosmo's ActionBar height
    }
}
}
