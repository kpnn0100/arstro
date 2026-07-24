/*
 *  arstro-android-shell — AndroidColors (M2.1): the Material 3 colour role table.
 *
 *  The ONE source of colour for every surface (the design-language "consistency lock": no
 *  control invents its own colour — it reads a role from here). Baseline M3 scheme, both
 *  light and dark, exact hex values from launcher/docs/android-theme-plan.md §2.2. A surface
 *  renders with `colors(state.themeMode.get())`. Pure data, header-only.
 *
 *  Dynamic wallpaper-seeded colour (the monet algorithm, plan §2.2) is a later stretch (M8+);
 *  until then these baked roles are the whole palette. Roles are M3 names so a reader can map
 *  1:1 to Material docs and to the AOSP source that inspired them.
 */
#pragma once
#include "artboard/artboard.h"
#include "theme/ThemeMode.h"

namespace arstro
{
namespace androidshell
{
    // One M3 role set (all colours opaque; callers apply alpha where a role is used as a scrim
    // or translucent fill, e.g. scrim@32%). Field order is the canonical role order — every
    // instance below initialises in this same order (C++17 has no designated initialisers).
    struct AndroidColors
    {
        artboard::Color primary, onPrimary, primaryContainer, onPrimaryContainer;
        artboard::Color secondaryContainer, onSecondaryContainer;
        artboard::Color surface, onSurface, onSurfaceVariant;
        artboard::Color surfaceContainerLowest, surfaceContainerLow, surfaceContainer,
            surfaceContainerHigh, surfaceContainerHighest;
        artboard::Color outline, outlineVariant;
        artboard::Color error, scrim, inverseSurface, inverseOnSurface;
    };

    // Light scheme (plan §2.2).
    inline const AndroidColors kLightColors = {
        artboard::Color::hex(0x6750A4),  // primary
        artboard::Color::hex(0xFFFFFF),  // onPrimary
        artboard::Color::hex(0xEADDFF),  // primaryContainer
        artboard::Color::hex(0x21005D),  // onPrimaryContainer
        artboard::Color::hex(0xE8DEF8),  // secondaryContainer
        artboard::Color::hex(0x1D192B),  // onSecondaryContainer
        artboard::Color::hex(0xFEF7FF),  // surface
        artboard::Color::hex(0x1D1B20),  // onSurface
        artboard::Color::hex(0x49454F),  // onSurfaceVariant
        artboard::Color::hex(0xFFFFFF),  // surfaceContainerLowest
        artboard::Color::hex(0xF7F2FA),  // surfaceContainerLow
        artboard::Color::hex(0xF3EDF7),  // surfaceContainer
        artboard::Color::hex(0xECE6F0),  // surfaceContainerHigh
        artboard::Color::hex(0xE6E0E9),  // surfaceContainerHighest
        artboard::Color::hex(0x79747E),  // outline
        artboard::Color::hex(0xCAC4D0),  // outlineVariant
        artboard::Color::hex(0xB3261E),  // error
        artboard::Color::hex(0x000000),  // scrim
        artboard::Color::hex(0x322F35),  // inverseSurface
        artboard::Color::hex(0xF5EFF7),  // inverseOnSurface
    };

    // Dark scheme (plan §2.2).
    inline const AndroidColors kDarkColors = {
        artboard::Color::hex(0xD0BCFF),  // primary
        artboard::Color::hex(0x381E72),  // onPrimary
        artboard::Color::hex(0x4F378B),  // primaryContainer
        artboard::Color::hex(0xEADDFF),  // onPrimaryContainer
        artboard::Color::hex(0x4A4458),  // secondaryContainer
        artboard::Color::hex(0xE8DEF8),  // onSecondaryContainer
        artboard::Color::hex(0x141218),  // surface
        artboard::Color::hex(0xE6E0E9),  // onSurface
        artboard::Color::hex(0xCAC4D0),  // onSurfaceVariant
        artboard::Color::hex(0x0F0D13),  // surfaceContainerLowest
        artboard::Color::hex(0x1D1B20),  // surfaceContainerLow
        artboard::Color::hex(0x211F26),  // surfaceContainer
        artboard::Color::hex(0x2B2930),  // surfaceContainerHigh
        artboard::Color::hex(0x36343B),  // surfaceContainerHighest
        artboard::Color::hex(0x938F99),  // outline
        artboard::Color::hex(0x49454F),  // outlineVariant
        artboard::Color::hex(0xF2B8B5),  // error
        artboard::Color::hex(0x000000),  // scrim
        artboard::Color::hex(0xE6E0E9),  // inverseSurface
        artboard::Color::hex(0x322F35),  // inverseOnSurface
    };

    // The role set for a theme mode. Surfaces call this each frame with the ShellState mode.
    inline const AndroidColors &colors(ThemeMode mode)
    {
        return mode == ThemeMode::Dark ? kDarkColors : kLightColors;
    }

} // namespace androidshell
} // namespace arstro
