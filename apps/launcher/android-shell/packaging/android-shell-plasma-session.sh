#!/usr/bin/env bash
# Plasma-based Android Shell session: run kwin_wayland (no plasmashell) + our shell.
# Suppress KWin's own edge gestures/effects via a session kwinrc profile.
export XDG_CURRENT_DESKTOP=android-shell
export KWIN_CONFIG="${XDG_CONFIG_HOME:-$HOME/.config}/arstro-android-shell/kwinrc"
exec kwin_wayland --exit-with-session=/usr/bin/arstro-android-shell
