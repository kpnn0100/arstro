/*
 *  arstro-android-shell — Type (M2.2): the Material 3 type ramp.
 *
 *  Named `artboard::TextStyle`s (size + family + tracking) for every text role, from plan §2.2.
 *  A surface picks a ramp entry and sets its `.color` from an AndroidColors role, so type and
 *  colour both come from the theme (the design-language consistency lock). Font FAMILIES follow
 *  FR-22: a distinct static weight is its own family name ("Roboto Medium"), because real static
 *  weights are separate font files at the OS text-stack level. The family strings only resolve to
 *  real glyphs once the TTFs are registered (theme/Fonts.h); absent them, the adapter falls back
 *  to its generic sans (same size/layout, different letterforms).
 */
#pragma once
#include "artboard/artboard.h"

namespace arstro
{
namespace androidshell
{
namespace type
{
    // Family names the registered TTFs (theme/Fonts.h) expose.
    inline const char *kRegular = "Roboto";
    inline const char *kMedium = "Roboto Medium";
    inline const char *kFlex = "Roboto Flex";  // variable font, for the large clock

    inline artboard::TextStyle make(double sizePx, const char *family, double tracking = 0.0)
    {
        artboard::TextStyle s;
        s.sizePx = sizePx;
        s.fontFamily = family;
        s.letterSpacingPx = tracking;
        return s;
    }

    // The ramp (plan §2.2). Colour is left at TextStyle's default; callers set it from a role.
    inline const artboard::TextStyle displayLarge = make(45, kRegular);
    inline const artboard::TextStyle displaySmall = make(36, kRegular);
    inline const artboard::TextStyle headlineLarge = make(32, kRegular);
    inline const artboard::TextStyle headlineMedium = make(28, kRegular);
    inline const artboard::TextStyle headlineSmall = make(24, kRegular);
    inline const artboard::TextStyle titleLarge = make(22, kRegular);
    inline const artboard::TextStyle titleMedium = make(16, kMedium);
    inline const artboard::TextStyle titleSmall = make(14, kMedium);
    inline const artboard::TextStyle bodyLarge = make(16, kRegular);
    inline const artboard::TextStyle bodyMedium = make(14, kRegular);
    inline const artboard::TextStyle bodySmall = make(12, kRegular);
    inline const artboard::TextStyle labelLarge = make(14, kMedium);
    inline const artboard::TextStyle labelMedium = make(12, kMedium);
    inline const artboard::TextStyle labelSmall = make(11, kMedium);

    // Apply a colour to a ramp entry (surfaces do `styled(type::labelMedium, colors(mode).onSurface)`).
    inline artboard::TextStyle styled(artboard::TextStyle base, const artboard::Color &color)
    {
        base.color = color;
        return base;
    }

} // namespace type
} // namespace androidshell
} // namespace arstro
