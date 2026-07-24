/*
 *  arstro-android-shell — ShellState: the cross-surface single source of truth (M1.5).
 *
 *  The surfaces (status bar, shades, launcher, …) are independent Artboard roots but must
 *  agree on shared state — theme mode, how far each shade is pulled down, the window list.
 *  Each such value is an `artboard::Observable<T>` (FR-23): a surface `observe()`s it and
 *  re-renders when it changes, so e.g. the status bar and quick settings can never disagree
 *  about the theme. ShellState also holds the two platform seams (`CompositorBridge`,
 *  `SystemServices`) every surface reaches the system through. Owned by `main`, passed by
 *  reference to each surface as it is built (M3+).
 *
 *  Grows with the milestones: the notification store is added here in M4, and the quick-
 *  settings tile states in M5, as those surfaces are built.
 */
#pragma once
#include "artboard/artboard.h"
#include "compositor/CompositorBridge.h"
#include "system/SystemServices.h"

#include <vector>

namespace arstro
{
namespace androidshell
{
    enum class ThemeMode { Light, Dark };

    struct ShellState
    {
        // Which colour scheme every surface renders in (bound by status bar, QS, launcher, …).
        artboard::Observable<ThemeMode> themeMode{ThemeMode::Dark};

        // Shade pull-down fractions in [0,1] (0 = closed, 1 = fully open). The status-bar drag
        // bands (M3) drive these; the notification (M4) and quick-settings (M5) shades render
        // from them, and their open/close animations ease them.
        artboard::Observable<double> notificationsExpansion{0.0};
        artboard::Observable<double> quickSettingsExpansion{0.0};

        // MRU window list, mirrored from the CompositorBridge (populated in M7 via
        // onWindowsChanged); recents/overview + quick-switch (M7) read it.
        artboard::Observable<std::vector<WindowInfo>> windows{std::vector<WindowInfo>{}};

        // The two platform seams. References — main owns the concrete impls (NullBridge +
        // FakeSystemServices for now; real backends swap in at M3/M7/M8 without touching surfaces).
        CompositorBridge &bridge;
        SystemServices &services;

        ShellState(CompositorBridge &b, SystemServices &s) : bridge(b), services(s)
        {
            // Keep the window Observable in sync with the bridge's notifications.
            bridge.onWindowsChanged = [this] { windows.set(bridge.listWindows()); };
        }
    };

} // namespace androidshell
} // namespace arstro
