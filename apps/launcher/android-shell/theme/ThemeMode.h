/*
 *  arstro-android-shell — ThemeMode (M2.1).
 *
 *  Which colour scheme the shell renders in. Lives in the theme layer (not shell/) because
 *  the colour tables key off it and the theme layer is below ShellState. ShellState holds it
 *  as an Observable<ThemeMode> so every surface stays in sync (M1.5).
 */
#pragma once

namespace arstro
{
namespace androidshell
{
    enum class ThemeMode { Light, Dark };
}
} // namespace arstro
