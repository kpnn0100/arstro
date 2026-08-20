# cosmo — Phone (Android) UI design brief for Figma

A complete component + interaction spec for a **small‑phone** version of the cosmo photo editor, to
hand to Figma AI.

> **AMENDED 2026-08-20 by R-TOUCH (see [`../REQUIREMENTS.md`](../REQUIREMENTS.md)).** Three things in
> this brief are superseded, and where they disagree the requirement wins:
> 1. **The tray no longer rises OVER the photo.** R-TOUCH-2 forbids any component overlapping
>    another: opening the tray **shrinks the photo's box** instead. On a phone the photo is the work,
>    and a sheet across it hides the thing being edited. Overlays are only modal ones (sheets,
>    dialogs, drawer, the fullscreen curve editor), each scrimmed.
> 2. **Landscape is no longer "future".** R-TOUCH-3 requires both orientations: portrait stays this
>    one column; landscape becomes **two panes** — photo left, the active tray a fixed panel on the
>    right — which is the desktop's shape at phone size, so it needs no new interaction model.
> 3. **Curves get a fullscreen editor with a loupe** (R-TOUCH-4), not a curve inside the tray's Full
>    detent: the plot takes the whole screen so nodes are far apart, the grab radius is ≥24 dp, a tap
>    selects the *nearest* node rather than requiring a hit, and a magnifier offset above the finger
>    shows the node the fingertip is covering.
>
> Everything else here — the visual language, the gesture mapping table, the component inventory, the
> tokens, the motion scale — stands unchanged. Target device is a **~6‑inch phone in portrait** (design at **393 × 852 dp**, must
also hold at **360 × 780 dp**). It replicates the desktop cosmo feature‑for‑feature — **nothing is
dropped** — but the layout is rebuilt for one narrow column: the photo is the hero, and every panel
that was docked on desktop becomes a **bottom tray / sheet / drawer** that rises over the photo.

Read top to bottom: §1 visual language · §2 the phone architecture (how the desktop's 3 zones become
one column) · §3 the touch gesture model · §4–§13 every screen & component with states · §14 the full
component inventory · §15 tokens · §16 motion · §17 Figma prompt notes.

> Scope note: design **one layout — the phone**. It may later scale up to tablets, but do NOT design a
> tablet frame now. Every component must work at 360–430 dp wide.

---

## 1. Design language (identical to desktop — do not restyle)

- **Theme:** one dark, near‑black surface family. Background `#141414`; card/panel/tray `#1C1C1C`;
  popover/menu `#222222`; input field (dark) `#252525`; elevated stage `#0A0A0A` (photo backdrop).
- **Accent (single):** `#4F7EF7` (blue) — ONLY for interactive/selected state: active tab, selected
  segment, slider fill, selection ring, primary button, caret. Never decorative.
- **Stacked/"final" green:** `#4CB573` — ONLY to show a group‑stacked value (see §9): the green reach
  on sliders and the faint "final" curve behind curve editors.
- **Neutrals:** foreground text `#DBDBDB`; muted `~#8A8A8A`; hairline border `rgba(255,255,255,0.072)`.
- **Light entry field** (rename / text input island): near‑white `#F4F4F5` bg, near‑black `#18181B`
  text — a deliberate light island in the dark UI.
- **Radii:** `1px` (hairline: segmented pickers), `2px` (control: buttons, chips, cards, trays),
  `9999px` (pill: slider track/thumb, Before/After pill). ONE radius scale.
- **Type:** UI sans = **Roboto** (regular/medium/semibold, R-FONT-2); numerics & filenames = **JetBrains Mono**.
  Wordmark "cosmo." = semibold, letter‑spacing −0.03·size, the "." in accent.
- **Density:** desktop is very compact. On the phone, **keep the tight visual rhythm but grow hit
  zones** (§3) and step type up ~1.15–1.3× for legibility at arm's length. The look stays tight; the
  touch bands grow invisibly.
- **Motion:** everything animates, nothing snaps (§16). Ease‑out, 120–520 ms by scope.
- **Icons:** stroke‑only lucide‑style line glyphs.

---

## 2. Phone architecture (how desktop's 3 zones collapse into one column)

Desktop cosmo is **Left rail | Center stage | Right edit panel**, all visible at once. A 6‑inch phone
can't show all three, so the model becomes **photo‑hero + on‑demand trays**:

```
┌───────────────────────────────┐  393 × 852 dp
│  Top bar            48 dp      │  back · name · undo/redo · ⋯
├───────────────────────────────┤
│                               │
│         PHOTO CANVAS          │  the hero — always visible
│         (fills, Contain)      │  overlaid: Before/Split/After pill,
│                               │  zoom badge, mask handles
│                               │
├───────────────────────────────┤
│  Breadcrumb (thin, scrolls)   │  ~24 dp
│  Filmstrip (horiz scroll)     │  ~72 dp  (collapsible)
├───────────────────────────────┤
│  Tool bar: 5 tabs   ~56 dp    │  Basic·Mask·Color·Grade·Xform
└───────────────────────────────┘
```

- **The three desktop zones map to:**
  - *Left rail / presets* → a **left slide‑over drawer** (hamburger/panel‑left in the top bar) or a
    full‑screen "Presets" sheet.
  - *Center stage* → the **permanent** photo canvas + breadcrumb + filmstrip (the only always‑on UI).
  - *Right edit panel* → the **bottom tool bar (5 tabs)**; tapping a tab raises a **control tray** from
    the bottom over the lower part of the photo (see §8). This is the core editing loop.

- **The control tray** (the single most important phone pattern): a `#1C1C1C` sheet that slides up from
  the bottom edge with a **grab handle** at its top. Detents:
  - **Rail** (closed): just the 5‑tab tool bar; photo maximized.
  - **Half** (~42% height): the tab's controls scroll inside; the photo stays fully visible above,
    shifted/scaled up so nothing important hides behind the tray.
  - **Full** (~88% height): for dense panels (all Basic sliders) and the big **curve/mixer editors**; a
    thin **live photo preview strip** stays pinned at the very top so you still see edits.
  - Drag the handle to move between detents; flick/drag down past Rail dismisses to the tool bar.

- **One tray at a time.** Opening presets/history/settings/dialogs uses full‑screen sheets or centered
  modals (§13). The photo canvas + top bar are the only things that persist across everything.

- **Safe areas:** respect the status bar (top) and the gesture/nav bar (bottom). Pin the tool bar and
  action buttons above the bottom safe inset; the top bar below the status bar.

- **Orientation:** portrait is the design target. (Landscape may simply widen the canvas and float the
  tray on the right — note it as future, don't design it now.)

---

## 3. Touch interaction model

No mouse hover, no right‑click, no physical keyboard. Map every desktop interaction:

| Desktop | Phone equivalent |
|---|---|
| Hover highlight | **Press** wash/ripple on touch‑down + a persistent **selected** state. Design idle / pressed / selected / disabled — NO hover‑only state. |
| Left‑click | **Tap** |
| Double‑click (drill group, reset slider, add/remove curve node) | **Double‑tap** |
| Right‑click (context menu on photo / filmstrip cell / group / preset) | **Long‑press** (~450 ms) → a **bottom action sheet** of the same items |
| Click‑drag (slider, curve node, mask handle) | **One‑finger drag** with an enlarged grab radius |
| Ctrl + wheel zoom about cursor | **Pinch‑to‑zoom** about the pinch midpoint (1×–8×) |
| Pan while zoomed | **Two‑finger drag** to pan (one‑finger stays free for on‑canvas mask editing) |
| Alt‑drag a curve node to pull tangent handles | **Select node (tap) → smooth toggle + drag handles**: tapping a node selects it and reveals a small corner↔smooth chip; toggling smooth shows its tangent handles to drag. Replaces Alt. (Alt: long‑press a node toggles corner↔smooth.) |
| Alt‑drag a handle to break in/out symmetry | A **"link handles" toggle** on the selected node (default linked/symmetric; off = independent). |
| Keyboard Undo/Redo (Ctrl+Z/Y) | **Undo/Redo buttons** in the top bar |
| Keyboard Save/Save As (Ctrl+S) | Save in the tray action bar / overflow sheet |
| open (o) / export (s) / Delete keys | On‑screen buttons / action‑sheet items |
| Text entry (rename, search, preset name) | On‑screen keyboard + focused field; commit = keyboard **Done**/Enter, cancel = tap‑away / Android back |

**Touch targets:** every interactive element ≥ **44 × 44 dp** hit zone (the visual can be smaller).
Slider rows ≥ **48 dp** tall touch band (track looks thin). Curve/mixer node grab radius ≥ **24 dp**.
Tabs/chips/list rows ≥ 44 dp tall. Space adjacent targets ≥ 8 dp apart.

**One‑handed reach:** put the highest‑frequency actions (tab bar, tray controls, action buttons) in the
**bottom third**; keep the top bar for low‑frequency chrome (name, undo/redo, overflow).

**Feedback:** every tap responds this frame (press state); anything that can't finish in‑frame (decode,
export) shows a determinate progress / spinner / skeleton and never freezes.

---

## 4. Screen: Home / launcher

Full‑screen, single scrolling column (desktop's 300 px sidebar + recents grid stack vertically):

- **Header block** (top): **wordmark** "cosmo." (~40 px) + one tagline line.
- **Primary actions (3):** `New Project`, `Open Project…`, `Import Catalog…` — full‑width tappable rows
  with a leading line‑icon. New = accent‑emphasized; others ghost/outline.
- **Reserved links (inert, styled):** Settings · What's New · Help & Documentation · version string
  (muted, bottom). Draw them; they do nothing yet.
- **Recents header:** "Recent Projects" + live **count** + a **search field** (focusable; filters cards
  by case‑insensitive name substring; empty result → empty‑state text).
- **Project cards:** a **1‑column (wide) or 2‑column** responsive grid of cards. Each card: 16:9 cover
  thumbnail (first image; skeleton until decoded), project name, metadata line (photo count · total
  size · last‑opened), an "edited" dot if applicable. States: idle / pressed (no hover). Tap = open →
  transition (§5). **Long‑press** = action sheet (Open / Remove from recents).
- **"New Project" card:** dashed‑outline + card at the grid end.

Tap New/Open/Import → Android **SAF** file/folder picker → editor.

---

## 5. Loading / open transition (3 phases — signature, keep it)

Full‑screen animated transition on a near‑black **star‑sky** (`#141414` + faint twinkling white
particles):
1. **Intro (~460 ms, no I/O):** the "cosmo." wordmark flies from the header toward a small top‑bar‑sized
   target; the tapped cover lifts from its card toward center; the project name grows; stars fade in.
2. **Loading (progress):** a thin accent **progress bar** fades in and fills with real decode progress;
   the cover fades in; min ~260 ms so it never just flashes.
3. **Reveal (~520 ms):** the editor materializes on the same dark backdrop while cover/stars/name/bar
   fade out; the wordmark cross‑fades into the top‑bar slot (never doubles).

**Return** (leave project → home) reverses it: editor → star‑sky → brief hold (home rebuilds behind) →
home fades in; wordmark flies back to the header.

Design: star‑sky bg, the flying wordmark at 2 sizes (home 40 px → top‑bar ~15 px), progress bar,
growing name, cover‑lift frames.

---

## 6. Editor shell (chrome)

### 6.1 Top bar (~48 dp)
Left → right: **back/home** arrow (→ save‑or‑discard confirm §13, then home); **project name** (center,
truncates with …); **Undo** / **Redo** (disabled when unavailable); **overflow (⋯)**. A **panel‑left**
icon (open the preset drawer §6.3) sits at the far left next to back, or inside the ⋯ sheet if space is
tight.
When a **group** is the edit target, replace the center name with **"Group: <name>"** as a tappable,
pressed‑state affordance → rename (§9.3).

### 6.2 Menu → overflow (⋯) sheet
The desktop MenuStrip collapses into a single **bottom sheet** grouped by section (tap a header to
expand, or show as one scroll list):
- **File:** Home · Open… · Save · Save As…
- **Settings:** Engine Settings… (§13.2) · Reset Workspace (confirm)
- **Develop:** Copy Settings · Paste to Selected · Paste to All Images · Group Selection · Ungroup
  Selection
- **History:** Undo · Redo · Show History Tree… (§11)
- **Preset:** Save Preset… · Import Preset…

### 6.3 Left preset drawer (desktop LeftRail + PresetTree)
A **slide‑over drawer from the left** (or full‑screen sheet), header "PRESETS", body = a **scrollable
preset tree**:
- **Row:** indented by depth; folder rows show a chevron (right=collapsed, down=expanded) + name; leaf
  rows show the preset name; selected row gets an accent fill/rule; ≥ 44 dp tall.
- Tap a folder = expand/collapse (eased). **Double‑tap a leaf = apply** to the current image.
  **Long‑press a row** = action sheet (e.g. Delete preset).
- Drawer open/close animates offset (~240 ms); scrim behind; tap scrim / swipe‑left / back closes.

---

## 7. Center stage: photo canvas, breadcrumb, filmstrip

### 7.1 Photo canvas (the hero — always visible)
- `#0A0A0A` backdrop; image **Contain**‑fit (letterboxed).
- **Before / Split / After** pill (segmented, 3) floating bottom‑center of the canvas: After = edited;
  Before = geometry‑only baseline; Split = before clipped to the left half with a 1.5 px accent seam.
- **Zoom/pan:** pinch to zoom about the pinch center (1×–8×); two‑finger drag to pan; before/after share
  one zoom/pan (seam stays aligned). **Double‑tap toggles fit ↔ 100%.** A small **zoom badge / reset**
  chip appears while zoomed. Reset to fit on image change.
- When the tray is open at Half, the canvas scales up to stay fully visible above it.
- States: empty (no image / skeleton) · loaded · zoomed.

### 7.2 Mask overlay (only when a mask is selected in the Mask tab)
Interactive geometry over the fitted photo (respects zoom/pan); **one‑finger drag edits geometry, never
pixels**:
- **Radial:** ellipse with a **center handle** (move) + **edge handles** (resize).
- **Linear:** two parallel boundary lines with **endpoint handles** (move/rotate/space).
- **Brush:** drag paints coverage **dabs** (show dab circles) + a brush‑size control.
Handles = accent dots outlined in the backdrop color, ≥ **28 dp** for touch. No mask selected → overlay
absent/click‑through (canvas gestures = zoom/pan).

### 7.3 Breadcrumb (~24 dp, thin)
One horizontal line: group path (root → … → current group) + the edited filename as the trailing crumb.
Every crumb but the last is muted + tappable, chevron separators; tap = navigate to that group.
Horizontally **scrollable** when long.

### 7.4 Filmstrip (~72 dp, horizontal scroll, collapsible)
- **Photo cell** (~78×58): thumbnail; a **sliding accent ring** marks the selected cell (eased 200 ms);
  small name/index.
- **Group chip** (~72×58): dashed folder chip with group name + child count.
- Interactions: **tap** = select; a **multi‑select mode** (long‑press to enter, or a select toggle)
  supports multi/range select for Group/Paste/Delete; **tap while multi‑selecting** = toggle.
  **Double‑tap a group chip** = drill in (navigate). **Long‑press** (incl. empty space) = action sheet:
  Add Photo · Group Selection · Ungroup Selection · Rename Group (on a group) · Delete.
- Can collapse (a chevron / drag) to give the canvas more height; scroll eases (180 ms).

---

## 8. Edit panel = bottom tool bar + control trays (the core editing loop)

The desktop right panel becomes a **bottom tool bar of 5 tabs** that raise a **control tray** (§2). The
tray is `#1C1C1C`, welds to the tab bar, and every edit updates the render immediately.

### 8.1 Tool bar (5 tabs, ~56 dp, pinned bottom)
`Basic/Detail` · `Mask` · `Mixer/Curve` · `Grade` · `Xform`, each an **icon + short label**; scrolls
horizontally if labels don't fit. A 2 px accent bar / filled pill marks the active tab. Tapping a tab
raises its tray (Half); tapping the active tab again lowers it. Switching tabs cross‑fades the tray
body (180 ms).

### 8.2 Tray header (shared)
Grab handle (drag between detents) · the tab title · a **histogram toggle** (the histogram is expensive
of space on a phone, so it's a **collapsible strip** at the tray top, ~72 dp, RGB + luminance, log‑
scaled, live) · a **detent/expand** affordance · action buttons where relevant.

### 8.3 Basic/Detail tray (many sliders — needs sub‑navigation)
Because there are ~20 sliders across 7 sections, do NOT dump them in one long scroll only:
- Pin a **section chip row** at the tray top (horizontal scroll): **TONE · COLOUR · PRESENCE · EFFECTS ·
  SHARPEN · NOISE · LENS**. Tapping a chip scrolls to / filters that section; the active chip is
  accented.
- Below it, the sliders of the (scrolled‑to) sections as **full‑width slider rows** (§10). At **Half**
  detent ~4–5 rows show; drag to **Full** for the whole list.
- **Sections & rows** (every row bipolar unless noted; ranges as desktop):
  - **TONE:** Exposure · Contrast · Highlights · Shadows · Whites · Blacks
  - **COLOUR:** Temperature (track = blue→amber ramp) · Tint (green→magenta ramp) · Vibrance · Saturation
  - **PRESENCE:** Texture · Clarity
  - **EFFECTS:** Dehaze · Grain Amount · Grain Size
  - **SHARPENING:** Amount · Radius · Masking
  - **NOISE REDUCTION:** Luminance · Colour
  - **LENS:** Distortion · Chromatic Aberration · Vignette
- *(Optional touch upgrade, note as optional):* tapping a slider's **label** promotes it to a single
  **focused slider** docked at the very bottom with a big value + wide track, so you drag it while the
  photo is maximally visible — mobile photo-editor style. Keep the scroll list as the default.

### 8.4 Mixer/Curve tray (opens tall/Full — needs a big plot)
Two stacked editors in one scroll (Mixer above, Curve below), each expandable:
- **Mixer** (per‑channel HSL): a **Hue / Sat / Lum** segmented picker + a **cyclic hue‑curve editor**
  (one per channel; the picker swaps which shows) + **Reset**. See §12.
- **Tone Curve:** an **RGB / R / G / B** segmented picker + a **tone‑curve editor** + Reset. Four
  independent curves (RGB master applied to all, then per‑channel R/G/B). See §12.
- On a phone the plot should be **as wide as the tray and tall** — this tray opens at **Full** with the
  live photo strip pinned on top. Consider showing one editor at a time via a top toggle (Mixer | Curve)
  so each gets full width.

### 8.5 Grade tray
- **Region** segmented picker: **Shadows / Midtones / Highlights**. For the active region: **Hue**
  (0–360), **Saturation** (0–100), **Luminance** (−100…100). *(A colour wheel is a strong touch upgrade
  and space‑efficient on a phone — offer wheel + a Lum slider; desktop uses 3 sliders. Optional.)*
- **Balance** slider (−100…100).
- **Hue‑range remap:** a **toggle switch** + when on: **Source** hue (0–360), **Range** (0–180),
  **Target** hue (0–360), **Strength** (0–100).

### 8.6 Xform tray
- **Rotation:** a **scrub strip / slider** (−45…45°, ~0.15°/px feel) with a numeric readout.
- **−90° / +90°** quarter‑turn buttons + a **reset‑rotation** icon button.
- **Aspect chips:** Free · 1:1 · 4:3 · 16:9 · 3:2 · 5:4 → set a centered normalized crop.
- Flip / Auto rows drawn but **inert/greyed**.
- *(Optional touch upgrade:)* a **draggable crop rectangle overlay** on the photo — the touch‑native way
  to crop; desktop sets crop via aspect chips only.

### 8.7 Mask tray
- **Add chips (3):** Radial · Linear · Brush → add that mask, then edit geometry on‑canvas (§7.2).
- **Mask selector:** a dropdown/list of existing masks → select to edit.
- **Inv** (invert) toggle · **trash** (delete selected mask).
- **Feather** slider (0–100).
- A **Basic‑style adjust block** for the mask (Tone / Colour / Presence rows) applied through the mask.
- Per‑mask controls hidden until a mask is selected (empty state = add chips + hint).

### 8.8 Action bar (pinned bottom of the tray)
Three buttons: **Save** (accent‑filled) · **Import** · **Export** with save / upload / download icons
(wired to Save/Import/Export **Preset**). ≥ 44 dp tall, above the bottom safe inset.

---

## 9. Groups, stacking & the green "final" indicators (replicate exactly)

cosmo organizes images in a recursive **group tree**, and **a group is itself an editable item**.

### 9.1 Group as an editable item
- Groups appear as chips in the filmstrip and crumbs in the breadcrumb.
- **Selecting a group** (single tap its chip — not double‑tap, which drills in) makes the **whole edit
  tray edit the GROUP's own settings** (top bar shows "Group: <name>"); a representative member previews
  live in the canvas so you see the group's effect.
- A group has the **same full settings** as an image (all Basic/Detail/Mixer/Curve/Grade + masks).

### 9.2 Stacking (recursive) + the green reach
A group's settings **stack additively onto every member**, recursively (child→root). e.g. group
Exposure +1 adds 1 to every member's exposure.
- **On every slider**, when the edited item sits inside groups, show a **green (`#4CB573`) reach**: the
  thumb marks the item's OWN value; a green bar extends from the thumb to `own + Σ(group contributions)`
  with a thin green **end tick** = the **effective/final** value. Negative reaches left. No groups → no
  green.
- **On the curve/mixer editors**, draw the **effective ("final") curve faint green behind** the editable
  curve = the item's curve summed with the groups' (`item + group − identity` per axis). Updates live as
  you edit. No group contribution → no green line.

### 9.3 Group rename (in‑app, animated) — two entry points
- **Long‑press a group** in the filmstrip → action sheet → **"Rename Group"**: the sheet **morphs** — its
  items collapse to a single "Rename" header, then an inline **light textbox** (near‑white `#F4F4F5` bg,
  near‑black text) grows below, focused with the current name **selected** (first keystroke replaces).
  Enter/Done commits; tap‑away / back cancels.
- **Tap "Group: <name>"** in the top bar → opens the same rename box at that spot.

### 9.4 Group operations (overflow Develop + filmstrip long‑press)
Group Selection · Ungroup Selection · Delete · Copy Settings · Paste to Selected · Paste to All Images.

---

## 10. Slider row (the most‑repeated control — get it perfect)

Full‑width on the phone; ≥ 48 dp tall touch band:
- **Label** (left, muted) + **value readout** (right, mono, signed int, e.g. `+37` / `-12`).
- **Track:** a pill; **bipolar** — when the range spans zero, the accent fill grows from the CENTER
  (zero) outward (right for +, left for −), not from the left edge. Temperature/Tint tracks render a
  **colour gradient** instead of a solid track.
- **Thumb:** a round knob at the value (enlarged for touch).
- **Green stacked reach** (§9.2): thumb → effective value + thin end tick, `#4CB573`. Hidden when zero.
- Interactions: **drag** the track/thumb to set (drag‑to‑set; a tap on the row does NOT jump the value —
  prevents mis‑taps); **double‑tap** resets to default. *(Optional: long‑press enters a fine/precision
  drag mode.)*
- States: idle / pressed(dragging) / disabled.

---

## 11. History (branching undo/redo)

- **Undo / Redo** in the top bar (§6.1), operating on the current edit target (image OR group).
- **Show History Tree…** (overflow) → a **full‑screen sheet** with a git‑log‑style **branching tree** of
  edit nodes (each node = a saved state; branches when you edit after undoing). Current node
  highlighted. **Tap a node** = jump to it; **drag** = pan the tree; **pinch** = zoom the tree (phones
  need it); close‑X / back = close. Design: nodes in lanes, connectors, labels, the current‑node marker.

---

## 12. Curve & mixer editors (bezier, per‑channel, reference line)

Both editors share the SAME bezier model and UX (hard requirement — they must match). On the phone they
open in a **Full‑detent tray** so the plot is large; the live photo strip stays pinned on top.

- **Plot:** rounded dark plot with faint gridlines. Tone curve = quarter grid + a faint identity
  diagonal. Hue‑curve (mixer) = a zero line + 60° gridlines + a **48‑swatch hue strip** below; the
  Hue‑channel line is coloured by output hue.
- **Nodes:** points are **corners** (straight segments) by DEFAULT — no auto‑smoothing.
- **Make it smooth (spline):** replaces desktop Alt‑drag — **tap a node to select**, then toggle it
  smooth (a small corner↔smooth chip on the selected node, or long‑press) and **drag its tangent
  handles** (two accent dots + lines appear) to shape the spline; a **link toggle** keeps in/out handles
  symmetric (default) or independent.
- **Add / remove:** **double‑tap** empty plot = add a corner node; **double‑tap a node** = remove it
  (endpoints can't be removed).
- **Move a node:** drag it. Endpoints **locked in x** (0 and 1); interior nodes clamp between neighbours.
  Node grab radius ≥ **24 dp**.
- **Per‑channel (tone curve):** **RGB / R / G / B** picker selects which of 4 independent curves shows;
  the spline + nodes draw in the channel colour (accent for RGB; red/green/blue for R/G/B). RGB is the
  master (applied to all), R/G/B apply per channel after.
- **Per‑channel (mixer):** **Hue / Sat / Lum** picker selects which cyclic hue‑curve shows; X = input hue
  [0,360) wrapping at the seam, Y = adjustment [−1,1].
- **Reset** icon clears the active channel/curve.
- **Green "final" reference (§9.2):** the effective group‑stacked curve draws faint `#4CB573` behind the
  editable one, live.

---

## 13. Overlays & dialogs (bottom sheets / modals)

Shared skeleton: a screen scrim + a card. **Prefer bottom sheets** (rise from the bottom edge ~180 ms,
dismiss down ~140 ms) for actions; use **centered modals** for confirms. Tap‑scrim or Android **back**
cancels.

### 13.1 Context menu = bottom action sheet (long‑press)
A bottom sheet listing labelled actions (filmstrip cell/group/empty, photo, preset rows). Includes the
**rename‑morph** behaviour for "Rename Group" (§9.3).

### 13.2 Engine settings sheet
- **Preview quality:** Draft / Standard / High (render preview edge 1000 / 1600 / 2400 px).
- **CPU threads:** Auto / 2 / 4 / 8.
- **GPU acceleration:** on/off toggle (use the GPU when available; falls back to CPU). Show
  disabled/"unavailable" when there's no GPU backend.

### 13.3 Preset category picker sheet
Title + a **"Select all"** master toggle + one **checkbox per category** (Basic, Colour, Presence,
Effects, Detail, Lens, Curve, Mixer, Grade, Transform, Masks) + Cancel / Confirm. Used on Save Preset /
Export / Import.

### 13.4 Confirm dialog (centered modal)
Title + message + a button row. Styles: destructive = red, primary = accent, else outline. Used for
**save‑or‑discard** when leaving an edited project (Save / Discard / Cancel) and Reset Workspace.

---

## 14. Full component inventory (design every one, in every state)

**Screens:** Home/launcher · Loading transition (intro/loading/reveal) · Editor · Return transition.

**Chrome:** Top bar (image mode + group mode) · Overflow (⋯) sheet · Left preset drawer · Preset tree row
(folder/leaf; collapsed/expanded/selected) · Photo canvas (empty/loaded/zoomed) · Before‑Split‑After
pill · Zoom badge/reset · Breadcrumb (+ crumb press) · Filmstrip (photo cell, group chip, selection
ring, multi‑select) · collapsed filmstrip.

**Edit trays:** Tool bar (5 tabs, active state) · Tray shell (handle, detents Rail/Half/Full, live photo
strip) · Histogram strip · Basic/Detail tray (section chip row + all 7 sections) · Mixer tray · Curve
tray · Grade tray (region picker, wheel/sliders, remap toggle) · Xform tray (rotation scrub, quarter‑
turn, aspect chips, optional crop rect) · Mask tray (add chips, selector, inv, trash, feather, adjust
block, empty state) · Action bar (Save/Import/Export) · optional focused‑slider dock.

**Reusable controls:** Slider row (bipolar + green reach + gradient‑track variant) · Segmented control ·
Pill button · Icon button · Toggle switch · Checkbox · Dropdown/list picker · Text field (dark) · Light
rename field.

**Editors:** Tone‑curve editor · Hue‑curve (mixer) editor · Curve node (corner/smooth/selected) · Tangent
handle · Per‑channel picker (RGB/R/G/B, Hue/Sat/Lum) · Green "final" reference curve · hue swatch strip.

**Overlays/modals:** Action sheet (+ rename morph) · History tree (full‑screen) · Settings sheet · Preset
picker sheet · Confirm modal · Mask overlay (radial/linear/brush handles) · Loading star‑sky + progress
bar.

**States for every interactive element:** idle · pressed · selected/active · disabled · (loading & empty
where a container can lack data). NO hover‑only states.

---

## 15. Token reference (Figma variables/styles)

- **Colours:** `bg #141414` · `card/tray #1C1C1C` · `popover #222222` · `input #252525` · `stage
  #0A0A0A` · `foreground #DBDBDB` · `muted #8A8A8A` · `border rgba(255,255,255,.072)` · `accent #4F7EF7`
  · `green/stacked #4CB573` · `destructive #E5484D` · rename field `#F4F4F5` bg / `#18181B` text ·
  channel R `#E65252` · G `#61CC6B` · B `#6B94F5`.
- **Radii:** `1` (hairline) · `2` (control) · `9999` (pill).
- **Type:** Roboto {Regular, Medium, SemiBold}; JetBrains Mono {Regular, Medium}. Phone body ~13–15 px,
  labels ~12 px, values mono ~13 px, section headers ~11–12 px tracked‑up. Keep desktop's ratios, scale
  ~1.15–1.3×.
- **Touch sizing:** min hit target **44 dp**; slider band **≥ 48 dp**; curve node grab **≥ 24 dp**; mask
  handle **≥ 28 dp**; tab/chip/list row ≥ 44 dp tall; adjacent targets ≥ 8 dp apart.
- **Layout:** design frame **393 × 852 dp** (verify at **360 × 780**); 4 dp spacing grid; respect top
  status‑bar and bottom gesture‑bar safe insets.
- **Tray detents:** Rail (tool bar only) · Half ~42% · Full ~88% (+ pinned live photo strip).

---

## 16. Motion (mandatory — nothing snaps; honor OS "reduce motion" → collapse to end state)

- Tray raise/lower & detent snap **200–320 ms** (spring‑ish ease‑out); tab cross‑fade 180 ms; tab
  underline/pill slide 200 ms; drawer open/close 240 ms; bottom sheet in 180 / out 140 ms (+~12 px
  rise); confirm modal in 150 / out 120 ms; segmented highlight 220 ms; filmstrip scroll 180 ms + ring
  slide 200 ms; slider thumb + green reach spring to target (~0.2 s); curve/mixer data swap instant,
  node/handle drags direct; screen transitions 460/520 ms; rename morph 260 ms; press feedback ~120 ms.
- Loading uses **real progress**, not a fake timer, with a small minimum visible time.

---

## 17. Notes for the Figma prompt

- Design **one product, one layout — a ~6‑inch Android phone in portrait** (393 × 852 dp; also holds at
  360 × 780). Do NOT design a tablet or desktop frame.
- The photo canvas is the persistent hero; **everything else is a bottom tray / sheet / drawer** that
  rises over it. Design the **tray shell** (handle + Rail/Half/Full detents + live photo strip) once and
  reuse it for every tab.
- Do **not** invent new colours or radii — use §15 only. Keep the dark, single‑accent, tight‑but‑touch
  feel; the **green is reserved strictly** for group‑stacked / "final" indicators.
- Highest‑frequency controls live in the **bottom third** for one‑handed reach.
- The **curve/mixer editors** and the **group‑stacking green indicators** are the distinctive,
  must‑not‑miss parts — spend detail there. Nothing from desktop is dropped; it's only re‑placed into
  trays.
