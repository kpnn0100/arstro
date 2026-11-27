#!/bin/sh
# arstro-shell — L5, headless: the REAL host with REAL app processes attached over the app socket.
#
#   live.sh <arstro-shell> <bundles-dir> <work-dir>
#
# Scripted fingers go through the host's own touch entry point, so this exercises the routing a
# screen would: the system claiming an edge swipe, the app getting the rest, Back being offered
# to the app first, a paused app keeping its picture. It needs no display.
set -eu
SHELL_BIN=$1; BUNDLES=$2; WORK=$3
rm -rf "$WORK"; mkdir -p "$WORK"; cd "$WORK"
fail() { echo "FAIL: $1"; echo "--- output ---"; cat out.txt; exit 1; }
expect() { grep -q -- "$1" out.txt || fail "expected: $1"; }

cat > run.script <<'SCRIPT'
clock 09:41 Friday, 27 November
wait 200
tap 428 422
wait 100
shot launch-a.png
wait 50
shot launch-b.png
wait 1500
state
shot app.png
swipe 480 536 480 250 110
wait 1200
state
tap 428 422
wait 1200
swipe 3 300 140 300 160
wait 1200
state
app launch arstro.settings
wait 1200
nav recents
wait 1000
shot recents.png
state
app close 1
wait 800
state
frobnicate
SCRIPT

"$SHELL_BIN" --headless --size 1920x1080 --script run.script --apps "$BUNDLES" > out.txt 2> err.txt && fail "an unknown line must make the run fail"
grep -q "unknown command: frobnicate" err.txt || fail "the rejection must name the line"

# 1. The tap on the icon started the app's process and it attached: running, not just starting.
expect "task 1 app=arstro.clock state=running"
# 2. A fast swipe up from the bottom edge went home and paused it.
expect "nav home task=1"
expect "task 1 app=arstro.clock state=paused"
# 3. Back from the left edge: Clock does not use Back, so the shell went home (the 3rd dump).
[ "$(grep -c '^nav home task=1' out.txt)" -ge 2 ] || fail "Back must fall through to home"
# 4. Recents with two cards, then closing one leaves the other.
expect "nav recents task=2"
[ "$(grep -c '^task 2 app=arstro.settings' out.txt)" -ge 2 ] || fail "settings must survive closing clock"
tail -n 14 out.txt | grep -q "^task 1 " && fail "task 1 must be gone after app close"
# 5. Two frames 50 ms apart mid-launch differ: the launch is a motion, not a cut.
cmp -s launch-a.png launch-b.png && fail "the launch did not move between two frames"
for f in app.png recents.png; do [ -s "$f" ] || fail "missing $f"; done
echo "ok: live shell, real apps"
