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
#include "theme/ThemeMode.h"

#include <cmath>
#include <vector>

namespace arstro
{
namespace androidshell
{
    struct ShellState
    {
        // Which colour scheme every surface renders in (bound by status bar, QS, launcher, …).
        artboard::Observable<ThemeMode> themeMode{ThemeMode::Dark};

        // Shade pull-down fractions in [0,1] (0 = closed, 1 = fully open). The status-bar drag
        // bands (M3) drive these; the notification (M4) and quick-settings (M5) shades render
        // from them, and their open/close animations ease them.
        artboard::Observable<double> notificationsExpansion{0.0};
        artboard::Observable<double> quickSettingsExpansion{0.0};

        // Where each shade settles once no finger holds it (plan §2.4): < 0 = a finger drives the
        // fraction directly; 0 / 1 = advanceShades() eases it closed / open, so a released shade
        // never snaps. settleShade() is what a release, a fling, or a tap on the scrim calls.
        double notificationsTarget = -1.0, quickSettingsTarget = -1.0;
        static constexpr double kOpenThreshold = 0.25;   // released past 25% of the pull → open
        static constexpr double kFlingOpen = 500.0;      // |vy| px/s that decides regardless of distance

        void holdShade(bool notifications)
        {
            // A finger took this shade: stop its easing, and close the other one (dual shade —
            // only one side is ever open).
            (notifications ? notificationsTarget : quickSettingsTarget) = -1.0;
            (notifications ? quickSettingsTarget : notificationsTarget) = 0.0;
        }
        void settleShade(bool notifications, double target)
        {
            (notifications ? notificationsTarget : quickSettingsTarget) = target;
        }
        // Release by distance: past the threshold opens, short of it springs back closed.
        void releaseShade(bool notifications)
        {
            const double f = (notifications ? notificationsExpansion : quickSettingsExpansion).get();
            settleShade(notifications, f > kOpenThreshold ? 1.0 : 0.0);
        }

        // One frame of easing toward each target. Returns true while either shade is moving.
        bool advanceShades()
        {
            bool moving = false;
            auto step = [&moving](artboard::Observable<double> &e, double target) {
                if (target < 0.0) return;
                const double cur = e.get();
                double next = cur + (target - cur) * 0.25;
                if (std::fabs(target - next) < 0.002) next = target;
                if (next != cur) { e.set(next); moving = true; }
            };
            step(notificationsExpansion, notificationsTarget);
            step(quickSettingsExpansion, quickSettingsTarget);
            return moving;
        }

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
