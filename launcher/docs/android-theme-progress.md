# Android Theme — Progress Ledger

**This file is the single source of truth for what is done and what to do next.** It is committed
to git and travels between computers. The `/android.theme.implement` skill reads the **NEXT** line
below, does that one task, updates this file, and commits — then repeats until §"Whole-project
done" is fully checked for **both GNOME and Plasma**.

- Full design + all concrete values: [`android-theme-plan.md`](android-theme-plan.md) (read the
  milestone's plan section before starting it).
- How to work: the `/android.theme.implement` skill (`.claude/skills/android.theme.implement/`).
- Checkbox legend: `[ ]` not started · `[~]` in progress · `[x]` done+verified · `[!]` done but
  UNVERIFIED (record why in Verification notes).

---

## ► NEXT

**Milestone M2 — `android_theme` module. First task: M2.1 (`theme/AndroidColors.h` — the M3 role
table from plan §2.2, light + dark, exposed via the `ShellState` `Observable<ThemeMode>`).**

Note for M2.1: this is a shell-app task (route: `arstro.design.desktop` design language + plan §2.2
values). Create `launcher/android-shell/theme/AndroidColors.h` with the full M3 role table (surface/
onSurface/primary/primaryContainer/outline/… — copy the exact hex values from plan §2.2), for BOTH
light and dark, as `artboard::Color`. Provide `const AndroidColors& colors(ThemeMode)` returning the
right set, so a surface does `colors(state.themeMode.get())`. Keep it pure data (header-only). Verify
L0: assert a couple of role values per mode + that Light≠Dark for `surface`. This unblocks every
surface's real styling (M3+). (M2 overall is L1-golden work — none of it needs L2, so M1.6's pending
L2 check does not block M2.)

Handy: `arstro-android-shell --surface=N --size=WxH --render-png=PATH` (L1 golden, works for all 7
surfaces); default run (no `--surface`) opens ALL surfaces; `--self-test` runs the L0 checks.

Last updated: 2026-07-24 · Last commit touching this project: umbrella `main` (M1.6 all surfaces).
Artboard: `feature/1.0.0` e9c64e4 (AB-4, unchanged).

---

## Milestone status at a glance

| M | Milestone | State | Track |
|---|---|---|---|
| M0 | Artboard primitives AB-1…AB-6 | **DONE** ✅ | Artboard repo |
| M1 | Shell host skeleton | code-complete; on-screen/layer-shell L2 verify PENDING | shared |
| M2 | `android_theme` module (color/type/shape/motion/icons) | in progress (starting M2.1) | shared |
| M3 | Status bar + system services | not started | shared |
| M4 | Notification panel + notifyd | not started | shared |
| M5 | Quick settings | not started | shared |
| M6 | Launcher (home + drawer + folders) | not started | shared |
| M7 | Gestures + recents + split — **Plasma/KWin bridge** | not started | Plasma |
| M8 | **GNOME track** (extension bridge, parity, degraded mode) | not started | GNOME |
| M9 | Sessions + packaging (deb + rpm, both desktops) | not started | both |

"shared" milestones (M1–M6) are desktop-agnostic (they run against `NullBridge` + fake services,
verified in nested KWin). The GNOME/Plasma split is real only from M7 on.

---

## M0 — Artboard primitives — DONE ✅

All six committed to the **Artboard repo**, branch `feature/1.0.0` (its active dev line; `main`
there is a stale 3-commit branch, deliberately not used), one feature per commit, each with docs +
puml synced and 100% line coverage on touched core files (128 core tests pass):

- [x] AB-1 `clipPath()` — arbitrary path clip (FR-26) · commit 8d92cab
- [x] AB-2 `pushLayer`/`popLayer()` — opacity-group compositing (FR-27) · commit f8e9bdf
- [x] AB-6 motion tokens — 5 cubic-bezier easings + Spring/duration presets (FR-31) · commit 279f465
- [x] AB-5 `drawShadow`/`drawElevation` — gradient-composed soft shadow (FR-30) · commit b7845d6
- [x] AB-3 touch input — velocity/fling/long-press + touch-aware hover (FR-28) · commit 53ae864
- [x] AB-4 `ScrollView` kinetic scrolling — rubber-band + fling + spring snap-back (FR-29) · commit e9c64e4

⚠ **Verification gap (must clear before shipping):** the web/Canvas2D adapter code for AB-1/AB-2
(`Canvas2DTarget.cpp` EM_JS `clipPath`/`pushLayer`/`popLayer`) is **UNVERIFIED** — no `emcc` was
available. Before M9 (or whenever emcc is present), run `./build.sh --target linux-web-server` from
the umbrella and confirm it builds; then change this line to cleared. Native/Cairo path is build-
verified.

---

## M1 — Shell host skeleton  (plan §3.1, §3.4)

**Goal:** `arstro-android-shell` binary — GTK3 + gtk-layer-shell multi-toplevel host that ticks all
Artboard surfaces from one frame clock and feeds GDK touch/pointer as `RawPointer`. No real UI yet.
**DoD:** placeholder-colored surfaces at correct geometry render in nested KWin (test L2); a drag-box
demo holds 60fps; CPU ≈0% idle. **Test level:** L1 smoke golden + L2 manual.

- [x] **M1.1** Scaffold `launcher/android-shell/` dir tree (plan §3.4) + root `CMakeLists.txt`, added
  to the umbrella build via `add_subdirectory`. Links `artboard_core`, GTK3, gtk-layer-shell, Cairo,
  fontconfig via pkg-config. Builds an empty `arstro-android-shell` that opens one GTK window.
  → Done: `launcher/android-shell/{CMakeLists.txt, shell/main.cpp}` + §3.4 subdir stubs; wired into
  the umbrella root `CMakeLists.txt` (option `ARSTRO_BUILD_ANDROID_SHELL`, default ON). Builds clean;
  `arstro-android-shell --self-test` passes (GTK 3.24.33 inits, artboard_core links). gtk-layer-shell
  made OPTIONAL (absent here) — window path compiled but on-screen display unverified headless.
- [x] **M1.2** `shell/SurfaceHost` — wrap one layer-shell toplevel + its `CairoTarget` + one Artboard
  root; `--windowed` debug mode (plain GTK window, no layer shell) and `--surface=N` single-surface
  mode for goldens. Copy the GTK3→Cairo glue from `cosmo/linux_main.cpp`.
  → Done: `shell/SurfaceHost.{h,cpp}` (one toplevel + CairoTarget + root, a shared pure `paint()`
  used by both the GTK draw signal and headless rendering) + `defaultSurfaces()` (the 5 §3.1 surface
  configs). `main.cpp` gains `--windowed`, `--surface=N`, `--size=WxH`, `--render-png=PATH`.
  CairoTarget.cpp compiled into the binary (not in artboard_core), like cosmo. **Verified L1-style:**
  `--render-png` produces correct-dimension PNGs with the surface drawn (dark bg + placeholder
  title) — the draw path is real-pixel-confirmed headless. See Verification notes for what's not.
- [x] **M1.3** One shared frame clock ticks every surface's Artboard root `advance(nowMs)` +
  `GestureRecognizer::advance(nowMs)`; per-surface dirty flag so idle = zero redraws.
  → Done: `shell/FrameClock.{h,cpp}` (one ~60Hz GLib timeout fans a monotonic `nowMs` to every
  registered surface; `tick(nowMs)` for headless tests). `SurfaceHost::frameTick()` advances the
  root + its owned `GestureRecognizer`, and `queue_draw`s ONLY when dirty; `paint()` clears the
  dirty flag + counts; `markDirty()` for external invalidation; `setAnimatingQuery()` lets content
  opt into continuous redraw. **Verified L0** (`--self-test` `frame-clock: ok`): static surface →
  idle after 1 paint (zero redraws); animating surface → redraws until its predicate settles;
  `markDirty` forces one; one clock fans out to N surfaces. GTK `g_timeout_add` on-screen loop
  wired in `main.cpp` but not verifiable headlessly (see Verification notes).
- [x] **M1.4** GDK input → `RawPointer` (set `.touch` from GDK source device; map buttons/modifiers).
  Feed each surface's recognizer.
  → Done: `SurfaceHost::onButton`/`onMotion` translate GDK button/motion events into
  `artboard::RawPointer` (button map, `GDK_MOD1/SHIFT/CONTROL` modifiers, `.touch` from
  `gdk_device_get_source() == GDK_SOURCE_TOUCHSCREEN`) and `feedPointer()` them into the surface's
  recognizer; the recognizer's sink routes gestures to the root and `markDirty()`s. Chose the
  emulated-pointer path (no `GDK_TOUCH_MASK`) so mouse + finger share one handler with a correct
  `.touch` and no double events (v1 is single-pointer per plan). **Verified L0** (`--self-test`
  `input: ok`): a synthesized touch tap yields Down+Click at the root, both `touch=true`, surface
  dirtied. GDK-event translation itself not drivable headlessly (see Verification notes).
- [x] **M1.5** `ShellState` (Artboard `Observable`s: theme mode, panel expansion fractions, notif
  store, tile states, window list) shared across surfaces. `NullBridge` + fake `SystemServices` wired.
  → Done: `compositor/CompositorBridge.h` (plan §3.2 interface + `WindowInfo`/`TopWindowHint`/`PngSink`
  + `NullBridge` no-op); `system/SystemServices.h` (plan §3.3 read/toggle/power interface +
  `FakeSystemServices` canned values, airplane cascades wifi+bt off, power actions counted);
  `shell/ShellState.h` (`Observable` themeMode + 2 shade expansions + window list, holding
  `CompositorBridge&`+`SystemServices&`, mirroring the bridge's window list). Notification store
  (M4) + QS tile states (M5) get added to ShellState as those surfaces are built (noted in the
  header). **Fully verified L0** (`--self-test` `shell-state: ok`): Observable fireNow + change-only
  notify + no-op-set guard; fakes canned + setter round-trip + airplane cascade + power counts;
  NullBridge callable + ShellState mirrors its empty window list. (Pure interfaces/fakes — L0 is the
  complete DoD, nothing display-dependent.)
- [!] **M1.6** All five surfaces (launcher/statusbar/shade/edge-strips per plan §3.1 table) created at
  correct layer/anchor/exclusive-zone with placeholder fills. Verify geometry in nested KWin (L2).
  → Done (wiring): edge-strips split into the real 3 (edge-left/right 32×full-height, edge-bottom
  full-width×24) → **7 surfaces** total; default run (no `--surface`) creates a `SurfaceHost` per
  config (heap-stable `unique_ptr` — `SurfaceHost` is now non-copyable since its sink captures
  `this`), each with a placeholder root + the shared `ShellState`, all on ONE `FrameClock`, shown.
  `--surface=N` stays single-surface mode. **Verified L0+L1** (`--self-test` `all-surfaces: ok`: 7
  hosts, 1 clock, 1 state, each paints + has shellState wired; `--render-png` of all 7 → correct
  config-driven dims + alpha: overlays RGBA-transparent, opaque surfaces RGB). **`[!]` because the
  DoD's L2 (nested-KWin geometry) could NOT run here** — see Verification notes; the layer-shell
  anchoring/exclusive-zone + on-screen multi-window display are unverified pending a capable machine.

## M2 — `android_theme` module  (plan §2.2)

**Goal:** the named design system. **DoD:** token sample-sheet golden PNGs (light+dark) committed as
L1 baselines; icon codegen is a CMake step; `licenses/` ships Apache-2.0 notices. **Test:** L1.

- [ ] **M2.1** `theme/AndroidColors.h` — the M3 role table (plan §2.2), light + dark, as `Observable`
  theme mode.
- [ ] **M2.2** `theme/Type` — vendor Roboto + Roboto Flex (app-private fontconfig registration like
  cosmo's DM Sans) + the type ramp. `theme/Shape` (radius scale) + `theme/Motion` (bind AB-6 tokens).
- [ ] **M2.3** Icon codegen: `assets/icons-src/*.svg` → build-time Python script → `theme/icons/*.h`
  cubic-path tables (flatten arcs to cubics). Start with the ~44 glyphs listed in plan §2.2.
- [ ] **M2.4** Adaptive-icon masker: mask path (circle + squircle options) + `clipPath` + `registerImage`
  pipeline (uses AB-1). Themed/monochrome icon tinting.
- [ ] **M2.5** Token sample-sheet screen → commit L1 golden PNGs (light+dark). Ship `licenses/` notices.

## M3 — Status bar + services  (plan §2.3, §3.3)

**Goal:** live status bar. **DoD:** clock/battery/wifi live over nested-KWin wallpaper; goldens for
{light,dark,charging,no-wifi,dnd}×{wallpaper-tint,surface-tint}. **Test:** L0 (service fakes), L1, L2.

- [ ] **M3.1** `system/SystemServices` aggregate interface + **fakes** (for tests) + real clients:
  NetworkManager (wifi), UPower (battery), BlueZ (bt) — D-Bus surface per plan §3.3.
- [ ] **M3.2** Status bar surface (plan §2.3): height 24, clock (locale 12/24h), battery glyph+percent,
  wifi glyph, bt/dnd/airplane conditional icons; tint modes (wallpaper vs surface).
- [ ] **M3.3** Shade-trigger hit bands (left/right top-edge) emit `ShellState.shadeDrag`. Exclusive zone.
- [ ] **M3.4** Goldens (L1) for the 5×2 state matrix; live check in nested KWin (L2).

## M4 — Notification panel + notifyd  (plan §2.4, §3.3)

**Goal:** left dual-shade + notification daemon. **DoD:** in an isolated bus, `notify-send` variants
(urgency/actions/image/transient) render per spec; action round-trip works; goldens {empty, 3-mixed,
expanded, heads-up}; swipe-dismiss physics via scripted `RawPointer` replay. **Test:** L0, L1, L2.

- [ ] **M4.1** `notifyd` — in-process GDBus `org.freedesktop.Notifications` (spec 1.2) + `NotificationStore`.
- [ ] **M4.2** Notification panel surface (plan §2.4): trigger/geometry/scrim, card anatomy, sections,
  kinetic list (AB-4), header + clear-all.
- [ ] **M4.3** Swipe-to-dismiss physics + heads-up (auto-dismiss, drag-to-open) + DND + empty state.
- [ ] **M4.4** L0 replay tests for dismissal, L1 goldens, L2 in isolated `dbus-run-session`.

## M5 — Quick settings  (plan §2.5)

**Goal:** right dual-shade. **DoD:** goldens {closed→open @ t=0/0.5/1, tile active/inactive/unavailable,
both themes}; tile shape-morph verified via recorded op stream at 3 timestamps. **Test:** L0, L1, L2.

- [ ] **M5.1** QS panel surface + header (clock/battery/gear/power) + open/close motion (AB tokens).
- [ ] **M5.2** Brightness + volume sliders wired to services (logind SetBrightness, PipeWire/libpulse).
- [ ] **M5.3** 4-col tile grid: tile states (active/inactive/unavailable) + M3-expressive shape-morph
  on toggle + press scale. v1 tile set per plan §2.5.
- [ ] **M5.4** Tile actions wired to services (wifi/bt/dnd/airplane/dark-theme/nightlight/screenshot…).
- [ ] **M5.5** L0 (fakes) + L1 goldens + L2.

## M6 — Launcher  (plan §2.6)

**Goal:** home + drawer + folders. **DoD:** launches real apps in nested KWin (GAppInfo activation
token); goldens {home, drawer-mid, drawer-open, folder-open, search}; drag-rearrange replay. **Test:** L0, L1, L2.

- [ ] **M6.1** Wallpaper layer + scrim; home grid (5-col) + dock + page indicator + JSON layout store.
- [ ] **M6.2** App icon pipeline: GTK icon theme 128px → mask (circle/squircle) → `registerImage`;
  fallback colored-circle+letter. App enumeration via GIO `GAppInfo`.
- [ ] **M6.3** Press feedback + long-press popup (App info / Remove / .desktop Actions) + drag-rearrange.
- [ ] **M6.4** All-apps drawer: swipe-up sheet + search field (live filter) + A-Z fast-scroll rail.
- [ ] **M6.5** Folders (drag icon onto icon; container-transform open) + JSON persistence.
- [ ] **M6.6** L0 replay (drag-rearrange, search) + L1 goldens + L2 real-app-launch.

## M7 — Gestures + recents + split — Plasma/KWin bridge  (plan §2.7, §3.2.1, §3.2.3–5)

**Goal:** gesture nav + overview + split on KWin. **DoD:** on nested KWin w/ 3 test windows — back
arrow goldens @ protrusion 0/50/100%; home/overview/quick-switch thresholds honored (replay); split
50:50→drag→67:33; `kwinrc` gesture suppression documented. **Test:** L1, L2 (+ scripted input).

- [ ] **M7.1** `compositor/CompositorBridge.h` interface (plan §3.2) + `KWinBridge` + the `kwin-script/`
  (JS) exposing `org.arstro.AndroidShell.Compositor` D-Bus (list/activate/close/tile/ratio/back).
- [ ] **M7.2** Edge strips (invisible layer-shell overlays) + back-arrow affordance + commit/cancel + back semantics (plan §3.2.4).
- [ ] **M7.3** Home pill + swipe-up (home vs overview via pause-detect vs quick-switch) — thresholds per plan §2.7.2.
- [ ] **M7.4** Overview/recents: MRU card row (icon-cards v1), swipe-up-to-close, tap-to-activate, clear-all.
- [ ] **M7.5** Split-screen: pair-picker, divider drag + snap points, dissolve; fullscreen bar hide/reveal.
- [ ] **M7.6** Open-maximized default rule; `kwinrc` gesture-suppression profile. L1 goldens + L2 scripted-input.
- [ ] **M7.7** (v1.5, optional) per-window thumbnails via `org.kde.KWin.ScreenShot2`.

## M8 — GNOME track  (plan §3.2.2)

**Goal:** the same shell working on GNOME via a companion extension. **DoD:** the M3–M7 smoke checklist
passes on nested `gnome-shell --nested --wayland` (L3) within its input limits, and fully in a GNOME VM
(L4); extension passes `gnome-extensions pack` lint. **Test:** L3 + L4.

- [ ] **M8.1** `gnome-extension/` (ESM, GNOME 46–49): host our shell client via `Meta.WaylandClient`,
  pin surfaces at their §3.1 geometry, keep-above overlays.
- [ ] **M8.2** `GnomeBridge` D-Bus parity — same `org.arstro.AndroidShell.Compositor` API via
  `Meta.Window`/`global.display` (list/activate/close/tile, maximize-on-map).
- [ ] **M8.3** Hide stock chrome (panel/dash/hot-corner), restore on disable.
- [ ] **M8.4** Notification mirror: `Main.messageTray` sources → our D-Bus → same `NotificationStore` (M4).
- [ ] **M8.5** Degraded GNOME mode (plan §3.2.2) implemented + documented (for when WaylandClient breaks).
- [ ] **M8.6** L3 nested-gnome smoke + L4 GNOME VM full run; `gnome-extensions pack` lint clean.

## M9 — Sessions + packaging  (plan §7)

**Goal:** installable deb + rpm, both desktops, custom sessions. **DoD:** clean VM matrix — Ubuntu
24.04 (GNOME), Fedora 41+ (GNOME), Fedora KDE, Kubuntu — install → log into "Android Shell" session →
15-point smoke checklist (below) passes; uninstall leaves stock session intact. **Test:** L4.

- [ ] **M9.1** Clear the M0 web-adapter verification gap (run `./build.sh --target linux-web-server`).
- [ ] **M9.2** wayland-session `.desktop` files (Plasma variant: kwin_wayland w/o plasmashell + kwinrc
  profile; GNOME variant: extension-enabled session) + systemd user units.
- [ ] **M9.3** CPack DEB + RPM from CMake install rules; `packaging/debian/` + `.spec` skeletons kept.
  Subpackages: base + `-plasma` (kwin-script) + `-gnome` (extension). Install-path whitelist so the
  153GB tree can never glob into a package.
- [ ] **M9.4** VM matrix acceptance (L4): all 4 targets, 15-point smoke checklist, clean uninstall.

---

## Whole-project done (the finish line — both desktops)

- [ ] All of M1–M6 green (shared surfaces).
- [ ] **Plasma:** M7 green — launcher/statusbar/QS/notifications/gestures/recents/split all work in a
  Plasma-based session (or nested KWin), package installs + session logs in.
- [ ] **GNOME:** M8 green — same feature set works via the extension in a GNOME session (or degraded
  mode documented + working), package installs + session logs in.
- [ ] M9 green — deb + rpm build in CI, VM matrix passes the 15-point smoke checklist on Ubuntu-GNOME,
  Fedora-GNOME, Fedora-KDE, Kubuntu; uninstall clean.
- [ ] M0 web-adapter verification gap cleared.
- [ ] M1 on-screen/layer-shell L2 verification cleared (7 surfaces anchor correctly + take input in
      nested KWin — see M1.6 Verification note; was code-complete but built/tested only headless).
- [ ] The 7 user deliverables all demonstrably working on **both** GNOME and Plasma: launcher · quick
  settings (right-half swipe) · status bar (wifi/battery/clock) · notification panel (left-half swipe)
  · gesture nav (back/home/recents) · all animation+icons · fullscreen + intentional split.

**15-point smoke checklist (M9.4):** 1 launcher shows apps · 2 tap launches an app maximized · 3 status
bar shows live clock/battery/wifi · 4 left-half top swipe opens notifications · 5 notification arrives
(`notify-send`) + dismiss-swipe works · 6 right-half top swipe opens quick settings · 7 a QS tile
toggles a real service (wifi/bt/dnd) · 8 brightness slider changes brightness · 9 back gesture works ·
10 home gesture returns to launcher · 11 recents/overview shows open windows + switch · 12 close a window
from recents · 13 split two windows + drag divider · 14 all transitions animate (nothing snaps) ·
15 reduced-motion setting collapses motion.

---

## Decisions & deviations log

Record any decision that departs from the plan, or resolves an open item, here (newest first) so a
future session on another machine doesn't re-litigate it. Format: `YYYY-MM-DD — decision — why`.

- 2026-07-24 — M1.6: default run (no `--surface`) now opens ALL 7 surfaces (the real shell);
  `--surface=N` is the single-surface goldens/debug mode. `SurfaceHost` made non-copyable/non-movable
  (its recognizer sink captures `this`), so instances live behind `unique_ptr`/`static` — never in a
  relocating value container. Edge-strips split into 3 real strips per plan §3.1/§2.7.
- 2026-07-24 — M1.5: `ShellState` deliberately ships only themeMode + the two shade expansions +
  the window list (+ the two seam refs). The notification store (M4) and QS tile states (M5) that
  the task note mentioned are **added to ShellState when those surfaces are built**, not
  speculatively now — avoids designing a store/tile model before its consumer exists. Not a plan
  deviation; the plan lists them as ShellState's eventual contents, and M1.5 is the skeleton.
- 2026-07-24 — M1.4: touch is detected from the GDK **source device** on emulated pointer events
  (no `GDK_TOUCH_MASK`, no separate touch handler), so mouse and finger share one code path with a
  correct `.touch` flag and no double-reporting. True multi-touch (independent finger sequences) is
  intentionally out of scope — plan v1 needs no multi-finger gestures; a single pointer covers the
  matrix. Revisit only if a future surface needs 2-finger input.
- 2026-07-24 — M1.3: per-surface dirtiness for animation is driven by a content-supplied
  `setAnimatingQuery()` predicate (returns true while animating), because Artboard's `Segment` has no
  tree-wide "is anything animating?" aggregate. Static content (no predicate) goes idle after one
  paint. **Candidate future AB task:** add `Segment::isAnimating()` (aggregate of child
  Property/Spring/hover activity) so the host can auto-detect animation and drop the per-content
  predicate — an `implement_artboard` mini-cycle, deferred (not needed until animated surfaces, M2+).
- 2026-07-24 — M1.2: added a headless `--render-png` mode (renders any surface to PNG with no
  display, via a `SurfaceHost::paint()` shared with the GTK draw signal). This IS the L1 golden-image
  mechanism — later golden milestones (M2.5, M3.4, …) render + diff through it rather than needing a
  compositor. Design choice: the pure `paint()` split lets on-screen and headless produce identical pixels.
- 2026-07-24 — M1.1: `gtk-layer-shell` made an OPTIONAL CMake dependency (it is absent on the
  current dev machine). M1.1 only needs a plain GTK window, so it builds without it; from M1.2 the
  layer-shell paths go behind `#ifdef HAVE_GTK_LAYER_SHELL` with a `--windowed` fallback. Real
  layer surfaces need `libgtk-layer-shell-dev` installed (or verification in nested KWin).
- 2026-07-24 — The umbrella now builds `arstro-android-shell` by default (`ARSTRO_BUILD_ANDROID_SHELL`
  ON). It depends only on `artboard_core` + GTK3/Cairo/fontconfig, all already required by cosmo, so
  this does not add a new hard dependency to the umbrella build.
- 2026-07-24 — Artboard work committed to `feature/1.0.0`, not `main` — that branch was already
  checked out and is the repo's real active line; `main` is a stale 3-commit branch.
- 2026-07-24 — `launcher/` tracked inside the umbrella `arstro` repo (android17 gitignored) rather
  than a standalone repo — one repo, existing remote, everything pulls together.

## Verification notes (honesty ledger)

What has actually been run vs. only written. Keep this truthful — a `[!]` in the checklists points here.

- M0 web/Canvas2D adapter (`clipPath`/`pushLayer`/`popLayer` EM_JS): **UNVERIFIED**, no emcc in the
  build env when written. Native/Cairo + RecordingTarget paths verified (128 tests, 100% coverage).
- No visual/on-screen rendering has been checked for anything yet (headless env); only op-stream
  correctness + native compile. First real pixels happen at M1.6 (nested KWin).
- M1.1: `arstro-android-shell` builds + links + `--self-test` passes headless (GTK inits,
  artboard_core usable). The actual `gtk_widget_show_all` window-display path is compiled but
  **not visually confirmed** (no display in this env) — it is a standard 4-line GTK call and will
  first be seen on screen at M1.6. gtk-layer-shell absent here, so the layer-shell build variant is
  entirely unbuilt/untested on this machine (plain-window mode only).
- M1.3: the frame-clock + dirty-gating **logic is verified headless (L0, `--self-test`)** — static
  idle, animating redraws, markDirty, N-surface fan-out. **NOT verified:** the actual GLib
  `g_timeout_add` → `gtk_widget_queue_draw` → on-screen redraw loop (needs a display/main loop);
  first seen at M1.6 in nested KWin.
- M1.4: the **`RawPointer` → recognizer → root → markDirty** chain is verified headless (L0,
  `--self-test` `input: ok`), incl. the touch flag carrying through. **NOT verified:** the GDK-event
  translation layer (`onButton`/`onMotion`, `gdk_device_get_source` touch detection) — needs real
  GDK events from a display/compositor.
- M1.6 / **the standing M1 gap**: the multi-surface WIRING is verified L0+L1 (7 hosts, 1 clock, 1
  state, all render at correct dims). **NOT verified on this machine — the whole on-screen + layer-
  shell side of M1** (accumulates the M1.1–M1.4 on-screen caveats): (a) the `#ifdef
  HAVE_GTK_LAYER_SHELL` anchoring/layer/exclusive-zone code is **not even compiled** (gtk-layer-shell
  absent), so surfaces positioning at screen edges is untested; (b) the on-screen multi-window
  display + real GDK events + the GLib-timeout redraw loop need a display/compositor; (c) L2
  nested-KWin geometry needs `kwin_wayland`. **To clear:** on a machine with `libgtk-layer-shell-dev`
  + `kwin_wayland`, run `dbus-run-session -- env XDG_RUNTIME_DIR=$(mktemp -d) kwin_wayland --width
  1600 --height 1000 ./arstro-android-shell` and confirm the 7 surfaces anchor correctly + respond to
  input. Until then M1 is code-complete but not L2-verified (does not block M2–M6, which are L0/L1).
- M1.2: the `SurfaceHost` **draw path is real-pixel-verified headless** (`--render-png` output
  inspected: correct dims, dark surface bg + placeholder title rendered). **NOT verified:** (a) the
  on-screen GTK window path (`create`/`show`/`gtk_main`) — no display, same as M1.1; (b) the
  **entire layer-shell code path** (`#ifdef HAVE_GTK_LAYER_SHELL` in `SurfaceHost::create`) — it is
  not compiled on this machine (gtk-layer-shell absent), so `gtk_layer_*` calls, anchors, layers,
  and exclusive zones are **unbuilt and untested**. First real verification of both at M1.6 in nested
  KWin (needs `libgtk-layer-shell-dev` + `kwin_wayland`, neither present here).
