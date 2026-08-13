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
 *  uniform scale. Typography: DM Sans (body/UI) + JetBrains Mono (numerics/
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
        inline const char *sans() { return "DM Sans"; }
        inline const char *sansMedium() { return "DM Sans Medium"; }
        inline const char *sansSemiBold() { return "DM Sans SemiBold"; }
        inline const char *mono() { return "JetBrains Mono"; }
        inline const char *monoMedium() { return "JetBrains Mono Medium"; }
    }

    // theme.css tokens, verbatim (hex values as authored in the Figma export).
    namespace palette
    {
        inline artboard::Color background() { return artboard::Color::hex(0x141414); }
        inline artboard::Color foreground() { return artboard::Color::hex(0xDBDBDB); }
        inline artboard::Color card() { return artboard::Color::hex(0x1C1C1C); }
        inline artboard::Color popover() { return artboard::Color::hex(0x222222); }
        inline artboard::Color primary() { return artboard::Color::hex(0x4F7EF7); }       // the single accent
        inline artboard::Color primaryForeground() { return artboard::Color::hex(0xFFFFFF); }
        inline artboard::Color secondary() { return artboard::Color::hex(0x252525); }
        inline artboard::Color secondaryForeground() { return artboard::Color::hex(0xAAAAAA); }
        inline artboard::Color muted() { return artboard::Color::hex(0x191919); }
        inline artboard::Color mutedForeground() { return artboard::Color::hex(0x636363); }
        inline artboard::Color destructive() { return artboard::Color::hex(0xE5534B); }
        // The other semantic state colour alongside destructive (neither is "the accent",
        // which stays reserved for interactive/selected state): a confirmation green for
        // "this finished successfully" -- the export-complete tick and the per-file
        // written wash (R-EXPORT-6).
        inline artboard::Color success() { return artboard::Color::hex(0x3FB950); }
        inline artboard::Color successAlpha(double a)
        { return artboard::Color{0x3F / 255.0, 0xB9 / 255.0, 0x50 / 255.0, a}; }
        inline artboard::Color border() { return artboard::Color{1.0, 1.0, 1.0, 0.072}; }  // rgba(255,255,255,.072)
        inline artboard::Color input() { return artboard::Color::hex(0x252525); }
        // Elegant light entry field (the inline group-rename box, DR-TREE-5): a deliberate
        // near-white surface with near-black text -- an inverted island in the dark UI.
        inline artboard::Color inputLight() { return artboard::Color::hex(0xF4F4F5); }      // near-white field bg
        inline artboard::Color inputLightText() { return artboard::Color::hex(0x18181B); }  // near-black field text
        inline artboard::Color switchBackground() { return artboard::Color::hex(0x444444); }
        inline artboard::Color ring() { return artboard::Color{0x4F / 255.0, 0x7E / 255.0, 0xF7 / 255.0, 0.5}; }

        // Literal per-widget surface colors used directly in the Figma source
        // (bracket values, not theme.css tokens) -- kept here so every widget
        // pulls from one place rather than re-hardcoding hex literals.
        inline artboard::Color canvasBg() { return artboard::Color::hex(0x0A0A0A); }     // photo stage
        inline artboard::Color histogramBg() { return artboard::Color::hex(0x0F0F0F); }
        inline artboard::Color leftRailBg() { return artboard::Color::hex(0x161616); }   // rail + breadcrumb
        inline artboard::Color filmstripBg() { return artboard::Color::hex(0x121212); }
        inline artboard::Color folderChipBg() { return artboard::Color::hex(0x1A1A1A); }
        inline artboard::Color segmentedBg() { return artboard::Color::hex(0x111111); }  // RGB/Hue-Sat-Lum pickers
        inline artboard::Color curvePlotBg() { return artboard::Color::hex(0x0D0D0D); }
        inline artboard::Color white() { return artboard::Color::rgba(255, 255, 255); }
        inline artboard::Color whiteAlpha(double a) { return artboard::Color{1.0, 1.0, 1.0, a}; }
        inline artboard::Color primaryAlpha(double a) { return artboard::Color{0x4F / 255.0, 0x7E / 255.0, 0xF7 / 255.0, a}; }

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
