#!/usr/bin/env bash
# Regenerate the android_theme sample-sheet golden PNGs (M2.5), light + dark.
# Build the shell first (cmake --build build), then run this from anywhere.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"     # .../android-shell/tests
SHELL_DIR="$(dirname "$HERE")"                            # .../android-shell
REPO_ROOT="$(cd "$SHELL_DIR/../.." && pwd)"
GOLDEN="$HERE/golden"; mkdir -p "$GOLDEN"
BIN="$(find "$REPO_ROOT/build" -name arstro-android-shell -type f 2>/dev/null | head -1)"
[ -z "${BIN:-}" ] && { echo "build arstro-android-shell first: cmake --build build"; exit 1; }
"$BIN" --sample-sheet=dark  --render-png="$GOLDEN/samplesheet_dark.png"
"$BIN" --sample-sheet=light --render-png="$GOLDEN/samplesheet_light.png"
for st in dark light charging nowifi dnd; do
  "$BIN" --status-bar="$st" --render-png="$GOLDEN/statusbar_$st.png"
done
for st in empty three expanded; do
  "$BIN" --notif-panel="$st" --render-png="$GOLDEN/notifpanel_$st.png"
done
echo "goldens -> $GOLDEN"
