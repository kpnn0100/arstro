/*
 *  Cosmo by arstro — theme factory + design tokens.
 *
 *  Design language (a dark, dense pro photo-editor): a NEUTRAL near-black base (so
 *  it never biases how you read the photo's colour), a calm grey type ramp, and a
 *  SINGLE amber accent reserved for interaction (slider fills, the active tab, focus)
 *  — not for decoration. One corner-radius scale, locked. Hierarchy comes from
 *  weight and colour, not loud size or accent bars.
 */
#pragma once
#include "../Artboard/include/artboard/artboard.h"

namespace arstro
{
namespace cosmo
{
    artboard::Theme makeCosmoTheme(const artboard::Color &accent);

    // Neutral grey ramp + one accent. Near-neutral on purpose (minimal hue bias).
    namespace palette
    {
        inline artboard::Color bg() { return artboard::Color::hex(0x0e0e11); }      // app background
        inline artboard::Color panel() { return artboard::Color::hex(0x17181c); }   // elevated surface
        inline artboard::Color surface() { return artboard::Color::hex(0x202228); } // track / sub-surface
        inline artboard::Color line() { return artboard::Color::hex(0x2a2c32); }    // hairline border
        inline artboard::Color ink() { return artboard::Color::hex(0xe9eaec); }     // primary text
        inline artboard::Color muted() { return artboard::Color::hex(0x9a9ca1); }   // secondary text
        inline artboard::Color faint() { return artboard::Color::hex(0x62646a); }   // tertiary text
        inline artboard::Color accent() { return artboard::Color::hex(0xf2b24a); }  // the single accent
        // legacy alias
        inline artboard::Color border() { return line(); }
    }

    // One radius scale, applied everywhere (shape-consistency lock).
    namespace radius
    {
        inline double panel() { return 8.0; }    // panels, frames, plots
        inline double control() { return 4.0; }  // slider tracks, combo fields, inner plots
        inline double pill() { return 999.0; }   // toggles, thumbs
    }
}
}
