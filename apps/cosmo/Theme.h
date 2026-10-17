/*
 *  cosmo_v2 by arstro — theme factory + design tokens.
 *
 *  Values are taken verbatim from the Figma export's theme.css (ref/extracted/
 *  src/styles/theme.css) -- this is a from-scratch visual language for the same
 *  editor cosmo already implements, so nothing here reuses cosmo's own
 *  CosmoTheme.h palette/radius scale (deliberately: see the task's "don't refer
 *  to cosmo's UI" constraint).
 *
 *  Design language: a near-black dark UI, ONE accent (a blue, used only for
 *  interactive/selected state -- active tab, slider fill, selection ring,
 *  primary button), a restrained near-neutral grey ramp otherwise. Radii are
 *  small and literal per Figma's own bracket values (1-3px), not a single
 *  uniform scale. Typography: Roboto (body/UI) + JetBrains Mono (numerics/
 *  filenames), both vendored under assets/fonts and registered at startup via
 *  Fontconfig (see linux_main.cpp) -- see Artboard's FR-22 drawText family
 *  parameter this depends on.
 */
#pragma once
#include "../../core/Artboard/include/artboard/artboard.h"

namespace arstro
{
namespace cosmo_v2
{
    artboard::Theme makeCosmoV2Theme();
    /** Cached instance (built once) for widgets that just need to read styles,
     *  rather than constructing a fresh Theme per control. */
    const artboard::Theme &sharedTheme();

    namespace font
    {
        // The family names here are the OTHER half of a pair: they must match the names the
        // faces are registered under (CMakeLists' COSMO_FONT_LIST -> EmbeddedFonts.cpp, R-FONT-1).
        // A weight is a family name rather than a number because that is how a static weight is
        // selected at the text-stack level (Artboard FR-22).
        inline const char *sans() { return "Roboto"; }
        inline const char *sansMedium() { return "Roboto Medium"; }
        inline const char *sansSemiBold() { return "Roboto SemiBold"; }
        inline const char *mono() { return "JetBrains Mono"; }
        inline const char *monoMedium() { return "JetBrains Mono Medium"; }
    }

    // theme.css tokens, verbatim (hex values as authored in the Figma export).
    namespace palette
    {
        /** How far the neutrals have moved toward HIGH CONTRAST, 0 … 1 — a RUNTIME amount, the accent slot's pattern
         *  (R-G-5): Solaris eases it and sets it each frame (its R-ACCESS-1), so a switch fades the whole UI and a token
         *  is still the only place a colour is named. Cosmo itself never calls `setContrast`, so its pixels do not move. */
        inline double &contrastSlot()
        {
            static double k = 0.0;
            return k;
        }
        inline void setContrast(double k) { contrastSlot() = k < 0.0 ? 0.0 : (k > 1.0 ? 1.0 : k); }
        inline double contrast() { return contrastSlot(); }
        /** A neutral: `normal`, moved toward its high-contrast value by the contrast amount. */
        inline artboard::Color neutral(unsigned normal, unsigned high, double alpha = 1.0, double highAlpha = 1.0)
        {
            const double k = contrastSlot();
            artboard::Color a = artboard::Color::hex(normal);
            a.a = alpha;
            if (k <= 0.0) return a;
            const artboard::Color b = artboard::Color::hex(high);
            return artboard::Color{a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k, alpha + (highAlpha - alpha) * k};
        }

        inline artboard::Color background() { return neutral(0x141414, 0x000000); }
        inline artboard::Color foreground() { return neutral(0xDBDBDB, 0xFFFFFF); }
        inline artboard::Color card() { return neutral(0x1C1C1C, 0x0A0A0A); }
        inline artboard::Color popover() { return neutral(0x222222, 0x121212); }
        /** THE accent, as a RUNTIME token with cosmo's blue as its default (R-G-5).
         *
         *  A runtime value rather than a literal so a sibling app that REUSES cosmo's widgets as
         *  libraries — Interstellar, whose accent is purple-pink — can carry its own accent without
         *  forking a single widget. Twenty-three widget files reach the accent through `primary()`,
         *  `ring()` and `primaryAlpha()`; all three derive from this one slot, so one call at startup
         *  re-themes every one of them. Cosmo itself never calls `setAccent`, so its pixels do not
         *  change — verified by rendering all 41 named shots before and after and diffing the bytes.
         *
         *  A function-local static inside an `inline` function is ONE object program-wide (the ODR
         *  guarantees it), which is what makes this safe across translation units, and it is why the
         *  token stays a function rather than becoming a mutable global — a namespace-scope `Color`
         *  would bring back the static-init-order hazard the token contract exists to avoid. */
        inline artboard::Color &accentSlot()
        {
            static artboard::Color c = artboard::Color::hex(0x4F7EF7);
            return c;
        }
        /** Call ONCE, at startup, before the first frame. `sharedTheme()` rebuilds itself if the accent
         *  has moved since it was built, so the order relative to it does not matter; a widget that
         *  copied an accent into a member at construction would still hold the old one, which is why
         *  "before the first frame" is the contract. */
        inline void setAccent(const artboard::Color &c) { accentSlot() = c; }
        inline artboard::Color primary() { return accentSlot(); }                          // the single accent
        inline artboard::Color primaryForeground() { return artboard::Color::hex(0xFFFFFF); }
        inline artboard::Color secondary() { return neutral(0x252525, 0x2E2E2E); }
        inline artboard::Color secondaryForeground() { return neutral(0xAAAAAA, 0xFFFFFF); }
        inline artboard::Color muted() { return neutral(0x191919, 0x0A0A0A); }
        inline artboard::Color mutedForeground() { return neutral(0x636363, 0xC0C0C0); }
        inline artboard::Color destructive() { return artboard::Color::hex(0xE5534B); }
        // The other semantic state colour alongside destructive (neither is "the accent",
        // which stays reserved for interactive/selected state): a confirmation green for
        // "this finished successfully" -- the export-complete tick and the per-file
        // written wash (R-EXPORT-6).
        inline artboard::Color success() { return artboard::Color::hex(0x3FB950); }
        inline artboard::Color successAlpha(double a)
        { return artboard::Color{0x3F / 255.0, 0xB9 / 255.0, 0x50 / 255.0, a}; }
        inline artboard::Color border() { return neutral(0xFFFFFF, 0xFFFFFF, 0.072, 0.45); }  // rgba(255,255,255,.072); solid-ish when high
        inline artboard::Color input() { return neutral(0x252525, 0x1E1E1E); }
        // Elegant light entry field (the inline group-rename box, DR-TREE-5): a deliberate
        // near-white surface with near-black text -- an inverted island in the dark UI.
        inline artboard::Color inputLight() { return artboard::Color::hex(0xF4F4F5); }      // near-white field bg
        inline artboard::Color inputLightText() { return artboard::Color::hex(0x18181B); }  // near-black field text
        inline artboard::Color switchBackground() { return neutral(0x444444, 0x6A6A6A); }
        inline artboard::Color ring()
        {
            artboard::Color c = accentSlot();   // derived from the accent, so it follows setAccent (R-G-5)
            c.a = 0.5;
            return c;
        }

        // Literal per-widget surface colors used directly in the Figma source
        // (bracket values, not theme.css tokens) -- kept here so every widget
        // pulls from one place rather than re-hardcoding hex literals.
        inline artboard::Color canvasBg() { return neutral(0x0A0A0A, 0x000000); }     // photo stage
        inline artboard::Color histogramBg() { return neutral(0x0F0F0F, 0x000000); }
        inline artboard::Color leftRailBg() { return neutral(0x161616, 0x050505); }   // rail + breadcrumb
        inline artboard::Color filmstripBg() { return neutral(0x121212, 0x000000); }
        inline artboard::Color folderChipBg() { return neutral(0x1A1A1A, 0x0F0F0F); }
        inline artboard::Color segmentedBg() { return neutral(0x111111, 0x000000); }  // RGB/Hue-Sat-Lum pickers
        inline artboard::Color curvePlotBg() { return neutral(0x0D0D0D, 0x000000); }
        inline artboard::Color white() { return artboard::Color::rgba(255, 255, 255); }
        inline artboard::Color whiteAlpha(double a) { return artboard::Color{1.0, 1.0, 1.0, a}; }
        inline artboard::Color primaryAlpha(double a)
        {
            artboard::Color c = accentSlot();   // derived from the accent, so it follows setAccent (R-G-5)
            c.a = a;
            return c;
        }

        // Canonical hover feedback (consistency lock, R-G-1): self-drawn rows/cells/
        // items wash the hovered region with hoverWash(hoverAmount); child-Segment
        // controls (Button/PillButton/IconButton) use artboard::hoverBox() instead.
        // The framework animates the amount, so these never pop.
        inline artboard::Color hoverWash(double t) { return whiteAlpha(0.07 * t); }
    }

    // Figma uses small literal per-component radii (bracket values), not one
    // uniform scale -- named here so every widget cites the same constant.
    namespace radius
    {
        inline double hairline() { return 1.0; }  // rounded-[1px]: mixer/curve/grade segmented pickers
        inline double control() { return 2.0; }   // rounded-[2px]: buttons, chips, panels, filmstrip cells
        inline double pill() { return 9999.0; }   // rounded-full: slider track/thumb, before/after pill
    }

    // Interaction hit targets -- the forgiving pick radius (px) around a small
    // drawn handle. A curve/mixer anchor dot is only ~4px, which is fiddly to
    // click and drag; the clickable area is deliberately larger than the dot so
    // points are easy to grab. Cited by every point-editing widget so the feel
    // is identical across the tone curve and the mixer hue curves (a lone
    // per-widget literal is a bug -- one token, one feel).
    namespace metrics
    {
        inline double anchorHitRadius() { return 13.0; }  // curve/mixer anchor + bezier handle pick radius
    }
}
}
