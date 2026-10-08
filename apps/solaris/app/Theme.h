/*
 *  solaris_ui — the design tokens. ALIASED from cosmo, one token forked (R-G-2, R-UI-2).
 *
 *  Solaris is "cosmo wearing one different colour", as Interstellar is. Every neutral, radius, font,
 *  the spacing ladder and the shared artboard Theme are cosmo's, reached through namespace ALIASES —
 *  a value changed in `apps/cosmo/Theme.h` reaches this app the next time it builds.
 *
 *  The accent is cosmo's RUNTIME slot (`palette::setAccent`, cosmo R-G-5): `installSolarisAccent()`
 *  moves it to teal #159387 once, before the first widget exists, so every reused cosmo widget and
 *  every Interstellar helper draws teal with nothing forked. Teal, at cosmo blue's LUMINANCE, so
 *  white text on it reads as well as on cosmo's (R-UI-2, amended with the measurement).
 *
 *  What IS declared here is only what cosmo has no word for, because cosmo has no timeline and no
 *  mixer: lanes, the ruler, clips, strips, meters, the playhead. Each is a NAMED token, defined AS a
 *  cosmo surface wherever one already has the right value. Domain adaptations, written down as the
 *  design law requires: the playhead is `destructive` (a POSITION, not a state — Interstellar's
 *  reading too); the meter ramps success → amber → destructive, the universal meter colours,
 *  meaning-only; solo is amber and record red, never the accent (why the accent is teal).
 */
#pragma once
#include "../../cosmo/Theme.h"

namespace arstro
{
namespace solaris_ui
{
    namespace palette = ::arstro::cosmo_v2::palette;
    namespace radius = ::arstro::cosmo_v2::radius;
    namespace font = ::arstro::cosmo_v2::font;
    namespace metrics = ::arstro::cosmo_v2::metrics;
    using ::arstro::cosmo_v2::sharedTheme;

    /** Call ONCE at startup, before the first widget (cosmo's contract for the slot). */
    inline void installSolarisAccent() { ::arstro::cosmo_v2::palette::setAccent(artboard::Color::hex(0x159387)); }

    // ── the spacing ladder: cosmo's 3.25 px unit; Home and modals on the 8/16/32 rhythm ─────
    namespace space
    {
        inline constexpr double u(double n) { return n * 3.25; }
        inline constexpr double padX() { return u(3); }     // 9.75 — the dominant panel padding
        inline constexpr double gap() { return u(2); }      // 6.5
        inline constexpr double rowH() { return u(7); }     // 22.75 — a list row
        inline constexpr double barH() { return u(9); }     // 29.25 — the top bar, deck headers
    }

    // ── motion: cosmo's durations, named once (design rule §2.5) — the same names Interstellar uses
    namespace motion
    {
        inline constexpr double kHoverMs = artboard::interaction::kHoverMs;  // 120
        inline constexpr double kModalOpenMs = 150.0;
        inline constexpr double kModalCloseMs = 120.0;
        inline constexpr double kScrollMs = 180.0;     // eased scroll everywhere; scrim
        inline constexpr double kCrossFadeMs = 200.0;  // page cross-fade
        inline constexpr double kSelectMs = 200.0;     // selection ring / wash slide
        inline constexpr double kSlideMs = 220.0;      // tab / segmented highlight travel
        inline constexpr double kCatchUpMs = 220.0;    // a value catching up to a source it does not control
        inline constexpr double kReflowMs = 260.0;     // grid reflow
        inline constexpr double kScreenFadeMs = 260.0; // shell cross-fade (Home / Project)
        inline constexpr double kToastInMs = 150.0, kToastOutMs = 300.0;
        inline constexpr double kToastHoldMs = 3200.0; // a HOLD, not a tween
        inline constexpr double kMeterFallMs = 300.0;  // a meter's release: fast attack, slow fall
    }

    // ── Solaris-only surfaces: named, and derived from cosmo's where cosmo has the value ─────
    namespace surface
    {
        inline artboard::Color stageBg() { return palette::canvasBg(); }        // behind the lanes
        inline artboard::Color rulerBg() { return palette::histogramBg(); }
        inline artboard::Color headerBg() { return palette::leftRailBg(); }      // lane headers, the browser
        inline artboard::Color laneBg() { return palette::segmentedBg(); }
        inline artboard::Color laneAltBg() { return palette::whiteAlpha(0.012); }
        inline artboard::Color deckBg() { return palette::filmstripBg(); }       // the mixer dock
        inline artboard::Color stripBg() { return palette::card(); }
        inline artboard::Color scrim(double a) { return artboard::Color{0.0, 0.0, 0.0, 0.55 * a}; } // cosmo's modal scrim
        inline artboard::Color playhead() { return palette::destructive(); }
        inline artboard::Color meterLow() { return palette::success(); }
        inline artboard::Color meterMid() { return artboard::Color::hex(0xE3B341); } // amber: hot, not yet clipping
        inline artboard::Color meterHigh() { return palette::destructive(); }
        inline artboard::Color solo() { return artboard::Color::hex(0xE3B341); }      // = meterMid: one amber
        inline artboard::Color skeleton() { return palette::whiteAlpha(0.045); }
        /** A strip's colour index (−1 = by kind) → a desaturated hue for its clips' plates. */
        inline artboard::Color track(int index)
        {
            static const uint32_t hues[10] = {0x5B7FA6, 0x6E9A68, 0xA67B5B, 0x8E6EA6, 0x5B9A95,
                                              0xA65B6E, 0x9A915B, 0x6E86A6, 0x7FA65B, 0xA6705B};
            return artboard::Color::hex(hues[((index % 10) + 10) % 10]);
        }
    }
}
}
