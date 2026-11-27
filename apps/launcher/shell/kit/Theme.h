/*
 *  arstro kit — the touch stack's design tokens (R-UI-1).
 *
 *  Colour, type and the motion vocabulary are the family's: this file ALIASES cosmo's token
 *  namespaces rather than copying them, because a forked token file is a divergence with a delay
 *  fuse (arstro.rule.design §2). What a touch screen may change is METRICS — a finger is not a
 *  mouse — so sizes, radii and the type ramp are declared here on an 8 dp grid.
 *
 *  Everything is in dp. Tokens are inline functions, never namespace-scope constants.
 */
#pragma once
#include "../../../cosmo/Theme.h"

namespace arstro
{
namespace kit
{
    namespace palette = cosmo_v2::palette;
    namespace font = cosmo_v2::font;

    /** Surfaces of the shell, darkest at the back. Named so no view hardcodes a hex. */
    namespace surface
    {
        inline artboard::Color wallpaperTop() { return artboard::Color::hex(0x101216); }
        inline artboard::Color wallpaperBottom() { return artboard::Color::hex(0x08090B); }
        inline artboard::Color sheet() { return artboard::Color::hex(0x121316); }     // the shade
        inline artboard::Color tile() { return palette::whiteAlpha(0.06); }          // a quiet tile on a sheet
        inline artboard::Color tilePressed() { return palette::whiteAlpha(0.11); }
        inline artboard::Color appBackground() { return palette::background(); }
        inline artboard::Color scrim(double a) { return artboard::Color{0, 0, 0, a}; }
        inline artboard::Color hairline() { return palette::whiteAlpha(0.08); }
    }

    /** The spacing ladder: an 8 dp grid, with the half step for tight pairs. */
    namespace space
    {
        inline constexpr double u(double n) { return n * 8.0; }
        inline constexpr double half() { return 4.0; }
        inline constexpr double gap() { return 8.0; }
        inline constexpr double pad() { return 16.0; }     // the dominant padding
        inline constexpr double margin() { return 24.0; }  // screen edge
    }

    namespace radius
    {
        inline double chip() { return 12.0; }
        inline double tile() { return 20.0; }   // tiles, list groups, recents cards
        inline double sheet() { return 28.0; }  // sheets and dialogs
        inline double icon() { return 0.29; }   // app icon: a FRACTION of its side, so it scales
        inline double pill() { return 9999.0; }
    }

    namespace metric
    {
        inline constexpr double touchTarget() { return 48.0; }  // nothing tappable is smaller
        inline constexpr double statusBar() { return 28.0; }
        inline constexpr double navBar() { return 20.0; }       // the strip the gesture pill lives in
        inline constexpr double rowH() { return 64.0; }
        inline constexpr double iconSize() { return 60.0; }     // a home-screen app icon
        inline constexpr double glyph() { return 22.0; }        // a line icon
        inline constexpr double topBar() { return 64.0; }       // an app's title bar
        inline constexpr double contentMax() { return 640.0; }  // a reading column never grows wider
        inline constexpr double swatch() { return 32.0; }       // a colour dot inside its 48 dp target
        inline constexpr double ring() { return 2.0; }          // the stroke of a selection ring
    }

    /** The accents a user may choose from (Settings › Display). The first is the family default.
     *  Named here so the one place that offers a colour and the shell that applies it agree. */
    namespace accents
    {
        struct Choice
        {
            const char *name;
            const char *hex;  // exactly what `set accent=` takes
        };
        inline constexpr int count() { return 6; }
        inline const Choice &at(int i)
        {
            static const Choice all[] = {{"Blue", "#4F7EF7"},  {"Violet", "#8E7CF8"}, {"Rose", "#E5679B"},
                                         {"Amber", "#E9944A"}, {"Green", "#3DB57F"},  {"Teal", "#2FAFC2"}};
            return all[i < 0 ? 0 : i >= count() ? count() - 1 : i];
        }
    }

    /** The type ramp, larger than the desktop's because the screen is at arm's length. */
    namespace type
    {
        inline constexpr double micro() { return 11.0; }
        inline constexpr double caption() { return 12.0; }
        inline constexpr double label() { return 13.0; }
        inline constexpr double body() { return 15.0; }
        inline constexpr double title() { return 20.0; }
        inline constexpr double headline() { return 28.0; }
        inline constexpr double display() { return 72.0; }
        /** The suite's one baseline rule. */
        inline constexpr double baseline(double centerY, double sizePx) { return centerY + sizePx * 0.35; }
    }

    /** Motion: durations from Artboard's named scale, spring speeds for anything a finger can
     *  interrupt. A spring is used wherever a gesture may grab the value mid-flight, because a
     *  tween cannot be re-aimed without a jump in speed. */
    namespace motion
    {
        inline constexpr double press() { return artboard::motion::kDurationShort2; }   // 100
        inline constexpr double fade() { return artboard::motion::kDurationShort4; }    // 200
        inline constexpr double move() { return artboard::motion::kDurationMedium2; }   // 300
        inline constexpr double page() { return artboard::motion::kDurationMedium4; }   // 400
        inline constexpr double dissolve() { return 160.0; }                            // LINEAR alpha only
        inline constexpr double springNav() { return 14.0; }     // card / shade travel: settles in ~0.4 s
        inline constexpr double springFast() { return artboard::motion::kSpatialFast; }
        inline constexpr double springFade() { return artboard::motion::kEffectsSlow; }
    }
}
}
