# Android Theme for Linux — Implementation Plan

Goal: recreate the **Android 17 phone shell** (launcher, status bar, quick settings,
notification panel, gesture navigation) as a **touch-friendly shell for Ubuntu and Fedora**,
on both **GNOME** and **KDE Plasma**, rendered with **Artboard** (Cairo/GTK host).
It is written so a lower-tier model can implement it milestone by milestone without
re-deriving decisions.

## Status (2026-07-24)

**M0 (Artboard primitives AB-1…AB-6) is DONE** — implemented, unit-tested (100% line coverage
on every touched core file via `RecordingTarget`), documented (requirements/architecture/
detailed_design/puml kept in sync per `/implement_artboard`), and committed one-feature-per-commit
to the Artboard repo's `feature/1.0.0` branch (its actual active development line — `main` there
is a stale, 3-commit branch predating all of this project's real feature work; nothing was merged
to `main`, and no branch was created for this — `feature/1.0.0` was already checked out before
this work began). Six commits: `clipPath()`, `pushLayer`/`popLayer`, named motion tokens
(cubic-bezier easings + Spring/duration presets), `drawShadow`/`drawElevation`, touch input
(velocity/fling/long-press + touch-aware hover — this surfaced and fixed a real pre-existing
`Segment` gesture-routing bug along the way), and `ScrollView` kinetic scrolling (rubber-band
overscroll + fling decay + spring snap-back). **M1 (shell host skeleton) has not been started.**

Verification performed: full native build (`cmake --build`) succeeds standalone and as part of
the umbrella's `cosmo` app (confirming `CairoTarget.cpp`'s new ops compile and link against real
Cairo headers). **Not verified**: the web/Canvas2D adapter (`Canvas2DTarget.cpp`'s new
`pushLayer`/`popLayer`/`clipPath` EM_JS code) — this environment has no `emcc`/Emscripten
installed, so `./build.sh --target linux-web-server` could not be run. That code was written
carefully against the existing EM_JS patterns but is unverified; run the web build before relying
on it. No visual/runtime rendering was checked either (headless environment, no display) — only
op-stream and native-compile correctness.

Conventions used below:
- `$A` = `apps/launcher/plasma.android/android17` (the 153 GB AOSP checkout — **reference only, never built, never packaged, stays untracked**).
- **(v)** = value verified in this tree (file cited). **(m3)** = standard Material 3 value, assumed —
  implementer may refine against the cited `$A` file but must not block on it.
- "px" means Artboard logical units; one global `uiScale` transform maps logical→device pixels
  (default 1.0 at 96 dpi; read from GDK monitor scale). Treat Android "dp" == our logical px.

---

## 0. Reading order for the implementer

1. This file, top to bottom.
2. `core/Artboard/docs/requirements.md` (mandatory per `/implement_artboard` — every Artboard change below follows that skill's V-model).
3. `apps/cosmo/linux_main.cpp` — the GTK3→CairoTarget host glue to copy.
4. `apps/cosmo/touch/PhoneApp.{h,cpp}` — existing touch-style Artboard app (screens + animated transitions); reuse its patterns.
5. Only then open `$A` files when a **(v)**/pointer says so. Never grep `$A` root (153 GB).

---

## 1. Scope

### 1.1 Deliverables (user's list → plan sections)

| # | Deliverable | Sections |
|---|---|---|
| 1 | Launcher: home screen for apps | §2.6, M6 |
| 2 | Quick settings: swipe down from **right** half of top edge | §2.5, M5 |
| 3 | Status bar: wifi, battery, clock | §2.3, M3 |
| 4 | Notification panel: swipe down from **left** half of top edge | §2.4, M4 |
| 5 | Gesture navigation: back, home, select windows (recents) | §2.7, M7 |
| 6 | All animation + icons | §2.2, M2, and per-surface motion specs |
| 7 | Fullscreen mode + intentional split, Android-style | §2.7.5, M7 |

The left/right dual-shade split matches Android 17's real model: SystemUI's **dual shade**
exists in this tree — see (v) `$A/frameworks/base/packages/SystemUI/src/com/android/systemui/shade/shared/flag/DualShadeFlag.kt`
and `shade/domain/interactor/ShadeModeInteractor.kt`.

### 1.2 Non-goals

- **No app widgets** (explicitly excluded by user). No Google Discover panel, no taskbar, no lockscreen/keyguard, no bubbles, no media-output switcher. Media player card in QS: out of v1.
- No Android app compatibility (Waydroid etc.) — this is a *theme/shell*, Linux apps run in it.
- X11 sessions: not a target. Wayland only (XWayland clients are fine as windows).
- No reuse of AOSP code or proprietary assets (no Google Sans, no Pixel wallpapers). We reimplement drawings from spec and ship Apache-2.0 assets (Roboto, Material Symbols).

### 1.3 Target platforms

Ubuntu 24.04+ (GNOME 46+), Fedora 41+ (GNOME 47+/49), Fedora KDE & Kubuntu (Plasma 6.1+).
Two integration tracks (§3.2): **Plasma/KWin** (primary — has layer-shell) and **GNOME** (companion shell-extension bridge).

---

## 2. Reference spec — what we clone from Android 17

### 2.1 Global principles

- Everything animates (Artboard rule §2A of `implement_artboard` matches Android's behavior): panels slide/settle with springs, state changes crossfade, nothing pops.
- All chrome is drawn by us via Artboard; no GTK widgets except the host `GtkDrawingArea` per surface.
- Dark and light theme both required, switchable live (Observable `ThemeMode`).

### 2.2 Design system ("Android theme" module)

This module is the *named* design system the user asked for: **`android_theme`**.

**Color.** Material 3 role table, baked (no dynamic wallpaper color in v1 — that is M8 stretch;
algorithm lives at (v) `$A/frameworks/base/packages/SystemUI/monet/`). Ship the M3 baseline
scheme (m3), both modes, as `theme/AndroidColors.h`:

| Role | Light | Dark |
|---|---|---|
| primary | #6750A4 | #D0BCFF |
| onPrimary | #FFFFFF | #381E72 |
| primaryContainer | #EADDFF | #4F378B |
| onPrimaryContainer | #21005D | #EADDFF |
| secondaryContainer | #E8DEF8 | #4A4458 |
| onSecondaryContainer | #1D192B | #E8DEF8 |
| surface | #FEF7FF | #141218 |
| onSurface | #1D1B20 | #E6E0E9 |
| onSurfaceVariant | #49454F | #CAC4D0 |
| surfaceContainerLowest | #FFFFFF | #0F0D13 |
| surfaceContainerLow | #F7F2FA | #1D1B20 |
| surfaceContainer | #F3EDF7 | #211F26 |
| surfaceContainerHigh | #ECE6F0 | #2B2930 |
| surfaceContainerHighest | #E6E0E9 | #36343B |
| outline | #79747E | #938F99 |
| outlineVariant | #CAC4D0 | #49454F |
| error | #B3261E | #F2B8B5 |
| scrim | #000000 | #000000 |
| inverseSurface | #322F35 | #E6E0E9 |
| inverseOnSurface | #F5EFF7 | #322F35 |

**Typography.** Vendor **Roboto** + **Roboto Flex** (Apache-2.0) exactly the way cosmo vendors
DM Sans: app-private Fontconfig registration in the host (`apps/cosmo/linux_main.cpp` shows the
call pattern). Ramp (m3): display 45/36, headline 32/28/24, title 22/16(Medium)/14(Medium),
body 16/14/12, label 14/12/11 (Medium). Clock in status bar: 14 Medium; big clock in shade
header: 45 (hour) / QS header 28.

**Shape scale** (m3): extra-small 4, small 8, medium 12, large 16, extra-large 28, full = h/2.
Notification cards use 28 — (v) `notification_corner_radius = 28dp` in
`$A/frameworks/base/packages/SystemUI/res/values/dimens.xml`.

**Adaptive icon mask** — exact AOSP default mask path, viewport 100×100 (v)
`$A/frameworks/base/core/res/res/values/config.xml` `config_icon_mask`:

```
M 50 0 L 92 0 A 8 8 0 0 1 100 8 L 100 92 A 8 8 0 0 1 92 100 L 8 100 A 8 8 0 0 1 0 92 L 0 8 A 8 8 0 0 1 8 0 L 50 0 Z
```

We offer two mask options in settings: `circle` (Pixel look, default) and `rounded-square`
(the path above). Icon pipeline: §M2. Themed (monochrome) icons: tint glyph
`onPrimaryContainer`, background `primaryContainer` (m3).

**Motion tokens** (m3, refine at (v) `$A/frameworks/base/packages/SystemUI/animation/` and
`core/res/res/interpolator/`): implemented as Artboard constants (AB-6):

- Easings: `standard(0.2,0,0,1)`, `standardDecel(0,0,0,1)`, `standardAccel(0.3,0,1,1)`,
  `emphasizedDecel(0.05,0.7,0.1,1)`, `emphasizedAccel(0.3,0,0.8,0.15)`.
- Durations: short 50/100/150/200, medium 250/300/350/400, long 450/500/550/600 ms.
- Springs (M3-expressive style): `spatialFast(stiffness 1400, damping 0.9)`,
  `spatialDefault(700, 0.9)`, `spatialSlow(300, 0.9)`, `effectsFast(3800, 1)`,
  `effectsDefault(1600, 1)`, `effectsSlow(800, 1)`. Spatial = things that move; effects = fades/color.

**Icon set.** ~44 Material Symbols glyphs (Apache-2.0) compiled to C++ path tables (§M2):
wifi(0-4 levels), signal levels, battery outline+bolt, bluetooth, airplane, dnd(moon),
flashlight, rotate-lock, dark-mode, brightness(low/high), volume(mute/low/high), settings(gear),
power, edit(pen), search, apps-grid, back-arrow, close, expand-more/less,
check, add, remove, person, notifications(bell), notifications-off, screenshot, cast,
location, hotspot, nearby-share, alarm, do-not-disturb-off, chevron-right, drag-handle,
fullscreen, split-screen, clear-all(delete-sweep), pin, star, folder, wallpaper, info.

### 2.3 Status bar

- Height **24** (v) `status_bar_height_default = 24dp`, `$A/frameworks/base/core/res/res/values/dimens.xml`. Horizontal padding 16 (m3ish; verify `status_bar_padding_start/end` in SystemUI dimens).
- Left cluster: **clock** `HH:mm` (respect locale 12/24h), Roboto Medium 14; then notification app-icon dots row (max 4 + "+N" overflow dot), icon size 17, spacing 4 (assumed; verify `status_bar_icon_size_sp`).
- Right cluster (order, right-aligned): battery percent text 12 Medium, battery glyph (Android-style **vertical** rounded body 8.5×14 with a 4×2 top cap; fill rises bottom-up with percent; bolt overlay when charging), wifi glyph (4-level quarter-arc fan 17px), bluetooth (when connected), dnd moon (when on), airplane (when on). Spacing 6.
- Colors: icons/text `onSurface` on light surfaces, `#FFFFFF` over wallpaper/dark — the bar is transparent over the launcher wallpaper (white icons + subtle 20% black top gradient scrim), `surface`-tinted with `onSurface` icons when a light app window is maximized beneath (see CompositorBridge `topWindowColorHint`, degraded default: always wallpaper mode over launcher, dark scrim mode over apps).
- Behavior: tap = no-op v1; swipe down on left/right half opens the respective shade (§2.4/2.5). The bar fades out when a window is fullscreen (compositor hint), fades back on edge-reveal swipe (M7).
- The bar reserves an **exclusive zone** of 24 so maximized apps sit below it (layer-shell `exclusive_zone`; GNOME track: extension sets struts).

### 2.4 Notification panel (left dual shade)

Pointers: (v) `$A/frameworks/base/packages/SystemUI/src/com/android/systemui/shade/` (ShadeModeInteractor, DualShadeFlag), `statusbar/notification/` (row/, stack/).

- **Trigger:** touch-down on top edge, x < 50% of screen width, drag down ≥ 24 → panel follows finger (its top tracks the drag with `spatialDefault` catch-up); release: open if dragged > 25% of panel height or fling velocity vy > 500 px/s, else spring back closed. Also `Super+N` keyboard shortcut.
- **Geometry:** width **420** anchored left with margin 12 from edges (screen < 600 wide: full-width minus 2×8). Panel background `surfaceContainerLow` @ 100%, radius **28** (v), Y offset 12 below bar. Behind it a full-screen scrim `scrim@32%` that also closes on tap. (Android uses blur; we ship scrim v1 — same fallback AOSP uses without blur support. Blur = future work, §8.)
- **Content, top to bottom:**
  - Header row (h 48): date "EEE, MMM d" label 14; right: DND chip + "Clear all" pill button (secondaryContainer, radius full, h 32) — visible only when dismissible notifications exist; animates in/out (fade+scale `effectsDefault`).
  - Notification cards in an AB-4 kinetic ScrollView. Card: radius 28, bg `surfaceContainerHigh` (dark: `surfaceContainerHigh`), padding 16, gap 8 between cards. Sections: "Incoming"/"Silent" label 12 Medium `onSurfaceVariant` with 12 top gap.
  - Card anatomy: 24 app icon (or image-data hint), app name 12, timestamp 12 `onSurfaceVariant`, title 14 Medium `onSurface`, body 14 `onSurfaceVariant` (2 lines collapsed), expand chevron when body/actions overflow. Expanded: full body + action buttons row (text buttons, label 14 Medium `primary`, h 36). Progress-hint renders a ProgressBar. Inline image (`image-data`/`image-path` hint) 40×40 right-aligned.
  - Empty state: centered bell-off glyph 40 + "No notifications" label 14 `onSurfaceVariant`.
- **Gestures:** vertical drag scrolls (kinetic, overscroll stretch — AB-4); horizontal drag on a card ≥ 56 or fling |vx| > 600 px/s dismisses (card slides out 200 ms `standardAccel`, siblings close the gap via `spatialDefault` spring; non-dismissible cards rubber-band back). Tap card = activate default action (D-Bus `ActionInvoked`) + close panel. Expand chevron toggles with 300 ms `emphasizedDecel` height animation.
- **Heads-up:** when panel closed and urgency=critical (or normal w/ hints), show top-center floating card (width 420, same anatomy, collapsed) sliding down from y=-h over 350 ms `emphasizedDecel`, auto-dismiss after 5 s (progress ring on the right), swipe-up dismisses, drag-down pulls the full panel open.
- **Backend:** our own `notifyd` implementing `org.freedesktop.Notifications` (§3.3). DND toggle suppresses heads-up + sounds, still stores.

### 2.5 Quick settings (right dual shade)

Pointers: (v) `$A/frameworks/base/packages/SystemUI/res/values/config.xml` `quick_settings_infinite_grid_num_columns = 4`; `src/com/android/systemui/qs/`, Compose QS under `compose/features/`.

- **Trigger:** same as §2.4 but x ≥ 50%. `Super+S` shortcut. Same panel geometry, anchored **right** (width 420, radius 28, bg `surfaceContainerLow`).
- **Header** (h 64): left big clock 28 Roboto Flex; right: battery percent + glyph, settings gear icon-button (opens system settings app via `.desktop`), power icon-button (menu: lock / suspend / restart / shut down — via logind, §3.3).
- **Brightness slider** (h 44, radius full): AB track with `brightness-low` icon; drag writes logind `SetBrightness` (§3.3). Then **volume slider** same style (PipeWire via libpulse).
- **Tile grid:** 4 columns (v), gap 8. Two tile sizes: large 2-col × h 64 (wifi, bluetooth — icon + label + secondary line) and small 1-col × h 64 (icon only, label 11 below inside). v1 tile set: WiFi, Bluetooth, Do Not Disturb, Airplane mode, Dark theme, Auto-rotate (desktop: disabled/grayed), Flashlight (grayed on desktop), Screenshot (portal), Night Light (per-desktop D-Bus), Cast (grayed v1), Hotspot (NM, stretch), Location (grayed v1).
  - States: active = bg `primaryContainer`, icon/label `onPrimaryContainer`; inactive = bg `surfaceContainerHighest`, `onSurface`; unavailable = 38% opacity, non-interactive.
  - **Shape morph on toggle** (M3-expressive): radius animates 16 → full-pill when activating (350 ms `spatialDefault` spring) and back; press-down scales 0.97 (`effectsFast`).
  - Long-press tile → opens the relevant settings page (`.desktop` deep link where available; else app).
- **Pagination:** grid pages horizontally if > 8 tiles (page dots, swipe with snap `spatialDefault`); "Edit" pencil in footer enters edit mode (v2 — v1 ships fixed order config file).
- **Open/close motion:** panel translates from -32 & fades 0→1 over 350/250 ms `emphasizedDecel`/`standardAccel` while the drag maps 1:1 during the gesture (expansion fraction = drag/panelHeight, clamp spring on release).

### 2.6 Launcher (home)

Pointers: `$A/packages/apps/Launcher3/` — `src/com/android/launcher3/` (Workspace, CellLayout, Hotseat, allapps/, folder/, popup/), profiles (v) `res/xml/device_profiles.xml` (grids from 3×3; phone default 5×5).

- **Wallpaper layer:** full-screen image (`drawImage`, Cover fit) + 12% black scrim for icon legibility. Ship one original CC0 default wallpaper asset. User-set via config file + QS "wallpaper" long-press v2.
- **Home grid:** 5 columns (v default profile), rows computed from height (cell 96×102); pages swipe horizontally (kinetic + snap, `spatialDefault`), page-dot indicator (4×4 dots, active pill 16 wide, slides between positions). Icons 56 + label 12 white with 40% black text shadow (1px blur approximation via offset dark copy).
- **Dock (hotseat):** bottom row of 5 icons, no labels, above the gesture pill; bg none (Android style).
- **App icons:** from GTK icon theme at 128 px → CPU-masked (circle/squircle per §2.2) → `registerImage`. Fallback: colored circle (hash of app id → tonal palette hue) + first letter glyph 24 Medium white.
- **Press feedback:** press-down scale 0.9 (`spatialFast` spring), release launch: icon scales up 1.1 + fades as the app opens (we cannot animate the real window from icon v1; do launcher-side zoom + rely on compositor map animation. v2: KWin script window-open-from-rect effect).
- **Long-press icon** (500 ms, AB-3): popup card (radius 28, `surfaceContainer`, elevation shadow AB-5) with rows: "App info" (opens `.desktop` details dialog v1: simple our-UI dialog), "Remove" (from home grid only), plus desktop-file `Actions=` entries (e.g. "New Window"). Drag while popup open = pick up icon for reposition (icon lifts: scale 1.1 + shadow, grid cells shift with springs).
- **All-apps drawer:** swipe up anywhere on home (or dock area) ≥ 25% height / fling vy < -500 px/s → sheet slides up over wallpaper: bg `surface@98%`, top radius 28 → 0 as it reaches the bar (interpolate with expansion fraction). Content: search field (h 52, radius full, `surfaceContainerHigh`, filters live), then 5-col alphabetical grid of all `.desktop` apps (kinetic scroll + right-edge A-Z fast-scroll rail with bubble). Close: swipe down past 25% / Back gesture / Esc.
- **Folders:** drag icon onto icon → folder (bg `surfaceContainerHigh@90%` circle preview with up-to-4 mini icons in 2×2). Open: expands to 4-col card (radius 28) centered via `spatialDefault` spring (grows from the folder icon rect — the "container transform" pattern); rename field on top. v1 storage: single JSON layout file `~/.config/arstro-android-shell/home.json` (grid positions, folders, dock).
- **Search:** the drawer search field matches `.desktop` Name/Keywords/Exec, results replace the grid live; Enter launches first hit.
- **Recents-as-home concepts** (app usage ordering in drawer): v2.

### 2.7 Gesture navigation, recents, fullscreen/split

Pointers: `$A/packages/apps/Launcher3/quickstep/src/com/android/quickstep/` (AbsSwipeUpHandler, MotionPauseDetector, RecentsView/TaskView), `$A/frameworks/base/packages/SystemUI/src/com/android/systemui/navigationbar/gestural/EdgeBackGestureHandler.*` + `BackPanelController` ; (v) `config_backGestureInset` key exists in `$A/frameworks/base/core/res/res/values/config.xml`.

All gesture surfaces are **invisible layer-shell overlay strips owned by the shell** (they sit above fullscreen apps): left edge 32 × full-height, right edge 32 × full-height, bottom 24 × full-width, plus the 24 status-bar band (already ours).

#### 2.7.1 Back (left/right edges)

- Touch-down in an edge strip → track horizontal pull. Draw the Android back-arrow affordance: a rounded "pill + chevron" that slides out from the edge, max protrusion 38 at pull ≥ 96 (rubber-band: protrusion = 38·(1−e^(−pull/96))); chevron glyph fades in past 50% protrusion; subtle scale pulse when the commit threshold crossed.
- Commit: release while protrusion > 24 → send **Back** (§3.2 `back()`: Esc-equivalent per bridge — Plasma: `qdbus` KWin emulated key? No — bridge injects `XF86Back`/Esc via compositor virtual-keyboard protocol; degraded: Alt+Left for browsers? v1 rule: synthesize `Esc`? — see §3.2.4 Back semantics, decided there). Cancel: drag back under threshold or vertical slop > 80 → arrow retracts (`spatialFast`).
- Vertical position of the affordance follows finger y. Both edges identical.

#### 2.7.2 Home + quick switch (bottom edge)

- **Pill:** 120×4 white/black 50% rounded-full, bottom-center margin 8, drawn in the bottom strip; auto-dims to 25% over fullscreen content.
- Swipe **up** from bottom strip: current window shrinks (we can't scale the real window v1 — instead show an immediate "app card" proxy: icon-centered rounded-rect 28 that tracks the finger with `spatialFast`, while the real window is hidden at gesture-start via bridge `minimize`/restore-on-cancel). Release: dy > 25% screen or vy < −700 px/s → **Home** (bridge `showDesktop` + launcher raises, proxy card flies into its icon grid slot); pause ≥ 150 ms with |v| < 50 px/s before release → **Overview** (§2.7.3); neither → cancel (window restores).
- Horizontal fling in bottom strip (|vx| > 600 px/s, |dx| > 48) without vertical intent → **quick switch**: activate previous/next window in MRU order (bridge `activate`), with a slide-crossfade proxy animation.

#### 2.7.3 Overview / recents ("select windows")

- Entry: pause-during-home-swipe, or bottom-strip swipe-up-and-hold, or `Super+Tab`.
- Full-screen shell surface over a `scrim@40%`. Cards: MRU row, centered current, side peek 24; card = 62% screen height rounded 28, header (app icon 24 + title 14 Medium) above; content v1 = **icon card** (large icon 96 on `surfaceContainerHigh`; honest limitation: Wayland denies cross-client pixels) — v1.5 per-desktop thumbnails (§3.2.5). Horizontal kinetic scroll with per-card snap; swipe a card **up** (dy > 30% or fling) → close that window (card flies up + fades, row closes gap); tap card → activate + dismiss overview.
- "Clear all" text button at row end → close every listed window sequentially.
- Card long-press/menu chip: "Split left" / "Split right" / "Fullscreen" / "Close".

#### 2.7.4 Fullscreen mode

- "Open maximized by default" policy (Android-like): bridge applies it (KWin window rules / GNOME extension `maximize` on map). Per-app opt-out list in config.
- True fullscreen (video etc.): app-initiated; our bars hide (strips remain). Swipe down from top edge while fullscreen → transient reveal of status bar for 3 s (Android immersive behavior).

#### 2.7.5 Split screen

- From overview menu (§2.7.3) or drag a card to left/right screen half → bridge `tilePair(A, B, 0.5)`; if only one chosen, remaining half shows overview to pick the second (Android's pair-picker flow).
- Divider: our overlay strip on the split boundary (w 24, centered grab handle 4×48 pill): drag → `setRatio` live (snap points 33/50/67); drag past 80% → dissolve split (loser un-tiles), matching Android.
- Session remembers one active split pair v1.

---

## 3. Linux architecture

### 3.1 Process & window model

One binary **`arstro-android-shell`** (GTK3 + `gtk-layer-shell` + Artboard/CairoTarget — GTK3 to match the proven cosmo host; gtk-layer-shell is the GTK3 layer-shell library) owning multiple toplevels:

| Surface | Layer-shell layer | Anchor / size | Exclusive zone | Keyboard |
|---|---|---|---|---|
| Launcher (wallpaper+home+drawer+overview) | `background`→raised to `top` when overview | full-screen | 0 | on-demand |
| Status bar | `top` | top, h 24 | 24 | none |
| Shade panels (notif/QS/heads-up) | `overlay` | full-screen input-transparent except panel rect (input region set per-frame) | 0 | on-demand |
| Edge strips ×3 | `overlay` | left/right w 32, bottom h 24 | 0 | none |

- Shared in-process `ShellState` (Artboard `Observable`s): theme mode, panel expansion fractions, notification store, tile states, window list. Surfaces are independent Artboard roots ticked by one GTK frame clock; render-on-demand (dirty flag) — idle = zero redraws (important on 4K).
- Input: GDK touch + pointer events → `RawPointer` (AB-3 adds `touch` flag, velocity, long-press, fling in the recognizer).
- Second binary **`arstro-android-notifyd`** only if the D-Bus name must outlive shell restarts — v1: in-process GDBus service in the shell (simpler), name `org.freedesktop.Notifications` (ownership rules §3.3).

### 3.2 CompositorBridge — the per-desktop seam

```cpp
// launcher/android-shell/compositor/CompositorBridge.h  (platform-free interface)
struct WindowInfo { std::string id, appId, title; bool active, fullscreen; };
class CompositorBridge {
public:
  virtual std::vector<WindowInfo> listWindows() = 0;      // MRU order
  virtual void activate(const std::string& id) = 0;
  virtual void close(const std::string& id) = 0;
  virtual void minimizeAll() = 0;                          // "home"
  virtual void setMaximizedDefault(bool) = 0;
  virtual void tilePair(const std::string& l, const std::string& r, double ratio) = 0;
  virtual void setRatio(double) = 0;  virtual void untile() = 0;
  virtual void back() = 0;                                 // §3.2.4
  virtual void requestThumbnail(const std::string& id, PngSink) = 0; // may no-op
  std::function<void()> onWindowsChanged;
  std::function<void(TopWindowHint)> onTopWindowHint;      // for bar tint / fullscreen hide
};
```

#### 3.2.1 Plasma / KWin track (primary)

- Surfaces: native layer-shell (KWin implements wlr-layer-shell; `gtk-layer-shell` works under Plasma 6).
- Bridge: a **KWin script** (`kwin-script/` package dir, JS/QML) loaded via D-Bus `org.kde.KWin /Scripting loadScript`; it exposes `org.arstro.AndroidShell.Compositor` on the session bus (KWin scripts can `registerDBus`/`callDBus`), implementing list/activate/close/tile (KWin 6 `workspace.windowList()`, `sendToScreen`, custom tiling via `window.tile` API) and emitting windowsChanged. Open-maximized via script intercepting `windowAdded`.
- Session (§7): custom wayland-session `android-shell-plasma.desktop` runs `kwin_wayland` **without plasmashell**, autostarting our shell (we own the whole chrome; org.freedesktop.Notifications is free for us). We keep `kded6`, `kglobalaccel`, powerdevil for battery/brightness plumbing.
- Suppress KWin's own edge gestures/effects in our session's `kwinrc` (ElectricBorders off, gestures off).

#### 3.2.2 GNOME track

- mutter has **no** layer-shell for third-party clients. Companion **GNOME Shell extension** (`gnome-extension/`, ESM JS, GNOME 46–49) that:
  1. Spawns/adopts our shell client via `Meta.WaylandClient` (the API GNOME itself uses to host X11-frames-like clients): `make_dock(window)` + `move_frame` to pin our surfaces at their §3.1 geometry, keep-above for overlays, input-region passthrough by splitting our overlay into the small strip windows (already our model).
  2. Implements the same `org.arstro.AndroidShell.Compositor` D-Bus API with `Meta.Window`/`global.display` (list/activate/close/`move_resize_frame` tiling, maximize-on-map).
  3. Hides stock chrome: `Main.panel.hide()`, hot-corner off, dash off — restores on disable.
  4. **Notification mirror:** GNOME Shell owns `org.freedesktop.Notifications` and that cannot be taken over in a GNOME session; the extension mirrors `Main.messageTray` sources to us over our D-Bus (add/remove/action-invoke round-trip), so §2.4 renders the same data. (In the Plasma session we own the name directly — same `NotificationStore`, two feeders.)
- Risk: `Meta.WaylandClient` is version-churny (§8). Degraded GNOME mode if it breaks on a release: launcher runs as a normal maximized always-below window, bars as extension-drawn thin St widgets that only *proxy open/close* commands to our panels rendered as normal windows. Defined, ugly, functional.

#### 3.2.3 Recents thumbnails (v1.5)

- KWin: `org.kde.KWin.ScreenShot2` per-window capture into a PNG fd (allowed for our whitelisted service in our own session config).
- GNOME: extension captures via `Shell.Screenshot.screenshot_window()` to a file and hands us the path.
- v1 ships icon-cards; this hook fills them in later without UI change.

#### 3.2.4 Back semantics (decision)

"Back" has no universal Linux meaning. Rule, in order: (1) if overview/drawer/panel/folder open → close it (shell-internal); (2) else inject `Alt+Left` if the focused window's app id is in the browser/file-manager allowlist (config); (3) else inject `Esc`. Injection: Plasma — KWin script `sendFakeKey` (or `org.kde.kglobalaccel` invoking a bound shortcut we register); GNOME — extension `Clutter.get_default_backend()` virtual keyboard. Document limitation plainly in README.

#### 3.2.5 System settings

We do not clone Android Settings. QS gear opens the desktop's settings (`gnome-control-center` / `systemsettings`). Long-press deep links use their `.desktop` panel args where stable.

### 3.3 System services (exact D-Bus surface)

| Need | Service | Key API |
|---|---|---|
| WiFi state/SSID/strength | NetworkManager (system bus) `org.freedesktop.NetworkManager` | `PrimaryConnection`→`ActiveConnection`→`SpecificObject`(AP)→`Strength`, `WirelessEnabled` r/w |
| Airplane | NetworkManager + rfkill | NM `WirelessEnabled`+`WwanEnabled`; full via `/dev/rfkill` (needs group perms; fallback NM-only) |
| Battery | UPower `org.freedesktop.UPower` `/org/freedesktop/UPower/devices/DisplayDevice` | `Percentage`, `State`, `TimeToEmpty` |
| Bluetooth | BlueZ `org.bluez` (system) | Adapter1 `Powered` r/w, Device1 `Connected` |
| Brightness | logind `org.freedesktop.login1.Session.SetBrightness("backlight", dev, v)` | unprivileged for the active session; enumerate `/sys/class/backlight` |
| Volume | PipeWire via **libpulse** compat (simplest stable C API) | default sink volume/mute + change events |
| Power menu | logind `Suspend(false)`, `PowerOff`, `Reboot`; lock via session `Lock()` | |
| Notifications | we serve `org.freedesktop.Notifications` (spec 1.2: `Notify`, `CloseNotification`, `GetCapabilities`, signals `NotificationClosed`, `ActionInvoked`; hints: urgency, image-data/path, actions, transient, resident) | Plasma session: own the name. GNOME: mirror (§3.2.2) |
| Dark theme | write portal setting: `gsettings color-scheme` (GNOME) / `plasma-apply-colorscheme` (KDE); read `org.freedesktop.portal.Settings` for our own theme follow | |
| Night light | GNOME `org.gnome.SettingsDaemon.Color`; KDE `org.kde.KWin.NightLight` | toggle tiles |
| Screenshot tile | `org.freedesktop.portal.Screenshot` | |
| Apps | GIO `GAppInfo`/`GDesktopAppInfo` (covers flatpak/snap exports), `GtkIconTheme` for icons, `g_app_info_launch` (Wayland activation token) | |

Every service client sits behind an interface with a **fake in-memory impl** for tests (§6): `SystemServices` aggregate injected into surfaces.

### 3.4 Repo layout (new code)

```
launcher/
  docs/android-theme-plan.md          (this file)
  android-shell/
    CMakeLists.txt                    (added to root CMakeLists via add_subdirectory)
    shell/        (main.cpp host, SurfaceHost, ShellState, StatusBar/, shade/, launcher/, recents/, gestures/)
    theme/        (AndroidColors, Type, Shape, Motion, icons/ generated)
    system/       (SystemServices + NM/UPower/BlueZ/Audio/Login1 clients + fakes)
    notifyd/      (org.freedesktop.Notifications GDBus service + NotificationStore)
    compositor/   (CompositorBridge.h, KWinBridge, GnomeBridge, NullBridge)
    kwin-script/  (JS + metadata.json)
    gnome-extension/ (extension.js, metadata.json, schemas)
    assets/       (fonts/, wallpaper/, icons-src/ SVGs + codegen script)
    packaging/    (debian/, rpm/arstro-android-shell.spec, sessions/, systemd-user/)
    tests/        (host-side golden tests; Artboard core tests stay in core/Artboard/tests)
```

`apps/launcher/plasma.android/` gets a `.gitignore` entry (never committed, never packaged).

---

## 4. Artboard changes (each = one `/implement_artboard` V-model cycle)

Existing capabilities already sufficient: gradients, `clipRect`, text + `measureText`,
raster images, tween/Animator/Spring/reduced-motion, Segment/snap/Row/Column, overlay pass,
hover (mouse), Observable, Theme. Gaps to fill, in order:

| ID | Change | HAL? | Draft requirement (paste into `core/Artboard/docs/requirements.md`) |
|---|---|---|---|
| AB-1 | `clipPath()` — intersect clip with **current path** (nonzero), scoped by save/restore, `beginPath` semantics unchanged | **Yes** → RecordingTarget + Cairo + Canvas2D all updated same change | **FR-26 Path clip primitive.** The render HAL shall provide `clipPath()` intersecting the current clip with the current path in the current transform space, scoped like `clipRect` (FR-11). Needed because rounded-rect/squircle content clipping (cards, icon masks) cannot be expressed by rect clips or fills. |
| AB-2 | Layer opacity group: `pushLayer(alpha)` / `popLayer()` | **Yes** (Cairo `push_group`/`pop_group` + `paint_with_alpha`; Canvas2D offscreen canvas + `globalAlpha`; Recording records both ops) | **FR-27 Opacity layer.** The HAL shall provide `pushLayer(alpha)`/`popLayer()` compositing all drawing between them as one group at `alpha`. A group fade of overlapping content cannot be expressed per-primitive (overlaps double-darken). Nestable; scoped independent of save/restore pairs but must be balanced. |
| AB-3 | Touch input: `RawPointer.touch` flag; recognizer gains **velocity tracker** (last-100 ms window), **`LongPress`** (500 ms, cancels on move > slop; requires new `GestureRecognizer::advance(nowMs)` the host ticks), **`Fling`** gesture on Up carrying `velocity` (emit when speed > 400 px/s); `Gesture.velocity` field; touch streams suppress hover routing | No | **FR-28 Touch gestures.** (velocity/fling/long-press/touch-source semantics as left — full text spelled out at implementation time, incl. defaults 500 ms / slop = dragThreshold / 400 px/s and reduced-motion-independence) |
| AB-4 | `ScrollView` kinetic fling + overscroll: consume `Fling` → exponential decay (Android-flinger-like, friction coeff tunable), rubber-band overscroll (offset compression) + stretch visual (scale transform approximation), scrollbar fade in/out (`effectsDefault`) | No | **FR-29 Kinetic scrolling.** ScrollView shall fling with framerate-independent decay, clamp with animated overscroll that never rests out of range, honor reduced motion (instant stop, no stretch). |
| AB-5 | `Elevation` helper: `drawShadow(t, roundedRect, dp)` composing existing linear+radial gradients (key+ambient two-pass) | No | **FR-30 Elevation shadow.** Core helper renders M3-style soft shadows from gradient primitives only; visually identical on every adapter. |
| AB-6 | Motion tokens: `anim/Motion.h` named easings (the 5 cubic-beziers §2.2 added to `Easing`) + named `Spring` presets (stiffness/damping table §2.2); `Spring` gains a configurable damping-ratio variant if the closed-form currently hardcodes critical damping | No (core) | **FR-31 Motion token set.** The framework shall expose the named easing/spring presets so apps share one motion vocabulary; presets honor reduced motion. |

Rules that bind every AB task: tests in `core/Artboard/tests/coreTests.cpp` via RecordingTarget
op-stream assertions, 100% line coverage on touched core files, docs/puml synced, HAL changes
land in **all** adapters in the same commit, commit to `main` (`Co-Authored-By` trailer).

The Android-specific look (tiles, cards, back-arrow, pill, icon masks) lives in
`apps/launcher/android-shell/theme+shell`, **not** in Artboard — Artboard only gains the six
generic capabilities above. Shell-side code follows the same everything-animates rules.

---

## 5. Milestones (implementation order; each ends green before the next starts)

Each milestone lists Definition of Done (DoD) and its test recipe (§6 levels L0–L4).

- **M0 — Artboard AB-1…AB-6.** One V-model cycle each, committed separately to Artboard `main`. DoD: `artboard_tests` 0 failed, 100% coverage on touched core, all adapters build (`cmake --build build`; web via `./build.sh --target linux-web-server`). Test: L0.
- **M1 — Shell host skeleton.** `arstro-android-shell` binary: GTK3 multi-toplevel + gtk-layer-shell wiring per §3.1 table, one GTK frame clock ticking all Artboard roots, GDK touch/pointer→RawPointer (AB-3 flags), per-surface dirty-flag rendering, `--windowed` debug mode (plain windows, no layer shell) and `--surface=N` single-surface mode for goldens. NullBridge + fake services wired. DoD: placeholder-colored surfaces at correct geometry in nested KWin (L2 recipe), drag box demo 60 fps, CPU ~0% idle. Test: L2 manual + L1 smoke golden.
- **M2 — `android_theme` module.** Color/type/shape/motion tables (§2.2), icon codegen: `assets/icons-src/*.svg` → build-time Python script (`svg-paths → theme/icons/*.h` cubic-path tables — flatten arcs to cubics), font vendoring + fontconfig registration, adaptive-icon masker (mask path + `clipPath` + `registerImage` pipeline), token sample-sheet screen. DoD: sample sheet golden PNGs (light+dark) committed as L1 baselines; icon codegen is a CMake step; `licenses/` ships Apache-2.0 notices. Test: L1.
- **M3 — Status bar + services.** §2.3 full spec against `SystemServices` (real NM/UPower/BlueZ/libpulse clients + fakes), tint modes, exclusive zone, shade-trigger hit bands emitting `ShellState.shadeDrag`. DoD: bar over nested-KWin wallpaper shows live clock/battery/wifi; goldens for {light, dark, charging, no-wifi, dnd} × {wallpaper-tint, surface-tint}. Test: L0 (service fakes), L1, L2.
- **M4 — Notification panel + notifyd.** §2.4 complete incl. heads-up + DND + dismissal physics. DoD: in an isolated bus (L2 recipe uses `dbus-run-session`), `notify-send` variants (urgency, actions, image, transient) render per spec; action round-trip works; goldens for {empty, 3-mixed, expanded, heads-up}; swipe-dismiss verified by scripted `RawPointer` replay test at host level (L0-style harness for shell code with RecordingTarget). Test: L0, L1, L2.
- **M5 — Quick settings.** §2.5 complete; tiles wired to services (fakes in tests, real in session); brightness/volume act on the isolated session only (nested compositor's env). DoD: goldens {closed→open frames at t=0/0.5/1, active/inactive/unavailable tiles, both themes}; tile morph verified via recorded op stream at 3 timestamps. Test: L0, L1, L2.
- **M6 — Launcher.** §2.6: wallpaper, grid+dock+layout JSON, drawer+search+fast-scroll, folders, long-press+drag re-arrange, icon pipeline with both masks. DoD: launches real apps in nested KWin via GAppInfo (activation token OK); goldens {home, drawer-mid-open (t=0.5), drawer-open, folder-open, search-results}; drag-rearrange replay test. Test: L0, L1, L2.
- **M7 — Gestures + recents + split (KWin bridge).** §2.7 with KWinBridge + kwin-script D-Bus (list/activate/tile/ratio/back-inject), edge strips, pill, overview (icon cards), split divider, open-maximized rule, fullscreen bar hide/reveal. DoD: on nested KWin with 3 test windows — back arrow spec-complete (goldens at protrusion 0/50/100%), home/overview/quick-switch thresholds honored (replay tests), split 50:50→drag→67:33 works, `kwinrc` gesture suppression documented. Test: L1, L2 (+ scripted `ydotool`/`wtype` input where replay insufficient).
- **M8 — GNOME track.** Extension (§3.2.2): WaylandClient hosting, D-Bus bridge parity, chrome hiding, notification mirror; degraded mode implemented + documented. DoD: same M3–M7 smoke checklist passes on nested `gnome-shell --nested --wayland` (L3) within its known input limits, and fully in a GNOME VM (L4); extension passes `gnome-extensions pack`/EGO lint. Test: L3 + L4.
- **M9 — Sessions + packaging.** §7: wayland-session files, systemd user units, deb + rpm (CPack first; debian/-native + spec kept in `packaging/` for later polish), postinst glib-compile-schemas etc. DoD: clean VM matrix — Ubuntu 24.04 (GNOME), Fedora 41+ (GNOME), Fedora KDE, Kubuntu — install package → log into "Android Shell" session → 15-point smoke checklist (each §1.1 deliverable exercised) passes; uninstall leaves stock session intact. Test: L4.

Milestone discipline for the implementing model: work strictly in order; every UI item follows
the shell-side everything-animates rules; any Artboard change discovered mid-milestone becomes
a new AB-n mini-cycle (never inline hacks in adapters).

---

## 6. Testing & isolation (answer: yes — fully isolated from this machine's GNOME)

Nothing below touches the host session; the host desktop is never restarted, its D-Bus
never sees our services, its config never written.

- **L0 — Unit (no display):** Artboard core via RecordingTarget (as always); shell logic gets the same treatment — surfaces render into RecordingTarget with fake `SystemServices`/NullBridge, gesture flows driven by scripted `RawPointer` streams (velocities/timestamps synthesized). Runs in CI/anywhere.
- **L1 — Golden images (no display):** render each surface state through **CairoTarget → `cairo_image_surface` → PNG** headlessly (proven technique — see memory note "Headless render of Artboard apps"), compare against committed baselines with small tolerance; regenerate via `tests/update-goldens.sh`. Covers all "does it look like Android" checks without any compositor.
- **L2 — Nested KWin (isolated live session):** `dbus-run-session -- env XDG_RUNTIME_DIR=$(mktemp -d) kwin_wayland --width 1600 --height 1000 --no-lockscreen ./arstro-android-shell` — a window on the dev desktop containing the full shell; private session bus (our notification daemon can't clash with GNOME's), private runtime dir. Mouse acts as touch v1 (AB-3 treats single pointer uniformly); scripted input via `wtype`/`ydotool` pointed at the nested compositor. Used from M1 on.
- **L3 — Nested GNOME:** `dbus-run-session -- env HOME=$(mktemp -d) MUTTER_DEBUG_DUMMY_MODE_SPECS=1600x1000 gnome-shell --nested --wayland` with the extension installed into the scratch `HOME`. Known limits (input quirks, single fake monitor) — acceptance happens in L4.
- **L4 — VMs (full-fidelity, still isolated):** virt-manager/quickemu VMs — Ubuntu 24.04 GNOME, Fedora GNOME, Fedora KDE, Kubuntu — with snapshots ("fresh install" restore point) for repeatable package tests; qemu `-device virtio-tablet-pci` gives absolute pointer ≈ single-touch; optional real-multitouch pass by USB-passthrough of a touch monitor/tablet at the very end (only step involving real hardware, still not the host's session).
- **Caveat (the honest bit):** multi-finger gestures don't exist in v1's spec (nothing above needs >1 finger), so pointer-as-touch covers the matrix; final tactile feel (velocity tuning) deserves one optional pass on a real touchscreen VM/passthrough at M9. If that hardware never materializes, tuning constants stay at Android's reference values — acceptable.

---

## 7. Packaging

- **Artifacts:** `arstro-android-shell` (binary + theme assets + fonts + wallpaper, `/usr/bin`, `/usr/share/arstro-android-shell/`), `arstro-android-shell-plasma` (kwin-script, `android-shell-plasma.desktop` in `/usr/share/wayland-sessions/`, session launcher script + `kwinrc` profile, systemd user units `arstro-android-shell.service` bound to the session target), `arstro-android-shell-gnome` (extension into `/usr/share/gnome-shell/extensions/android-shell@arstro/`, optional `android-shell-gnome.desktop` session variant enabling the extension via a dconf profile).
- **Deps:** Debian: `libgtk-3-0t64, libgtk-layer-shell0, libcairo2, libglib2.0-0t64, libpulse0, fontconfig`; Fedora: `gtk3, gtk-layer-shell, cairo, glib2, pulseaudio-libs, fontconfig`. Build: `cmake, g++-13+`, python3 (icon codegen).
- **Tooling:** CPack `DEB` + `RPM` generators from the same CMake install rules (v1, lowest maintenance); keep `packaging/debian/` + `.spec` skeletons for future distro-native builds. CI job builds both on every tag.
- **Guards:** install rules must whitelist paths explicitly — `apps/launcher/plasma.android/` (153 GB) and `$A` assets must be impossible to glob into a package.
- Licensing: our code under repo license; ship `NOTICE` for Roboto/Roboto Flex/Material Symbols (Apache-2.0); wallpaper CC0-original.

## 8. Risks & fallbacks

| Risk | Mitigation |
|---|---|
| GNOME `Meta.WaylandClient`/extension API churn per release | Pin `metadata.json` versions per GNOME major; degraded GNOME mode (§3.2.2) always kept working; Plasma track is the flagship |
| Wayland forbids cross-client thumbnails | v1 icon-cards (spec'd); per-desktop screenshot hooks in v1.5 (§3.2.3) |
| "Back" has no universal semantic | Explicit rule + allowlist (§3.2.4), documented |
| Blur behind shades not portable | Scrim (AOSP's own no-blur fallback); optional later: KWin blur-behind hint on our surfaces (Plasma-only bonus) |
| Full-screen Cairo repaints on 4K | Dirty-flag per surface, panels are the only animating surfaces (420 px wide), launcher static when idle; profile at M1, budget: <4 ms/frame @1600px nested |
| KWin/GNOME gesture conflicts | Session-level suppression (kwinrc / extension), documented |
| notifyd name clash outside our sessions | Own the name only in Plasma session; GNOME uses mirror; running shell on a stock session skips notifyd (panel shows mirror/empty state) |

## 9. Deferred decisions (safe defaults chosen, revisit when asked)

Default icon mask **circle** vs squircle (setting exists either way); dynamic wallpaper-seeded
color (M8+, monet port); QS edit mode (v2); widget system (excluded); lock screen (excluded —
sessions use the DM/stock lockers); per-monitor multi-display layout (v1: primary monitor only,
others get wallpaper + bar without panels).
