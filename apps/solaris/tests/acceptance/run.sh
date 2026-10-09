#!/usr/bin/env bash
# R-SVC-6 — the equivalence test (arstro.rule §1): ONE committed script through BOTH front ends —
# headless with `solaris-cc --watch --script`, and through a LIVE window (`solaris --control`) with
# `solaris-cc attach` — then assert the event stream and diff everything the two runs left behind:
# the outputs (the stable state dump among them), the events, the saved song, the rendered mix.
#
#   apps/solaris/tests/acceptance/run.sh [solaris] [solaris-cc]      ctest: solaris_equivalence
#
# Exit 0 = passed; 77 = SKIPPED (no display to open a window on — said loudly, never a silent
# pass); anything else = failed, the reason on stderr and the evidence left in the work dir.
#
# Modelled on apps/cosmo/tests/acceptance/run.sh. Everything it drives is the ordinary product
# surface — `--control`, `attach`, `state print --json --stable` — there is no test-only path, so it
# is also the only check that sees a widget quietly stop routing through the service.
#
# Never the user's files: the settings and recents of both runs are scratch files (SOLARIS_SETTINGS,
# SOLARIS_RECENTS), the songs and the mix are written under the work dir, and the script never plays
# (no `transport play`), so nothing sounds on the machine's audio device.
set -u

SOLARIS="${1:-build/apps/solaris/solaris}"
CC="${2:-build/apps/solaris/cli/solaris-cc}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

skip() { echo "SKIP: $*" >&2; exit 77; }
fail() { echo "FAIL: $*" >&2; [[ -n "${WORK:-}" ]] && echo "      evidence: $WORK" >&2; exit 1; }

if [[ -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
    skip "no display (DISPLAY / WAYLAND_DISPLAY unset) — this test drives a real window; the headless half is solaris_control"
fi
for bin in "$SOLARIS" "$CC"; do
    [[ -x "$bin" ]] || fail "$bin is not built"
done
SOLARIS="$(cd "$(dirname "$SOLARIS")" && pwd)/$(basename "$SOLARIS")"
CC="$(cd "$(dirname "$CC")" && pwd)/$(basename "$CC")"

WORK="${SOLARIS_TEST_DIR:-$(mktemp -d)}/equivalence"
rm -rf "$WORK"
mkdir -p "$WORK"
# The comment and blank lines go before EITHER face sees the script: both answer them with nothing,
# and the lines that remain are exactly the same commands, in the same order, for both.
grep -v -E '^[[:space:]]*(#|$)' "$HERE/a-song-made-twice.txt" > "$WORK/script.txt"

# Both runs happen in the SAME directory (cleared between them), so every relative path — the song,
# the mix, the socket — is the same text in both, and the dumps compare without any rewriting.
RUN="$WORK/run"
fresh() { rm -rf "$RUN"; mkdir -p "$RUN"; }
export SOLARIS_SETTINGS="$RUN/settings.txt" SOLARIS_RECENTS="$RUN/recents"

APP_PID=""
cleanup() {
    if [[ -n "$APP_PID" ]]; then
        kill "$APP_PID" 2>/dev/null
        wait "$APP_PID" 2>/dev/null
    fi
}
trap cleanup EXIT

# ── 1. headless: solaris-cc is the whole app with no window ──
fresh
( cd "$RUN" && "$CC" --watch --script ../script.txt > ../cli.out 2> ../cli.err ) \
    || fail "solaris-cc refused the script: $(tail -3 "$WORK/cli.err")"
cp "$RUN/song.slp" "$WORK/cli.slp" && cp "$RUN/mix.wav" "$WORK/cli.wav" || fail "the headless run wrote no song or no mix"

# ── 2. the same script through a live window, over its control socket ──
fresh
( cd "$RUN" && exec "$SOLARIS" --control control.sock ) > "$WORK/app.log" 2>&1 &
APP_PID=$!
for _ in $(seq 1 100); do
    [[ -S "$RUN/control.sock" ]] && break
    if ! kill -0 "$APP_PID" 2>/dev/null; then
        if grep -qiE "cannot open display|failed to open display|cannot connect to.*display" "$WORK/app.log"; then
            APP_PID=""
            skip "DISPLAY=${DISPLAY:-} is set but no window can open there ($(head -1 "$WORK/app.log"))"
        fi
        APP_PID=""
        fail "the window exited before opening its control socket (see $WORK/app.log)"
    fi
    sleep 0.1
done
[[ -S "$RUN/control.sock" ]] || fail "the window never opened its control socket (see $WORK/app.log)"

( cd "$RUN" && "$CC" attach control.sock --script ../script.txt --timeout 90 > ../gui.out 2> ../gui.err ) \
    || fail "attach reported a refusal or a timeout: $(tail -3 "$WORK/gui.err")"
kill -0 "$APP_PID" 2>/dev/null || fail "the window died during the run (see $WORK/app.log)"
cp "$RUN/song.slp" "$WORK/gui.slp" && cp "$RUN/mix.wav" "$WORK/gui.wav" || fail "the window wrote no song or no mix"
kill "$APP_PID" 2>/dev/null
wait "$APP_PID" 2>/dev/null
APP_PID=""

# ── 3. the event stream: a silent no-op must not read as success ──
for want in \
    "\[evt\] project.opened path=song.slp" \
    "\[evt\] project.changed what=clip.added node=ac_1" \
    "\[evt\] project.changed what=note.added node=pt_2" \
    "\[evt\] project.changed what=send.added node=sd_1" \
    "\[evt\] params.changed address=dv_2.filter.cutoff value=900" \
    "\[evt\] project.changed what=redo" \
    "\[evt\] project.saved path=song.slp" \
    "\[evt\] render.finished out=mix.wav frames="
do
    grep -qE "$want" "$WORK/gui.err" || fail "the window's event stream is missing: $want"
done
grep -q "command.rejected" "$WORK/cli.err" "$WORK/gui.err" && fail "a line was refused (see $WORK/cli.err, $WORK/gui.err)"

# ── 4. one answer: the same events, outputs, stable state, song and mix ──
same() {   # same <what> <cli file> <gui file>
    if ! cmp -s "$2" "$3"; then
        echo "FAIL: the two faces disagree on $1 (R-SVC-6):" >&2
        diff -u "$2" "$3" | head -40 >&2
        fail "$1 differs"
    fi
}
same "the event stream" "$WORK/cli.err" "$WORK/gui.err"
awk '/^\{$/{on=1} on' "$WORK/cli.out" > "$WORK/cli.state.json"
awk '/^\{$/{on=1} on' "$WORK/gui.out" > "$WORK/gui.state.json"
[[ -s "$WORK/gui.state.json" ]] || fail "no state dump came back from the window"
same "state print --json --stable" "$WORK/cli.state.json" "$WORK/gui.state.json"
same "every command's output" "$WORK/cli.out" "$WORK/gui.out"
same "the saved song" "$WORK/cli.slp" "$WORK/gui.slp"
same "the rendered mix" "$WORK/cli.wav" "$WORK/gui.wav"

EVENTS=$(grep -c '^\[evt\]' "$WORK/gui.err")
LINES=$(wc -l < "$WORK/script.txt")
echo "PASS: $LINES lines drove a headless service and a live window; $EVENTS events, the stable state,"
echo "      every output, the song and the mix identical (R-SVC-6)"
