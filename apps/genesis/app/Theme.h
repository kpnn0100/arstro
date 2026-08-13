/*
 *  Genesis — design tokens.
 *
 *  Same token STRUCTURE as cosmo (the reference implementation of the Arstro desktop
 *  language): near-black neutral surfaces, ONE accent used only for interactive/selected
 *  state, a restrained grey ramp, a small named radius scale, one type ramp. The colours
 *  differ — Genesis takes a violet accent where cosmo takes blue — because each app is its
 *  own product; the structure, motion, spacing rhythm and hover language do not differ.
 *
 *  No widget may write a raw hex or a magic radius: cite a token here, or add one.
 */
#pragma once
#include <artboard/artboard.h>

namespace genesis
{
namespace ui
{
    /** How a message reads: which palette colour carries it. Declared here beside the
     *  palette rather than in App, so a panel can report status without depending on App. */
    enum class StatusLevel { Info, Good, Warn, Bad };

    /** The Artboard control theme, built from the tokens below (cached). */
    const artboard::Theme &theme();

    namespace font
    {
        inline const char *sans() { return "DM Sans"; }
        inline const char *sansMedium() { return "DM Sans Medium"; }
        inline const char *sansSemiBold() { return "DM Sans SemiBold"; }
        inline const char *mono() { return "JetBrains Mono"; }
        inline const char *monoMedium() { return "JetBrains Mono Medium"; }
    }

    namespace palette
    {
        // The Arstro desktop palette, taken from cosmo (the reference implementation) so the
        // two apps read as one family: a near-black neutral ramp, ONE accent used only for
        // interactive/selected state, and a border that is a white alpha rather than a grey.
        inline artboard::Color background() { return artboard::Color::hex(0x141414); }
        inline artboard::Color foreground() { return artboard::Color::hex(0xDBDBDB); }
        inline artboard::Color card() { return artboard::Color::hex(0x1C1C1C); }
        inline artboard::Color popover() { return artboard::Color::hex(0x222222); }
        inline artboard::Color primary() { return artboard::Color::hex(0x4F7EF7); }   // the single accent
        inline artboard::Color primaryForeground() { return artboard::Color::hex(0xFFFFFF); }
        inline artboard::Color secondary() { return artboard::Color::hex(0x252525); }
        inline artboard::Color secondaryForeground() { return artboard::Color::hex(0xAAAAAA); }
        inline artboard::Color muted() { return artboard::Color::hex(0x191919); }
        inline artboard::Color mutedForeground() { return artboard::Color::hex(0x636363); }
        inline artboard::Color destructive() { return artboard::Color::hex(0xE5534B); }
        inline artboard::Color success() { return artboard::Color::hex(0x3FB950); }
        inline artboard::Color warning() { return artboard::Color::hex(0xE0A64B); }
        inline artboard::Color border() { return artboard::Color{1.0, 1.0, 1.0, 0.072}; }
        inline artboard::Color input() { return artboard::Color::hex(0x252525); }

        // Per-surface literals, named here so no widget re-hardcodes one.
        inline artboard::Color stageBg() { return artboard::Color::hex(0x0A0A0A); }    // the preview stage
        inline artboard::Color railBg() { return artboard::Color::hex(0x161616); }     // left/right columns
        inline artboard::Color chromeBg() { return artboard::Color::hex(0x121212); }   // top bar + bottom panel
        inline artboard::Color gridLine() { return artboard::Color{1.0, 1.0, 1.0, 0.04}; }
        inline artboard::Color frameEdge() { return artboard::Color{1.0, 1.0, 1.0, 0.16}; }

        inline artboard::Color white() { return artboard::Color::rgba(255, 255, 255); }
        inline artboard::Color whiteAlpha(double a) { return artboard::Color{1.0, 1.0, 1.0, a}; }
        inline artboard::Color primaryAlpha(double a)
        { return artboard::Color{0x4F / 255.0, 0x7E / 255.0, 0xF7 / 255.0, a}; }
        inline artboard::Color destructiveAlpha(double a)
        { return artboard::Color{0xE5 / 255.0, 0x53 / 255.0, 0x4B / 255.0, a}; }
        inline artboard::Color successAlpha(double a)
        { return artboard::Color{0x3F / 255.0, 0xB9 / 255.0, 0x50 / 255.0, a}; }
        inline artboard::Color warningAlpha(double a)
        { return artboard::Color{0xE0 / 255.0, 0xA6 / 255.0, 0x4B / 255.0, a}; }

        /** Canonical hover feedback for self-drawn regions (cosmo's R-G-3 value). Child-
         *  Segment controls use artboard::hoverBox() instead; both are driven by the
         *  animated hoverAmount, so neither can pop. */
        inline artboard::Color hoverWash(double t) { return whiteAlpha(0.07 * t); }
        /** Canonical selected-row wash. */
        inline artboard::Color selectedWash(double t) { return primaryAlpha(0.16 * t); }
    }

    // cosmo uses small literal per-component radii rather than one uniform scale; Genesis
    // cites the same constants so the two apps' corners agree.
    namespace radius
    {
        inline double hairline() { return 1.0; }
        inline double control() { return 2.0; }
        inline double panel() { return 4.0; }
        inline double pill() { return 9999.0; }
    }

    namespace type
    {
        inline double micro() { return 10.0; }   // field labels, chips
        inline double small() { return 11.5; }   // rows, values
        inline double body() { return 13.0; }    // panel titles, buttons
        inline double title() { return 15.0; }   // the document name
    }

    namespace metrics
    {
        inline double chromeH() { return 44.0; }        // top bar
        inline double railW() { return 216.0; }         // shape tree column
        inline double inspectorW() { return 268.0; }    // inspector column
        inline double panelH() { return 208.0; }        // reactions panel
        inline double rowH() { return 26.0; }           // list/field row
        inline double gap() { return 8.0; }             // the one spacing unit
        inline double pad() { return 12.0; }            // panel padding
        inline double handleHit() { return 14.0; }      // canvas resize-handle pick radius
    }
}
}
