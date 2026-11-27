# arstro-shell — progress ledger

**The only authority on status.** Read **NEXT** first; update this file in the same commit as the
work. Legend: `[ ]` not started · `[~]` in progress · `[x]` done and verified · `[!]` done but
UNVERIFIED (the note says exactly what is unverified and why).

Conformance rung (`arstro.rule` §5): **4** — generated API document, committed, drift-tested; a
control socket exists. Rung 5 (the equivalence test: one script through the headless service and
through a live window, dumps compared) is task T2.1.

## ► NEXT

**T1.1 — run it on a real board with a real touch panel** (Orange Pi or Raspberry Pi, SDL3's
KMSDRM driver, no desktop underneath) and measure frame times during a launch, a go-home and a
shade pull. Everything so far was driven by a mouse on a desktop GPU; R-PERF-3 is a goal until this
number exists, and it decides whether T3.1 (GPU drawing for apps) is urgent.

Last updated: 2026-11-27 · first commit of the unit.

## Milestones

### T0 — the stack exists, end to end — DONE
- [x] T0.1 Service: model, 19 commands, 15 events, 10 settings, one text codec, rejection by name — `arstro_shell_core` (138 checks)
- [x] T0.2 System gestures in the service: home / recents / back / shade, claim-after-slop — same suite
- [x] T0.3 API document generated and drift-tested — `arstro_shell_api_doc`
- [x] T0.4 App protocol (socket + shared memory), SDK (`arstro::app::run`), kit (tokens, fonts, icons, touch controls)
- [x] T0.5 Scene: spring poses, finger-follows-exactly, icon launch/return, recents row, flick to close — `arstro_shell_scene` (632 checks; it found the opacity pop recorded in the decisions log)
- [x] T0.6 Host: SDL3 GPU compositor + Cairo headless compositor, home, status bar, shade, app server, process spawn/reap, control socket, script runner
- [x] T0.7 Headless host with real app processes — `arstro_shell_live`
- [x] T0.8 `arstro-pm` pack / install / update / remove / list / info and its refusals — `arstro_shell_pm`
- [x] T0.9 Apps: Clock (the example), Settings
- [x] T0.10 Looked at: home, shade, recents, launch and go-home mid-transition at 1920×1080, 1280×800, 800×480 and 480×800 (headless PNGs); run full-screen on a second desktop monitor with the GPU renderer and driven by mouse
- [!] T0.11 Touch hardware — **UNVERIFIED**: no touch panel on the development machine. SDL turns a touch into the mouse events the host handles, but no real finger has driven it.
- [!] T0.12 Small-board performance — **UNVERIFIED**: never run on a Pi-class board (→ T1.1).

### T1 — a device boots into it
- [ ] T1.1 Run on a board under KMSDRM with a touch panel; measure; record the numbers here ← **NEXT**
- [ ] T1.2 A session: autologin → `arstro-shell` (systemd unit, no display manager); restart on crash
- [ ] T1.3 Install rules: `arstro-shell`, `arstro-pm`, the system apps into `/opt/arstro/apps`
- [ ] T1.4 Present only when something changed (R-PERF-4)
- [ ] T1.5 Rotation and portrait panels that report landscape

### T2 — the platform apps need
- [ ] T2.1 The equivalence test (rung 5): one script headless and through a live window, dumps diffed
- [ ] T2.2 On-screen keyboard and text input — **requirements first**, none written yet
- [ ] T2.3 Multi-touch to apps: pinch, rotate (R-GEST-7); needs Artboard multi-pointer input
- [ ] T2.4 Hardware back ends behind one interface: backlight, PipeWire volume, NetworkManager, BlueZ, battery (R-SET-3)
- [ ] T2.5 Standalone window for an app started without a shell (R-SDK-5)
- [ ] T2.6 Memory pressure: stop the least recently used paused task, restore from saved state (R-NAV-6)
- [ ] T2.7 Lock screen and power — requirements first
- [ ] T2.8 "Clear all" in recents; home pages when the apps do not fit one screen
- [ ] T2.9 The app's clock in a headless run: the shell steps a virtual clock but the SDK runs on real time and ignores `FrameDone.timeMs`, so a scripted `wait 300` is not 300 ms of the app's animation and an app's mid-transition frame is not at a known time. Drive the SDK's clock from `FrameDone.timeMs`.
- [ ] T2.10 An accent change reaches an app in one frame: `sdk/App.cpp` calls `palette::setAccent` on `Configure`. Settings eases it itself; the SDK should ease it for every app.
- [ ] T2.11 `kit::TouchScroll::render` ignores its own `opacity` (it only tests faded-out). Fading one directly would pop; today a parent is faded instead.
- [ ] T2.12 Reduced motion: Artboard collapses every tween to instant, but `arstro.rule.animation` asks for ~150 ms fades to remain. Never switched on in a run — look at it, then decide where the fade lives.
- [ ] T2.13 Settings: the top page's one-line summaries and the device name swap text in one frame when changed from outside; cross-fade them. Editing the device name waits on the keyboard (T2.2).

### T3 — speed and safety
- [ ] T3.1 Artboard GLES render target, so an app's own drawing is on the GPU (R-PERF-5) — an `arstro.artboard.implement` cycle
- [ ] T3.2 Damage rectangles in the app protocol; dma-buf instead of a copy
- [ ] T3.3 Package signing and app confinement (R-PKG-6)
- [ ] T3.4 The Windows port: `protocol/Channel` on a named pipe and file mapping; process start in `ShellHost`

### Artboard gaps found (each is an `arstro.artboard.implement` cycle, not a patch here)
- [ ] AB-7 A captured drag does not bubble to a scroll container, so a list of tappable rows in `ScrollView` cannot be scrolled by touching a row. Worked around by `kit::TouchScroll` keeping its content out of the child list. `Segment::dispatchGesture` is private and non-virtual, so the workaround cannot be a subclass of `ScrollView`.
- [ ] AB-8 `GestureRecognizer` has no cancel. A cancelled touch is fed as an Up far off screen.
- [ ] AB-9 `Segment::renderOverlay` is not virtual; a container that owns content outside the child list forwards the overlay pass by hand.
- [ ] AB-10 One pointer only: `RawPointer` has no pointer id.

## Decisions log (newest first)

- **2026-11-27 — Headless composites with Cairo, not SDL's software renderer.** SDL's software
  `RenderGeometry` tore every textured rounded card (the fan's triangles sampled wrongly). One
  primitive, `ShellHost::quad`, now has two back ends. The service, scene, views, protocol and
  poses are shared, so a headless PNG shows the real layout and motion; what it cannot show is a
  GPU-driver fault.
- **2026-11-27 — The card's fade ramp is `presence / 0.85`, spring 14 rad/s.** The first ramp
  (`/ 0.6`, 15 rad/s) dropped 0.07 of alpha in one 8 ms frame where the spring is fastest — over
  the 0.06 limit in `arstro.rule.animation`. Found by `sceneTests`, not by eye.
- **2026-11-27 — Brightness default is 1.** At 0.8 the software dim greyed every screenshot.
- **2026-11-27 — No Wayland, no wlroots.** "Only arstro app" (the founder) makes a private app
  protocol sufficient: a socket, one shared buffer per app, commit / frame-done. It is ~250 lines,
  it gives the shell every touch first, and it is portable to Windows.
- **2026-11-27 — SDL3 is the host's platform layer.** It is already installed, has a KMSDRM driver
  for a board with no desktop, a GPU renderer everywhere, and Windows.
- **2026-11-27 — Tokens are aliased from cosmo; the fonts come from cosmo's generator.** The
  touch stack is one of the family (`arstro.rule.design` §2), with its own metrics only.
- **2026-11-27 — A manifest is `key = value`, not JSON.** The shell reads it on every boot of a
  small board and must not need a parser library to learn what is installed.
- **2026-11-27 — This unit replaces `android-shell`.** See REQUIREMENTS.md, *Withdrawn*. The old
  directory is left in place and still builds; deleting it is the founder's call.

## Verification notes (the honesty ledger)

- Driven by a **mouse** only. No touch panel, no multi-touch.
- Run on a **desktop GPU** only (X11, second monitor, 1920×1080). Never on KMSDRM, never on ARM.
- Settings was written by a helper agent and reviewed by frame (every page at 1920×1080, 800×480, 480×800, both transitions mid-flight). Not exercised: reduced motion on; an outside `set` arriving while a finger holds a slider.
- Wi-Fi, Bluetooth, volume and do-not-disturb change the model and the UI and **nothing else**;
  brightness is a dim drawn over the screen (R-SET-3 is open).
- `git pull --rebase` and `git push` were **not** run for the first commit: the index held another
  session's staged interstellar work, which a rebase would have had to disturb.
