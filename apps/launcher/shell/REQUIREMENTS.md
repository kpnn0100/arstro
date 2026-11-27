# arstro-shell — requirements (intent tier)

What was asked for, why, and what happened to it. Normative: *shall / must / may not*. Ids are
stable, never renumbered, never reused. What the code does today is in
[`docs/requirements.md`](docs/requirements.md) (`DR-` entries); status is in
[`docs/PROGRESS.md`](docs/PROGRESS.md) and nowhere else.

**The brief, in the founder's words (2026-11-27):**

> "I want to make touch screen UI for linux that will be set as default UI replicate from android UI
> using Artboard … i'm building whole new stack for arstro application, like i'm making package pack
> similar to android so my orange pi/raspberry pi/computer and all other linux device or later
> windows can have touch screen launcher, setting app, application on that stack, and application is
> touch friendly too, performance is still needed" · "gesture too" · "only arstro app, target is
> 5 inch to 10 inch, this will replace gnome/plasma version" · "the UI need to be clean, minimalism"
> · "the UI must be full of clean animation"

---

## R-SCOPE — What this is, and what it replaces

- **R-SCOPE-1** The touch stack is a shell that **owns the screen**, an **SDK** that Arstro apps are
  written against, a **package format**, and the **apps**. It is not a theme for another desktop.
  **It replaces the GNOME/Plasma `android-shell`** (`apps/launcher/android-shell`, plan
  `apps/launcher/docs/android-theme-plan.md`), whose requirement to be a guest on two desktops is
  withdrawn — see *Withdrawn* at the end.
- **R-SCOPE-2** Only Arstro apps run on it. The shell is therefore **not** a Wayland or X11
  compositor and must not grow into one: an app speaks the app protocol (R-APP) or it does not run.
  *Why:* this is what removes wlroots, libinput, GTK and a display server from a small board, and
  what lets the same shell run on Windows later.
- **R-SCOPE-3** Target screens are **5 to 10 inches**, landscape or portrait, from 800×480 upward.
  Every surface must be usable at 480 dp on the short side.
- **R-SCOPE-4** Target machines are Orange Pi / Raspberry Pi class boards, any Linux computer, and
  later Windows. Nothing above the two platform seams (R-HOST-2, R-APP-6) may name a platform.

## R-CORE — The service is the product

- **R-CORE-1** One `ShellService`: commands in, a plain-data `Model` and an `Event` stream out. It
  decides everything a second front end would otherwise decide again — which task is in front,
  what a gesture means, when a task is paused, whether a setting is legal. It draws nothing, opens
  no file and starts no process; when a process must start it says so with an event.
- **R-CORE-2** One text grammar, generated from one table, for every command. **An input the
  service does not understand is rejected by name** — never accepted quietly — and the rejection
  is both an `error` event and `lastError` in the model. A `set` line with several pairs applies
  all of them or none.
- **R-CORE-3** Every change is observable as an event line whose shape is an interface: fields are
  added at the end, never reordered or renamed.
- **R-CORE-4** `arstro-shell-cc api [--json]` prints the API document from the same tables the
  parser uses; `docs/API.md` and `docs/api.json` are committed and a test fails when they drift.
- **R-CORE-5** The whole shell is drivable with no display: `arstro-shell-cc run` (service only),
  `arstro-shell --headless --script` (the real host, real app processes, PNG frames), and a control
  socket on a running shell that takes the same lines.
- **R-CORE-6** A stable state dump: two dumps of the same state are identical. It leaves out only
  what is a property of *when* it was taken — the revision counter and a gesture in flight.

## R-GEST — System gestures

- **R-GEST-1** Gestures are decided in the service, in dp, from thresholds that are named once and
  published in the API document.
- **R-GEST-2** A press in an edge zone is only a *candidate*: it is still delivered to the app.
  The system **claims** the touch only after it has travelled past the slop in the gesture's own
  direction, with that direction dominant; the app is then sent a cancel. A tap on the edge, and a
  sideways drag along the bottom edge, stay the app's.
- **R-GEST-3** **Bottom edge, up:** a fast flick goes Home; a drag that is held and released goes
  to Recents; a drag past 45% of the height goes Home at any speed; a short nudge is cancelled.
  From Home it opens Recents; from Recents it goes Home.
- **R-GEST-4** **Side edge, inward, inside an app:** Back. The app is offered Back first; if it
  does not use it the shell goes Home. On Home the side edges are not a gesture.
- **R-GEST-5** **Top edge, down:** the shade. With the shade open, a drag up from the bottom of the
  screen closes it, and nothing under it can be reached or claimed.
- **R-GEST-6** While a gesture is in flight the model carries its live travel, its progress and
  **what releasing now would do**, so a view can show the outcome before the finger lifts.
- **R-GEST-7** *(open)* Multi-touch — pinch and rotate — reaches apps. Today one finger does.

## R-NAV — Tasks, recents, lifecycle

- **R-NAV-1** Launching an app that is already running brings its task to the front; it does not
  start a second one.
- **R-NAV-2** A task is **running** only while it is the focused task and the screen is showing
  it; otherwise it is **paused** and must stop drawing. Its last frame is kept and is what recents
  and transitions show.
- **R-NAV-3** Recents lists tasks most recently used first; a card can be tapped to the front or
  flicked up to close.
- **R-NAV-4** Back closes the shade first, then leaves recents, then is offered to the front app.
- **R-NAV-5** An app that exits, hangs up, or cannot start is removed from recents; one that could
  not start says so in a notification.
- **R-NAV-6** *(open)* Under memory pressure the shell stops the least recently used paused task
  and restarts it from its saved state when it is brought back.

## R-ANIM — Motion

Bound by `arstro.rule.design` §1 and `arstro.rule.animation`. Restated only where the shell adds to
them.

- **R-ANIM-1** Nothing a user can see changes in one frame: a change of front, a setting, a
  notification arriving or leaving, the display scale, an icon appearing.
- **R-ANIM-2** A transition a finger can interrupt is a spring, re-aimed from where it is. While a
  gesture is in flight the finger **is** the animation: the value follows it exactly, with no
  easing; on release it carries on from that value.
- **R-ANIM-3** An app's window is a container that changes size; the app's picture inside it is
  only ever scaled uniformly and cropped. A card in the recents row always has the screen's
  proportions.
- **R-ANIM-4** A leaving card fades **while** it travels and is gone exactly when it arrives; it is
  never removed at partial opacity, and its opacity never changes by more than 0.06 in 8 ms.
- **R-ANIM-5** An app opens out of its icon and goes home into it.
- **R-ANIM-6** The reduce-motion setting is reachable by the user (the shade and Settings) and
  reaches every app.
- **R-ANIM-7** A transition must not cost a redraw of the surfaces it moves (see R-PERF-1).

## R-HOME · R-BAR · R-SHADE — The shell's own surfaces

- **R-HOME-1** Home is a clock, the date and the installed apps. Nothing else.
- **R-HOME-2** The grid is whole cells, centred, with as many columns as the width allows; a
  resize re-flows it with every icon travelling to its new cell.
- **R-HOME-3** An app without artwork still looks deliberate: a tinted tile with its initial.
- **R-HOME-4** With nothing installed, Home says so and says how to install.
- **R-BAR-1** The status bar shows the time and what is switched on; an indicator makes room as it
  fades in, so its neighbours slide instead of jumping.
- **R-SHADE-1** The shade holds quick settings (Wi-Fi, Bluetooth, do not disturb, reduce motion),
  brightness and volume, and notifications, on one sheet, in one scrolling column.
- **R-SHADE-2** A slider moves by how far the finger travels, never by where it landed, and
  follows the finger exactly; only a value set from outside glides.
- **R-SHADE-3** A notification fades in, the others slide into a gap, and one fades out — none
  appears or vanishes. With none, the sheet says "No notifications".

## R-APP — What an app is, and how it reaches the screen

- **R-APP-1** An app is a separate process. It never opens a window, reads an input device, or
  knows what draws it.
- **R-APP-2** An app draws into a shared pixel buffer and commits; the shell copies it and answers
  when the frame is on screen. The buffer has one owner at a time, and an app cannot draw faster
  than the screen shows.
- **R-APP-3** The shell tells an app its size in pixels, its density, the insets the shell draws
  over, the accent and reduced motion — first, and again whenever any of them changes.
- **R-APP-4** Only a process the shell started for a task may attach to that task.
- **R-APP-5** An app with the `system` permission may send service commands and hears the event
  stream; no other app may.
- **R-APP-6** The transport (a local socket, shared memory, descriptor passing) is one file. A
  Windows port replaces that file and nothing above it.

## R-SDK — Writing an app

- **R-SDK-1** An app is an Artboard `Segment` tree and five moments: create, layout, back, paused,
  resumed. `arstro::app::run` does everything else.
- **R-SDK-2** Every size an app sees is dp.
- **R-SDK-3** An idle app draws nothing: it is drawn on input, for a short tail after, when it
  asks, or continuously while it says an animation is running.
- **R-SDK-4** The shell's tokens, typeface, icons and touch controls are one kit shared by the
  shell and every app, so an app cannot look like a guest.
- **R-SDK-5** *(open)* An app started without a shell opens its own window, for development.

## R-PKG — Packages

- **R-PKG-1** A bundle is a directory with `manifest.arstro` — `key = value` lines: `id`, `name`,
  `exec` required; `version`, `icon`, `accent`, `permissions` optional. An unknown key is an error.
- **R-PKG-2** The shell lists the bundles under the per-user directory, then the system one; the
  first wins. `$ARSTRO_APPS` overrides both.
- **R-PKG-3** A package (`.arstro`) is a gzip-compressed tar of a bundle. `arstro-pm` packs,
  installs, removes, lists and inspects.
- **R-PKG-4** An install is all-or-nothing: unpacked beside its destination, checked, moved into
  place with one rename.
- **R-PKG-5** A manifest may not point the program or the icon outside its bundle.
- **R-PKG-6** *(open)* Packages are signed and the shell refuses an unsigned one outside
  developer mode; apps are confined (bubblewrap on Linux).

## R-UI — Look

- **R-UI-1** Colour, typeface and the motion vocabulary are the family's, **aliased** from cosmo's
  token namespaces, never copied. What a touch screen changes is metrics: an 8 dp grid, a 48 dp
  minimum touch target, larger type, larger radii.
- **R-UI-2** One accent. It appears at rest only where something is on, and as one faint bloom on
  the wallpaper.
- **R-UI-3** Every tappable answers in the frame the finger lands, with an eased press.
- **R-UI-4** The typeface is compiled into every binary; no font file or font database is needed
  on the device.

## R-HOST — The front end that owns the screen

- **R-HOST-1** The host is a client of the service like any other.
- **R-HOST-2** SDL3 is the only thing that knows the platform's window, GPU and input.
- **R-HOST-3** Density: the display's own, times the user's `scale` setting. With no DPI to read,
  the short side of the screen is at most 540 dp.
- **R-HOST-4** Settings persist across restarts; a write is never half a file.
- **R-HOST-5** Scripted fingers (`tap`, `swipe`) enter where real ones do.
- **R-HOST-6** `scene` exposes the live eased values, so a test can tell a tween from a snap.
- **R-HOST-7** Headless frames are composited by the same scene, views and poses as a screen's.
- **R-HOST-8** A mouse stands in for one finger.

## R-PERF — Performance

- **R-PERF-1** The shell's surfaces are drawn into textures only when they change; every frame the
  GPU composites textures at the scene's poses. A full-screen transition draws nothing with the CPU.
- **R-PERF-2** A hidden surface is not redrawn.
- **R-PERF-3** *(open)* 60 fps on a Raspberry Pi 4 at its panel's native size, **measured on the
  board**. Until that measurement exists this is a goal, not a claim.
- **R-PERF-4** *(open)* The shell presents only when something changed.
- **R-PERF-5** *(open)* An app's own drawing is on the GPU (an Artboard GLES render target).

## R-SET — Settings

- **R-SET-1** Every setting has a type, a default and, for a number, a range; all are in the API
  document. An illegal value is rejected and nothing changes.
- **R-SET-2** The Settings app is an ordinary app with the `system` permission. It follows a change
  made elsewhere and never fights a control the finger is holding.
- **R-SET-3** *(open)* Brightness, volume, Wi-Fi and Bluetooth drive the hardware (backlight,
  PipeWire, NetworkManager, BlueZ) behind one interface. Today brightness is a dim drawn over the
  screen and the others are stored and shown only — and Settings says so.

## Open areas with no requirement written yet

On-screen keyboard and text input · a lock screen · power (sleep, shutdown, battery) · rotation ·
keyboard and controller navigation · the Windows port · accessibility beyond reduced motion. Each
gets its `R-` entries **before** any code (`arstro.rule` §2).

---

## Withdrawn

<details>
<summary><b>The GNOME/Plasma Android theme (plan M0–M9) — ❌ WITHDRAWN (2026-11-27)</b></summary>

**Trigger.** The founder: *"this will replace gnome/plasma version"* and *"only arstro app"*.

**What was tried, so nobody tries it again.** A touch shell as a *guest*: a GTK3 + gtk-layer-shell
client on KWin (driven by a KWin script over D-Bus) and on GNOME (by a companion shell extension).
All ten milestones were coded; none past M0 was ever verified on a screen, because every level
above a unit test needed a nested compositor, a layer-shell library or a VM that the development
machine did not have. As a guest it also could not own system gestures — a client never sees a
touch that lands on another client's window — so every gesture went through a per-desktop bridge.

**What the withdrawal must not break.** The six Artboard primitives from its M0 (path clip, opacity
layers, motion tokens, shadows, touch velocity and long-press, kinetic scrolling) are in Artboard
and are used here. Its icon generator and icon sources were copied into `kit/`. The
`android-shell` directory still builds behind `ARSTRO_BUILD_ANDROID_SHELL`; removing it is a
separate decision for the founder.

**Requirements withdrawn.** Parity on GNOME and Plasma; layer-shell surfaces; the KWin script and
GNOME extension bridges; a freedesktop notification daemon; `.deb`/`.rpm` session packaging; the
Android 17 pixel metrics as the specification. **None of them is in force.**
</details>
