/*
 *  interstellar_v1 — the design tokens. ALIASED from cosmo, forked nowhere (R-G-2, R-UI-2).
 *
 *  Interstellar is "cosmo wearing one different colour", and that sentence is the whole of this
 *  file. Every neutral colour, every radius, every font family, the spacing ladder and the shared
 *  artboard Theme are cosmo's — reached through namespace ALIASES, so a value changed in
 *  `apps/cosmo/Theme.h` reaches this app the next time it builds, and there is no second copy to
 *  drift (the design rule's "a forked token file is a divergence with a delay fuse").
 *
 *  The one divergence is the accent, and it is not a forked token either: cosmo's accent is a
 *  RUNTIME slot (`palette::setAccent`, cosmo R-G-5), so `installInterstellarAccent()` moves it to
 *  #CF5AED once at startup and every reused cosmo widget — slider fills, tab indicators, the
 *  segmented highlight, the curve editor, the focus ring, `sharedTheme()` — draws purple-pink with
 *  no widget forked. #CF5AED is cosmo's #4F7EF7 rotated to H 288° at S 80% and the same lightness
 *  (ui-brief §1), so every alpha token built on it keeps its weight.
 *
 *  What IS declared here is only what cosmo has no word for, because cosmo has no timeline:
 *  the deck / ruler / track / lane / clip surfaces, and the playhead. Each is a NAMED token so no
 *  widget carries a bare hex, and where an existing cosmo surface already has the right value the
 *  token is defined AS that surface rather than as a number that happens to match it.
 *
 *  Domain adaptation, written down as the law requires: the playhead is `destructive`, because it
 *  marks a POSITION, not a state or a selection — ui-brief §1. It is not a second accent.
 */
#pragma once
#include "../../cosmo/Theme.h"

namespace arstro
{
namespace interstellar_v1
{
    // ── cosmo's tokens, aliased (never copied) ─────────────────────────────────────────────
    namespace palette = ::arstro::cosmo_v2::palette;
    namespace radius = ::arstro::cosmo_v2::radius;
    namespace font = ::arstro::cosmo_v2::font;
    namespace metrics = ::arstro::cosmo_v2::metrics;
    using ::arstro::cosmo_v2::sharedTheme;

    /** The accent: purple-pink #CF5AED (R-UI-2). Call ONCE at startup, BEFORE the first frame —
     *  the App constructor and every harness main do. A widget that copied the accent into a
     *  member at construction would keep the old blue, which is why "before the first frame" is
     *  cosmo's contract for the slot and therefore this one's. */
    inline void installInterstellarAccent() { ::arstro::cosmo_v2::palette::setAccent(artboard::Color::hex(0xCF5AED)); }

    // ── the spacing ladder: cosmo's 3.25 px unit (a 13 px root, 0.25 rem) ───────────────────
    namespace space
    {
        inline constexpr double u(double n) { return n * 3.25; }
        inline constexpr double padX() { return u(3); }        // 9.75 — the dominant panel padding
        inline constexpr double gap() { return u(2); }         // 6.5
        inline constexpr double rowH() { return u(7); }        // 22.75 — a list row
        inline constexpr double barH() { return u(9); }        // 29.25 — the top bar, deck headers
        inline constexpr double indent() { return u(4); }      // 13 — one tree level
        // Home and modals use the coarser 8/16/32 rhythm (design rule §2.4), like cosmo's HomeScreen.
        inline constexpr double home8() { return 8.0; }
        inline constexpr double home16() { return 16.0; }
        inline constexpr double home32() { return 32.0; }
    }

    // ── motion: the durations cosmo uses, named once (design rule §2.5) ─────────────────────
    namespace motion
    {
        inline constexpr double kHoverMs = artboard::interaction::kHoverMs;  // 120
        inline constexpr double kModalOpenMs = 150.0;
        inline constexpr double kModalCloseMs = 120.0;
        inline constexpr double kDropdownMs = 160.0;     // menu grow
        inline constexpr double kDissolveMs = 160.0;     // a picture cross-dissolve — LINEAR
        inline constexpr double kScrollMs = 180.0;       // eased scroll everywhere
        inline constexpr double kCrossFadeMs = 200.0;    // tab page cross-fade
        inline constexpr double kSelectMs = 200.0;       // selection ring / wash slide
        inline constexpr double kSlideMs = 220.0;        // segmented / tab highlight travel
        inline constexpr double kCatchUpMs = 220.0;      // a value catching up to a source it does not control
        inline constexpr double kZoomMs = 220.0;         // timeline zoom
        inline constexpr double kReflowMs = 260.0;       // home-grid reflow
        inline constexpr double kScreenFadeMs = 260.0;   // shell cross-fade (Home / Loading / Edit)
        inline constexpr double kProgressInMs = 120.0;   // a progress bar fades in fast...
        inline constexpr double kProgressOutMs = 300.0;  // ...and out slowly, so its fill finishes first
        inline constexpr double kPulseMs = 1400.0;       // an indeterminate sweep's PERIOD (a timing, not a tween)
    }

    // ── Interstellar-only surfaces: named, and derived from cosmo's where cosmo has the value ──
    namespace surface
    {
        inline artboard::Color monitorBg() { return palette::canvasBg(); }        // the picture's stage
        inline artboard::Color deckBg() { return palette::filmstripBg(); }        // the bottom deck
        inline artboard::Color rulerBg() { return palette::histogramBg(); }       // timeline ruler band
        inline artboard::Color trackHeaderBg() { return palette::leftRailBg(); }  // track header column
        inline artboard::Color laneBg() { return palette::segmentedBg(); }        // a track lane
        inline artboard::Color laneAltBg() { return palette::whiteAlpha(0.012); } // every other lane, a breath lighter
        inline artboard::Color clipVideo() { return artboard::Color::hex(0x2B2B2B); }  // a video clip plate
        inline artboard::Color clipAudio() { return palette::popover(); }              // an audio clip plate
        inline artboard::Color thumbPlaceholder() { return artboard::Color::hex(0x111111); }  // = cosmo's card plate
        inline artboard::Color skeleton() { return palette::whiteAlpha(0.045); }       // a loading skeleton block
        inline artboard::Color scrim(double a) { return artboard::Color{0.02, 0.02, 0.02, a}; }
        /** The playhead: `destructive`, as a documented domain adaptation (ui-brief §1). */
        inline artboard::Color playhead() { return palette::destructive(); }
        inline artboard::Color marker() { return palette::whiteAlpha(0.62); }
    }

    // ── fixed shell metrics, shared by more than one widget ─────────────────────────────────
    namespace shell
    {
        inline constexpr double topBarH() { return 29.25; }   // cosmo's TopBar::kHeight
        /** cosmo's RightColumn::kWidth — the reused panels sit at the width they were designed at. */
        inline constexpr double rightW() { return 324.0; }
        inline constexpr double leftW() { return space::u(72); }   // 234 — rack tree / source bin / checks
        inline constexpr double transportH() { return 39.0; }       // cosmo's ActionBar::kHeight
        /** THE timeline's time origin: the track-header column width. Every time<->x mapping in
         *  the Cut tab reads this one token (ruler, lanes, clips, markers, playhead, drag), so the
         *  ruler and the clips cannot disagree about where 0 s is. */
        inline constexpr double headerWidth() { return space::u(36); }   // 117
        inline constexpr double rulerH() { return space::u(7); }         // 22.75
        inline constexpr double trackH() { return space::u(11); }        // 35.75
        /** Pixels one wheel notch is worth when the host reports notches (App::wheel). A list then
         *  scrolls ~1.2 rows per notch; a strip converts back to "one cell per notch". */
        inline constexpr double wheelNotchPx() { return 40.0; }
        inline constexpr double minMonitorW() { return 360.0; }          // the monitor's floor
        inline constexpr double minMonitorH() { return 160.0; }
        /** The deck is ONE height for every tab so the monitor never moves on a tab switch
         *  (R-UI-3); derived from the window height, clamped. */
        inline double deckH(double windowH)
        {
            const double h = windowH * 0.30;
            return h < 176.0 ? 176.0 : (h > 264.0 ? 264.0 : h);
        }
    }
}
}
