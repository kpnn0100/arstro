# arstro-shell — developing

Status is in [`PROGRESS.md`](PROGRESS.md). If this file and the ledger disagree, the ledger wins
and this file is the bug.

## Build and run

```sh
cmake -S . -B build && cmake --build build --target arstro-shell arstro-shell-cc arstro-pm -j
B=build/apps/launcher/shell

$B/arstro-shell --apps $B/bundles                          # a 1280x800 window; the mouse is a finger
$B/arstro-shell --display 1 --fullscreen --apps $B/bundles # cover the second display
$B/arstro-shell --size 800x480 --apps $B/bundles           # a 5" panel's pixels
$B/arstro-shell-cc api                                     # the grammar, events, settings, thresholds
```

Needs SDL3, Cairo and FreeType. Without SDL3 the host is skipped and everything else still builds.

## Drive it without touching it

```sh
$B/arstro-shell --control $XDG_RUNTIME_DIR/arstro-shell.sock --apps $B/bundles &
$B/arstro-shell-cc send $XDG_RUNTIME_DIR/arstro-shell.sock app launch arstro.clock
$B/arstro-shell-cc send $XDG_RUNTIME_DIR/arstro-shell.sock swipe 480 536 480 300 120   # dp
$B/arstro-shell-cc send $XDG_RUNTIME_DIR/arstro-shell.sock scene                       # live eased values
$B/arstro-shell-cc send $XDG_RUNTIME_DIR/arstro-shell.sock shot /tmp/frame.png
```

Headless, on a virtual clock, with the real app processes — what the tests use:

```sh
printf 'clock 09:41 Friday, 27 November\ntap 428 422\nwait 120\nshot mid.png\nwait 1200\nshot app.png\nstate\n' > s.script
$B/arstro-shell --headless --size 1920x1080 --script s.script --apps $B/bundles
```

Script and control lines are any service command plus the host verbs `state`, `scene`,
`tap x y`, `swipe x0 y0 x1 y1 ms`, `clock HH:MM date…`, `shot file.png`, `quit`; a script also has
`wait ms`. Coordinates are dp: a 1920×1080 output is 960×540 dp.

## Test

```sh
ctest --test-dir build -R arstro_shell --output-on-failure
```

`arstro_shell_core` (the service by text lines) · `arstro_shell_api_doc` (the committed API
document equals the generated one — after changing a table, regenerate with
`arstro-shell-cc api > docs/API.md; arstro-shell-cc api --json > docs/api.json`) ·
`arstro_shell_scene` (the motion law, measured) · `arstro_shell_live` (headless host, real apps) ·
`arstro_shell_pm` (pack, install, update, remove).

## Write an app

Copy `apps/clock/`: a `manifest.arstro`, one `App` subclass, `arstro::app::run`. Link `arstro_app`.
Sizes are dp; keep controls out of `ctx.insetTop` / `ctx.insetBottom`; return `true` from `back()`
only if you used it; call `ctx.setContinuous(true)` while something animates on its own. Draw with
`kit/` so the app is one of the family.

```sh
arstro-pm pack <bundle-dir>            # -> <id>-<version>.arstro
arstro-pm install <file.arstro>        # into ~/.local/share/arstro/apps
```

## Which document decides what

| question | the file that decides |
|---|---|
| What was asked for, and why? | `REQUIREMENTS.md` |
| What does the code do today? | `docs/requirements.md` |
| What is done, and what is next? | `docs/PROGRESS.md` |
| Which commands, events, settings and thresholds exist? | `docs/API.md` (generated — never edit) |
| Where does a behaviour belong? Which file is which? | `docs/architecture.md` |
| How should it look and move? | `arstro.rule.design`, `arstro.rule.animation`; the values in `kit/Theme.h` |
| How do I build, run, drive and test it? | this file |
