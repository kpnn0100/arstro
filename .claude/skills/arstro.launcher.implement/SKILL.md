---
name: arstro.launcher.implement
description: Use to implement or resume ANY work on the Arstro touch stack (apps/launcher/shell) — the shell that owns the screen (arstro-shell: service, system gestures, scene and compositor, home, status bar, shade, recents), the app protocol and SDK, the shared touch kit, arstro-pm and packages, and the stack's apps (Settings, Clock). Resume-driven from its committed ledger; verifies headlessly with real app processes and by looking at frames. Invoke for "continue the launcher", "add a gesture", "add a shell command", "the shade/recents/home does X", "write a touch app", "/arstro.launcher.implement". Replaces the withdrawn GNOME/Plasma Android theme.
---
# arstro.launcher.implement

> **Invoke `arstro.rule` first**, and `arstro.rule.design` + `arstro.rule.animation` before any
> pixel or motion. Follow all of them; where they overlap, this file's checklist is the one to
> satisfy — except on the architecture, requirement and agent-drivability laws, where
> `arstro.rule` wins.

The workflow for **the Arstro touch stack** — `apps/launcher/shell/`: a shell that owns the screen
(`arstro-shell`, SDL3), the SDK apps are written against (`arstro_app`), the shared touch kit
(`arstro_kit`), the package tool (`arstro-pm`) and the apps (`apps/`).

**(REPLACED 2026-11-27.** This skill used to drive the Android theme for GNOME/Plasma
(`apps/launcher/android-shell`, plan `apps/launcher/docs/android-theme-plan.md`, milestones M0–M9).
That plan is withdrawn — the founder: *"this will replace gnome/plasma version"*, *"only arstro
app"*. Do not resume it. The post-mortem is in `apps/launcher/shell/REQUIREMENTS.md`, *Withdrawn*.**)**

## Orient — every invocation

1. `apps/launcher/shell/docs/PROGRESS.md` — **NEXT**, the milestone list, the decisions log, the
   honesty ledger. Reconcile it to the repo first if they disagree, and say so.
2. The requirement that governs the task: `apps/launcher/shell/REQUIREMENTS.md` (intent, `R-`) and
   `docs/requirements.md` (as built, `DR-`). No requirement → write it before the code. The ledger
   lists areas that have **none yet** (keyboard, lock screen, power): those start with requirements.
3. `docs/DEVELOPING.md` — build, run, drive, test, and the table of which document decides what.
4. `docs/API.md` — the grammar. Build scripts from it, not from memory.

## Where a change belongs

| the change | goes in | never in |
|---|---|---|
| what a gesture means, who is in front, a setting's legality | `core/` (service) | a view or the host |
| where things are this frame, how they travel | `host/Scene` (pure, springs) | `ShellHost::composite` |
| what a shell surface shows | `host/Views` (Artboard trees) | the compositor |
| drawing textures at poses, processes, sockets, files | `host/ShellHost` | the service |
| a control an app could also use, a token, an icon | `kit/` | one app or one view |
| a message between shell and app | `protocol/Wire.h` + both ends | either end alone |
| a new Artboard primitive or a fix to its input model | `core/Artboard` via `arstro.artboard.implement` | a workaround here (the ledger lists AB-7…AB-10) |

**Add a command:** a `CommandKind`, one row in `commandSpecs()`, its `case` in
`ShellService::dispatch`, a line in the round-trip list in `tests/coreTests.cpp`, then regenerate
the API document. **Add a setting:** one row in `settingSpecs()`. **Add an event:** one row in
`eventSpecs()` and the `emit`.

## Verify — in this order, lowest level that proves it

```sh
cmake --build build --target arstro-shell arstro-shell-cc arstro-pm -j
ctest --test-dir build -R arstro_shell --output-on-failure
```

- **The service** — `tests/coreTests.cpp`: text lines in, events and dump out. A new behaviour gets
  a check that fails without it.
- **Motion** — `tests/sceneTests.cpp`: step one frame at a time and assert on LIVE values (a value
  mid-flight differs from its target; a finger moves it exactly; opacity never steps more than 0.06
  in 8 ms; a leaving card is still moving while it fades). Never settle and then look.
- **The real host with real apps, no display** — `tests/live.sh`, or by hand:
  `arstro-shell --headless --size 1920x1080 --script s.script --apps build/apps/launcher/shell/bundles`
  with `tap`, `swipe`, `wait`, `shot f.png`, `state`, `scene` (coordinates in dp).
- **Look.** Read the PNGs: every state, **mid-transition and at rest**, at 1920×1080 **and**
  800×480 **and** portrait 480×800. A design change is not done until a frame has been seen.
- **Live** — on a screen: `arstro-shell --display N --fullscreen --control $XDG_RUNTIME_DIR/arstro-shell.sock`,
  then `arstro-shell-cc send <sock> …`. The control socket path must be under 108 bytes. The desktop
  at `:1` is the founder's; put a window there only when asked.
- After changing a command, event or setting table:
  `arstro-shell-cc api > docs/API.md; arstro-shell-cc api --json > docs/api.json`.

## Rules this unit adds

- **The shell is not a display server** (R-SCOPE-2). Do not add Wayland, X11 or GTK to it.
- **A transition may not redraw the surfaces it moves** (R-PERF-1): motion is the compositor moving
  textures; a view is redrawn only when its content changes. Check `busy()` before adding anything
  that animates inside a full-screen layer.
- **Anything a finger can interrupt is a spring**, and during a gesture the value is set straight
  from the finger. A fixed-length tween is for a control's own state (a switch, a press).
- **Platform code lives in two places**: SDL in `host/`, and `protocol/Channel.cpp`. A `#include`
  of a POSIX or SDL header anywhere else is a layering bug.
- **Claims about a Pi are measurements or they are not made.** The ledger's honesty section says
  what has only ever run on a desktop GPU with a mouse.

## Commit

One focused commit per task on `main`, with the requirement, as-built entry, module-map row,
diagram, API document and ledger (**NEXT** rewritten) in the same commit. Then pull, re-test and
push as `arstro.rule` §7 says — unless the index holds another session's staged work, in which
case commit only this unit's paths, do not rebase, and say so.
