#!/usr/bin/env bash
# R-SVC-8/R-SVC-9 acceptance test: launch a real cosmo window, drive it entirely from a
# shell, assert the event stream, and prove a GUI dump equals a CLI dump of the same project.
#
#   apps/cosmo/tests/acceptance/run.sh <project.cmp> [build-dir]
#
# Exit 0 = passed. Any other = failed, with the reason on stderr.
#
# Why a shell script rather than a ctest case: it needs a display and a real window, which no
# unit test may assume. It is committed anyway, because before this existed the acceptance
# evidence lived only in a commit message — and a claim you cannot re-run is a claim, not a
# test. Everything it drives is the ordinary product surface: `--control`, `cosmo-cc attach`,
# `cosmo-cc project --print`. There is no test-only code path anywhere in it.
set -u

PROJECT="${1:-}"
BUILD="${2:-build}"
if [[ -z "$PROJECT" || ! -f "$PROJECT" ]]; then
    echo "usage: $0 <project.cmp> [build-dir]" >&2
    exit 2
fi
if [[ -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
    echo "SKIP: no display — this test drives a real window" >&2
    exit 0
fi

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COSMO="$BUILD/apps/cosmo/cosmo"
CC="$BUILD/apps/cosmo/cli/cosmo-cc"
for bin in "$COSMO" "$CC"; do
    [[ -x "$bin" ]] || { echo "FAIL: $bin not built" >&2; exit 1; }
done

WORK="$(mktemp -d)"
SOCK="$WORK/control.sock"
# Sandboxed config, so the run can never touch the user's real settings/recents/log (D-8:
# always set XDG_CONFIG_HOME explicitly in a scripted run).
export XDG_CONFIG_HOME="$WORK/config"
mkdir -p "$XDG_CONFIG_HOME/cosmo_v2"
printf 'cosmosettings=1\npreviewEdge=1600\nthreads=0\nuseGpu=1\ncpuPercent=25\n' \
    > "$XDG_CONFIG_HOME/cosmo_v2/settings.txt"

APP_PID=""
cleanup() {
    [[ -n "$APP_PID" ]] && kill "$APP_PID" 2>/dev/null
    wait "$APP_PID" 2>/dev/null
    rm -rf "$WORK"
}
trap cleanup EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }

sed "s|@PROJECT@|$PROJECT|" "$HERE/drive-a-live-window.txt" > "$WORK/script.txt"

# ── 1. the SAME script, headless, with no window in the process at all ──
# It must be the same script on both sides or the comparison in step 4 is meaningless: an
# unedited project obviously differs from an edited one, and diffing those would "pass" only
# by accident. R-SVC-9 is about the same commands producing the same state.
"$CC" run --script "$WORK/script.txt" --stable > "$WORK/cli-run.txt" 2>"$WORK/cli.err" \
    || fail "cosmo-cc run failed: $(tail -3 "$WORK/cli.err")"
awk '/^screen=/{on=1} on' "$WORK/cli-run.txt" > "$WORK/cli.txt"
[[ -s "$WORK/cli.txt" ]] || fail "cosmo-cc run produced no state dump"

# ── 2. the same project, opened in a real window, driven over the socket ──
"$COSMO" --control "$SOCK" > "$WORK/app.log" 2>&1 &
APP_PID=$!
for _ in $(seq 1 100); do [[ -S "$SOCK" ]] && break; sleep 0.2; done
[[ -S "$SOCK" ]] || fail "the window never opened its control socket (see $WORK/app.log)"

"$CC" attach "$SOCK" --script "$WORK/script.txt" --watch --timeout 300 > "$WORK/stream.txt" 2>&1 \
    || fail "attach reported a rejected command or a timeout (see $WORK/stream.txt)"

kill -0 "$APP_PID" 2>/dev/null || fail "the window died during the run (see $WORK/app.log)"

# ── 3. assert the event stream — a silent no-op must not read as success ──
for want in \
    "\[evt\] project.opening" \
    "\[evt\] entry.decoded" \
    "\[evt\] load.finished decoded=" \
    "\[evt\] screen.changed editor" \
    "\[evt\] selection.changed" \
    "\[evt\] params.changed" \
    "\[evt\] history.changed"
do
    grep -qE "$want" "$WORK/stream.txt" || fail "event stream is missing: $want"
done
grep -qE "load.finished decoded=0 " "$WORK/stream.txt" && fail "the load attached nothing (D-13)"

# ── 4. R-SVC-9: the GUI's dump must equal the CLI's, byte for byte ──
awk '/state\.begin/{on=1;next} /state\.end/{on=0} on' "$WORK/stream.txt" > "$WORK/gui.txt"
[[ -s "$WORK/gui.txt" ]] || fail "no state dump came back from the window"
if ! diff -q "$WORK/cli.txt" "$WORK/gui.txt" >/dev/null; then
    echo "FAIL: a CLI-opened project and a GUI-opened project disagree (R-SVC-9):" >&2
    diff "$WORK/cli.txt" "$WORK/gui.txt" >&2
    exit 1
fi

IMAGES=$(grep -c '^  node=' "$WORK/gui.txt")
echo "PASS: the same script drove a headless service and a live window; $IMAGES nodes;"
echo "      both dumps identical (R-SVC-9), event stream complete (R-SVC-8)"
