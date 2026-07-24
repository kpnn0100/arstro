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

**Milestone M1 — Shell host skeleton. First task: M1.1 (scaffold `launcher/android-shell/` +
CMake, wired into the umbrella build).**

Last updated: 2026-07-24 · Last commit touching this project: Artboard `feature/1.0.0` e9c64e4
(AB-4 kinetic scrolling). Umbrella: this ledger's introduction.

---

## Milestone status at a glance

| M | Milestone | State | Track |
|---|---|---|---|
| M0 | Artboard primitives AB-1…AB-6 | **DONE** ✅ | Artboard repo |
| M1 | Shell host skeleton | not started | shared |
| M2 | `android_theme` module (color/type/shape/motion/icons) | not started | shared |
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

- [ ] **M1.1** Scaffold `launcher/android-shell/` dir tree (plan §3.4) + root `CMakeLists.txt`, added
  to the umbrella build via `add_subdirectory`. Links `artboard_core`, GTK3, gtk-layer-shell, Cairo,
  fontconfig via pkg-config. Builds an empty `arstro-android-shell` that opens one GTK window.
- [ ] **M1.2** `shell/SurfaceHost` — wrap one layer-shell toplevel + its `CairoTarget` + one Artboard
  root; `--windowed` debug mode (plain GTK window, no layer shell) and `--surface=N` single-surface
  mode for goldens. Copy the GTK3→Cairo glue from `cosmo/linux_main.cpp`.
- [ ] **M1.3** One shared frame clock ticks every surface's Artboard root `advance(nowMs)` +
  `GestureRecognizer::advance(nowMs)`; per-surface dirty flag so idle = zero redraws.
- [ ] **M1.4** GDK input → `RawPointer` (set `.touch` from GDK source device; map buttons/modifiers).
  Feed each surface's recognizer.
- [ ] **M1.5** `ShellState` (Artboard `Observable`s: theme mode, panel expansion fractions, notif
  store, tile states, window list) shared across surfaces. `NullBridge` + fake `SystemServices` wired.
- [ ] **M1.6** All five surfaces (launcher/statusbar/shade/edge-strips per plan §3.1 table) created at
  correct layer/anchor/exclusive-zone with placeholder fills. Verify geometry in nested KWin (L2).

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
