#!/bin/sh
# arstro-pm — the package round trip: pack, inspect, install, update, list, remove, and the
# refusals (R-PKG-3..5).   pm.sh <arstro-pm> <bundles-dir> <work-dir>
set -eu
PM=$1; BUNDLES=$2; WORK=$3
rm -rf "$WORK"; mkdir -p "$WORK"; cd "$WORK"
fail() { echo "FAIL: $1"; exit 1; }
ROOT="$WORK/root"

"$PM" pack "$BUNDLES/arstro.clock" -o clock.arstro | grep -q "Packed arstro.clock 1.0.0" || fail "pack"
"$PM" info clock.arstro | grep -q "^arstro.clock " || fail "info on a package"
"$PM" install clock.arstro --root "$ROOT" | grep -q "^Installed arstro.clock" || fail "install"
[ -x "$ROOT/arstro.clock/bin/arstro-clock" ] || fail "the program was not installed"
"$PM" install clock.arstro --root "$ROOT" | grep -q "^Updated arstro.clock" || fail "a second install is an update"
"$PM" list --root "$ROOT" | grep -q "^arstro.clock .*Clock" || fail "list"

# Not a package: refused, and nothing is left behind — no staging directory, no half an app.
echo junk > bad.arstro
"$PM" install bad.arstro --root "$ROOT" 2>/dev/null && fail "junk must be refused"
[ "$(ls -A "$ROOT" | tr '\n' ' ')" = "arstro.clock " ] || fail "a refused install left files behind: $(ls -A "$ROOT")"

# A manifest that points its program outside the bundle is refused.
mkdir -p evil && printf 'id = evil.app\nname = Evil\nexec = /bin/sh\n' > evil/manifest.arstro
"$PM" install evil --root "$ROOT" 2>err.txt && fail "a program outside the bundle must be refused"
grep -q "outside the bundle" err.txt || fail "the refusal must say why"
# So is an unknown manifest key, by name.
mkdir -p typo/bin && cp "$BUNDLES/arstro.clock/bin/arstro-clock" typo/bin/x
printf 'id = typo.app\nname = Typo\nexec = bin/x\nicno = a.png\n' > typo/manifest.arstro
"$PM" install typo --root "$ROOT" 2>err.txt && fail "an unknown key must be refused"
grep -q "icno" err.txt || fail "the refusal must name the key"

"$PM" remove arstro.clock --root "$ROOT" | grep -q "^Removed arstro.clock" || fail "remove"
"$PM" remove arstro.clock --root "$ROOT" 2>/dev/null && fail "removing twice must fail"
"$PM" list --root "$ROOT" | grep -q "No apps installed" || fail "the empty list must say so"
echo "ok: arstro-pm"
