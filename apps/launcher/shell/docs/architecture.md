# arstro-shell — architecture

```
   host        SDL3 window · GPU · input · processes · sockets · files          knows everything
   ─────────────────────────────── seam ───────────────────────────────
   front ends  arstro-shell (host/) · arstro-shell-cc · control socket · tests  peers, not layers
   ─────────────────────────────── seam ───────────────────────────────
   service     core/  Command in · Model + Event out                            THE shell
   ─────────────────────────────── seam: protocol/ ─────────────────────
   apps        separate processes: an Artboard tree + sdk/ + kit/
```

## Why it is shaped this way

**The shell owns the screen and is not a display server.** Only Arstro apps run (R-SCOPE-2), so
there is no Wayland to speak. An app draws into shared memory and commits; the shell copies that
into a texture. System gestures therefore need no bridge — every touch reaches the shell first —
and a transition is the shell moving textures it already has, whether the app is paused, slow or
gone.

**Two-stage drawing.** Artboard draws through Cairo, on the CPU. That is fine for a surface that
changes occasionally and too slow for a full-screen surface 60 times a second on a small board. So
each shell surface is drawn into a texture only when it changes, and the GPU composites textures
every frame (R-PERF-1). Motion lives in the compositor, content in Artboard.

**The scene is separate from both.** `Scene` is pure maths — springs in, poses out — with no SDL
and no Cairo, so the motion law is measured by a unit test rather than watched.

**Platform seams, two of them.** SDL3 (window, GPU, input) and `protocol/Channel` (socket, shared
memory). A Windows port replaces `Channel.cpp` and the fork/exec/reap in `ShellHost`.

## Threading

One thread per process. The shell's loop: read the app and control sockets → reap → play scripted
touches → advance views and scene → upload committed app frames → redraw stale layers → composite
→ present → send `FrameDone`. The service is not re-entrant: a command born inside an event
handler is deferred (`ShellHost::dispatch`).

## A frame of an app, end to end

```
finger ─▶ SDL ─▶ ShellHost::touch ─▶ service `touch` (Navigator: claim?) ─┬─ claimed ─▶ gesture in Model ─▶ Scene
                                                                          └─ not ─▶ Touch ─▶ app (sdk: GestureRecognizer ─▶ root)
app: advance · layout · render (Cairo ─▶ shm) ─▶ Commit ─▶ shell copies to texture ─▶ composite ─▶ present ─▶ FrameDone ─▶ app may draw again
```

## Module map

| file | layer | what | requirement |
|---|---|---|---|
| `core/Model.h` | service | the observable model | R-CORE-1 |
| `core/Command.{h,cpp}` | service | command, event and setting tables; the text codec | R-CORE-2, R-CORE-3, R-SET-1 |
| `core/Navigator.{h,cpp}` | service | system gestures | R-GEST-1..6 |
| `core/ShellService.{h,cpp}` | service | the application; the API document | R-CORE-1..6, R-NAV-1..5 |
| `core/AppRegistry.{h,cpp}` | host-side helper | manifests, bundle roots | R-PKG-1, R-PKG-2 |
| `protocol/Wire.h` | seam | the app protocol's messages | R-APP-1..5 |
| `protocol/Channel.{h,cpp}` | seam | framing, descriptor passing, shared memory | R-APP-6 |
| `kit/Theme.h` | kit | tokens (aliases cosmo's) | R-UI-1, R-UI-2 |
| `kit/Draw.h` · `kit/IconTypes.h` | kit | rounded rect, measured text, icons | R-UI-1 |
| `kit/Widgets.h` | kit | touch controls | R-UI-3, R-SDK-4 |
| `kit/Fonts.{h,cpp}` | kit | the embedded typeface | R-UI-4 |
| `kit/icons-src/` · `kit/icons-svg-to-header.py` | kit | icon sources and generator | R-SDK-4 |
| `sdk/App.{h,cpp}` | sdk | the app interface and run loop | R-SDK-1..3 |
| `host/Scene.{h,cpp}` | front end | poses and springs | R-ANIM-1..5, R-NAV-3 |
| `host/Views.{h,cpp}` | front end | home, status bar, shade | R-HOME, R-BAR, R-SHADE |
| `host/ShellHost.{h,cpp}` | front end | compositor, input routing, app server, control socket | R-HOST-1..8, R-PERF-1..2 |
| `host/main.cpp` | host | window or headless; the script runner | R-HOST-2, R-CORE-5 |
| `cli/main.cpp` | front end | `arstro-shell-cc` | R-CORE-4, R-CORE-5 |
| `pm/main.cpp` | tool | `arstro-pm` | R-PKG-3..5 |
| `apps/clock/` | app | the example app | R-SDK-1 |
| `apps/settings/main.cpp` | app | Settings: pages, navigation, the event parser | R-SET-2 |
| `apps/settings/Controls.h` | app | Card, Block, Segmented, Swatches, Note | R-SET-2, R-UI-3 |
| `tests/coreTests.cpp` | test | L2: the service by text lines; API drift | R-CORE, R-GEST, R-NAV |
| `tests/sceneTests.cpp` | test | the motion law, measured | R-ANIM |
| `tests/live.sh` · `tests/pm.sh` | test | L5 headless with real apps; the package round trip | R-APP, R-PKG |

The class diagram is [`architecture.puml`](architecture.puml).
