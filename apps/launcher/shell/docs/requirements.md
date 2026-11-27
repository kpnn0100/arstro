# arstro-shell — requirements (as-built tier)

What the code contractually does **today**. Descriptive, present tense. An entry is current or it
is a bug. Each cites the intent it serves in [`../REQUIREMENTS.md`](../REQUIREMENTS.md). Anchors
are a file and a symbol, so they survive an edit above them.

## Service

- **DR-CORE-1** (R-CORE-1) `ShellService` holds a `Model` of plain data and a `Navigator`. It has
  no include of SDL, Cairo or Artboard, reads no file and forks nothing: `task.spawn`, `task.kill`
  and `task.back` are events the host acts on. Installed apps arrive through an injected `Scanner`.
  — `core/ShellService.h`
- **DR-CORE-2** (R-CORE-2) `commandSpecs()` is one table of 19 kinds, each with a name, an
  `ArgShape` and a grammar line. `parseCommand` matches the longest name, then one parser per
  shape; `formatCommand` is its inverse. An unknown command, an extra argument, an unknown `set`
  key, an out-of-range number and an unknown notification field are each rejected naming the
  offending text. `Set` validates every pair, applies them with event sinks held, and restores the
  previous values if any pair fails. — `core/Command.cpp`, `ShellService::dispatch`
- **DR-CORE-3** (R-CORE-3) `ShellService::reject` writes `lastError`, bumps the revision and emits
  `error message=…`. `lastError` is cleared by the next command that is not a `touch`.
- **DR-CORE-4** (R-CORE-4) `apiMarkdown()` / `apiJson()` are built from `commandSpecs`,
  `eventSpecs`, `settingSpecs` and the `gesture::` constants. `arstro_shell_api_doc` (ctest)
  compares `docs/API.md` and `docs/api.json` byte for byte. — `core/ShellService.cpp`
- **DR-CORE-5** (R-CORE-5) Three ways in with no display: `arstro-shell-cc run <script|->` prints
  each event as `[evt] …` and the final dump; `arstro-shell --headless --size WxH --script F` runs
  the real host on a virtual 60 Hz clock; `--control <path>` on a running shell takes the same
  lines, replies `ok` or `error: …`, and after `events` streams `[evt]` lines. `arstro-shell-cc
  send <socket> <line…>` is the client. A socket path must fit `sockaddr_un` (107 bytes).
- **DR-CORE-6** (R-CORE-6) `ShellService::dump` prints `screen`, `nav`, `shade`, `app`, `task`,
  `notification`, `setting`, `error` lines; `revision` and `gesture` are never printed.

## Gestures

- **DR-GEST-1** (R-GEST-1, R-GEST-2) `Navigator::feed` records one candidate per press from the
  zone it starts in (`gesture::kBottomZone` 24, `kSideZone` 20, `kTopZone` 32, `kShadeCloseZone`
  72 dp). It claims on travel past `kSlop` 10 dp in the gesture's direction when that direction is
  the larger component. `Feed{claimedNow, consumed, ended, result}` is what the host reads through
  `ShellService::lastTouch()`. — `core/Navigator.cpp`
- **DR-GEST-2** (R-GEST-3) `Navigator::decide`, Home: under `kMinTravel` 28 dp → cancelled; from
  Home → recents; from Recents → home; from an app, upward speed over `kFlingSpeed` 700 dp/s or
  travel over `kHomeFraction` 0.45 of the height → home, over `kRecentsFraction` 0.12 → recents.
  Speed is measured over the last `kVelocityWindowMs` 90 ms, so a finger that stops before lifting
  reads as zero.
- **DR-GEST-3** (R-GEST-4) Back commits at `kBackCommit` 56 dp of sideways travel.
  `ShellService::back` closes the shade, else leaves recents, else emits `task.back` — or goes home
  directly when the task has not attached. `task back-unhandled <n>` is what sends it home.
- **DR-GEST-4** (R-GEST-5) With `shadeOpen` the only candidate is `ShadeClose`. The shade commits
  at 0.4 of its travel (0.6 of the height) or on a fling over 500 dp/s.
- **DR-GEST-5** (R-GEST-6) While a gesture is active `Model::gesture` carries `dx`, `dy`,
  `progress`, `edge` and `hint`. The model's gesture is cleared **before** the result is applied.

## Tasks

- **DR-NAV-1** (R-NAV-1, R-NAV-2) `app launch` focuses an existing task of that app or creates one
  (`Starting`). `ShellService::syncTaskStates` is the one rule: `Running` iff attached, focused and
  `nav == App`; attached otherwise → `Paused`. The focused task is rotated to the front of
  `Model::tasks`.
- **DR-NAV-2** (R-NAV-5) `ShellHost::reap` maps a reaped pid to its task and dispatches
  `task exited`; exit status 127 first posts "*name* could not start". A client that hangs up is
  treated as exited and its process is sent `SIGTERM`. Closing the focused card in recents keeps
  recents open while another task remains.

## Scene and motion

- **DR-ANIM-1** (R-ANIM-2) `Scene` holds critically damped `artboard::Spring`s for zoom, home,
  shade, scroll and, per card, presence, slot and dismiss, advanced at `motion::springNav()` 14
  rad/s (spatial) and `motion::springFade()` 20 rad/s (opacity). While `Model::gesture` is active
  it `reset`s the driven springs to a value computed from the travel. — `host/Scene.cpp`
- **DR-ANIM-2** (R-ANIM-3, R-ANIM-5) A card's row rectangle is the screen scaled by
  `lerp(1, kRecentsScale 0.62, zoom)`. With `toIcon` (set when the front changes Home↔App) the
  rectangle is interpolated to the app's home icon and `iconMix` rises; the compositor draws the
  app's texture at one scale, cropped (`ShellHost::composite`), and cross-fades it to the icon over
  `iconMix` 0.45–0.85.
- **DR-ANIM-3** (R-ANIM-4) Toward the icon a card's alpha is `clamp(presence / 0.85)`. A card whose
  task is gone becomes `dying`, keeps moving, and is erased under presence 0.004; its textures are
  released 30 frames after it leaves the pose list.
- **DR-ANIM-4** (R-ANIM-1) The display scale is a spring (`ShellHost::frame`); while it moves the
  dp screen is re-derived every frame and apps are re-configured. Brightness is an eased dim.
- **DR-ANIM-5** (R-ANIM-6) `setting.changed` calls `artboard::setReducedMotion` in the shell and
  re-sends `Configure` to every app, where the SDK calls it too.

## Host

- **DR-HOST-1** (R-PERF-1, R-PERF-2) Home, status bar and shade are each an Artboard tree drawn
  through `CairoTarget` into a `Layer` (a Cairo surface and an SDL streaming texture). A layer is
  redrawn when it is marked stale or its view is `busy()` (700 ms after the last touch or change,
  or while its scroller or list is moving), and only while it is visible.
- **DR-HOST-2** (R-HOST-7) `ShellHost::quad` is the only compositing primitive. With a renderer it
  emits a triangle fan plus a one-pixel feather ring (premultiplied blending); with none it draws
  the same quad with Cairo into `canvas()`. SDL's software renderer is not used.
- **DR-HOST-3** (R-HOST-3) Pixels per dp = `clamp(min(w,h) / 540, 1, 3)` × the `scale` setting,
  or `--scale` × the setting.
- **DR-HOST-4** (R-HOST-4) Settings are `key=value` lines in `~/.config/arstro/shell.conf`
  (`--config`), written to a `.tmp` and renamed. Headless persists nothing unless `--config` is
  given. A line that no longer parses is skipped.
- **DR-HOST-5** (R-HOST-5, R-HOST-6) `tap` and `swipe` queue samples that `frame()` feeds to
  `ShellHost::touch`. `scene` prints zoom, home, shade, scroll and every card pose.
- **DR-HOST-6** (R-HOST-8) Left mouse button = one finger (SDL also synthesises these from a real
  touch). Esc back · Home/F1 home · F2 recents · F3 shade · Ctrl+Q quit.
- **DR-HOST-7** Touch routing: every sample goes to the service first; an unclaimed press is
  captured by the shade (if open), Home, the recents row, or the focused app, by state. A claim
  cancels the captured target — an Up far off screen for a view, `TouchPhase::Cancel` for an app.

## Apps

- **DR-APP-1** (R-APP-2) `protocol/Wire.h`: `Hello`, `Buffer` (+ one descriptor), `Commit`,
  `BackResult`, `Request`, `Notify`, `Finish` from the app; `Configure`, `FrameDone`, `Touch`,
  `Lifecycle`, `Back`, `ShellEvent` from the shell. Pixels are premultiplied ARGB32, native order.
  The shell copies a committed buffer on its next frame and sends `FrameDone` after presenting.
- **DR-APP-2** (R-APP-4) `Hello` is refused unless its task exists, its app id matches and no
  client already holds the task. A `Buffer` is mapped only if the file is at least `stride × height`.
- **DR-APP-3** (R-APP-5) `Request` is dispatched only for a client whose `AppInfo::system` is true;
  such a client receives every event except `gesture.*`, and the current settings on attach.
- **DR-APP-4** (R-APP-6) `protocol/Channel.cpp` is the only file with `socket`, `sendmsg`,
  `SCM_RIGHTS` or `memfd_create`.
- **DR-SDK-1** (R-SDK-1..3) `arstro::app::run` (`sdk/App.cpp`): connects to `$ARSTRO_SHELL_SOCKET`
  as `$ARSTRO_TASK`, registers the fonts, sizes the root, feeds an `artboard::GestureRecognizer`
  (drag threshold 8 dp, no double-click), and draws when resumed, not awaiting `FrameDone`, and
  either continuous or within 700 ms of the last input, event or `invalidate()`.
- **DR-SDK-2** (R-SDK-4) `kit/`: `Theme.h` aliases `cosmo_v2::palette` and `font` and declares the
  touch metrics; `Widgets.h` has `Pressable`, `Tile`, `TouchSlider`, `ListRow`, `TouchScroll`,
  `IconButton`, `TextButton`. `TouchScroll` keeps its content out of the child list so a vertical
  drag that starts on a row becomes a scroll (Artboard does not bubble a captured drag — AB-7).

## Packages

- **DR-PKG-1** (R-PKG-1, R-PKG-2) `parseManifest` / `scanApps` / `defaultAppRoots` in
  `core/AppRegistry.cpp`. Roots: `$ARSTRO_APPS`, else `~/.local/share/arstro/apps` then
  `/opt/arstro/apps`. A broken bundle is skipped and reported.
- **DR-PKG-2** (R-PKG-3..5) `pm/main.cpp`: `pack` (`tar -czf`), `install` (stage directory beside
  the destination → `readManifest` → rename; the old bundle is put back if the rename fails),
  `remove`, `list`, `info`. `tar` and `cp` are run with an argument vector, never through a shell.
  `readManifest` refuses a program or icon outside the bundle and a program that is not executable.

## Not built (the intent exists, the code does not)

R-GEST-7 multi-touch · R-NAV-6 memory pressure · R-SDK-5 standalone window · R-PKG-6 signing and
confinement · R-PERF-3 the Pi measurement · R-PERF-4 present-on-change · R-PERF-5 GPU app drawing ·
R-SET-3 hardware back ends.
