# Solaris — UI Design Brief (for a Figma AI)

Paste this into a Figma AI / generative design tool. It describes the UI for **Solaris**, a
touch-first Digital Audio Workstation, and is deliberately specific about **how every control
opens, how components react to each other, and how everything animates**. The look must be
**clean, minimalist, calm** with **pervasive smooth motion — nothing ever snaps**.

Primary form factor: **tablet in landscape** (1280×800 → 1600×1000). Provide a reduced **phone
portrait** layout where noted.

Screens: (1) Arrange/Timeline, (2) Mixer, (3) Piano Roll/MIDI, (4) Device & Rack editor, (5) Routing
Matrix, (6) Versions/Branches, (7) Browser, plus the persistent (8) Transport bar and (9) App shell.
Also design the shared surfaces (bottom sheet, side drawer, inspector, value bubble, context sheet)
and the key states. **Design the motion, not just the frames** — provide open/close/transition
states and, where the tool supports it, prototype the animations.

---

## 1. What it is

Solaris is a professional multi-track DAW: arrange audio and MIDI on tracks, play a synth instrument
and effect rack per track, mix through a flexible routing graph, record from multiple inputs, and
version the whole project like code (auto-updating branches, merges). It is part of the **Arstro**
suite with a photo editor (Cosmo) and a video editor (Interstellar) and must feel like their family
member: dark, precise, flat, quiet. It runs on a touch screen for scoring artists and producers, so
it stays dense-yet-tappable and **never relies on hover or right-click**.

## 2. Design philosophy — clean, minimalist, progressive

Design for **calm focus**. The screen should feel mostly empty until the user needs a control.

- **Minimal persistent chrome.** Only three things are always on screen: the **left rail**, the **top
  bar**, and the **transport bar**. Everything else — inspector, sends, device editors, browser,
  context actions — is **summoned and dismissed** as sheets/drawers, then gone. No permanent panels
  cluttering the edges.
- **Progressive disclosure.** Show the few controls used constantly; hide the rest one layer down.
  A track header shows name + arm/mute/solo + a meter; *everything else* is a long-press away. A
  device card shows name + bypass; its 20 knobs live inside, opened on demand.
- **One accent, lots of negative space.** A single blue accent marks "active/primary". Separate with
  hairlines and spacing, not boxes and shadows. No gradients except meters. Monochrome line icons.
- **Content over controls.** Waveforms, notes, curves, and meters are the bright/colored elements;
  chrome recedes (low-contrast grays). The user's music is the UI.
- **Terse and legible.** Short labels; all numbers in a monospace so digits align; hide a label when
  an icon is unambiguous (but keep it discoverable — long-press reveals a label).
- **One surface at a time.** Never stack modals. Opening a new sheet **smoothly hands off** from the
  previous one (the old dismisses as the new arrives). The user is never lost in layers.

## 3. Visual language

- **Dark only.** Canvas `#0A0A0A`; panels `#141414`; raised cards/sheets `#1C1C1C`; hairline borders
  `rgba(255,255,255,0.08)`. Flat — no heavy shadows; elevation is a 1px border + a barely-lighter fill.
- **Accent:** electric blue `#4F7EF7` — active state, playhead, selection ring, primary button, the
  logo dot. Used sparingly.
- **Semantics:** record/armed red `#F04747`; solo amber `#F5A623`; mute = dimmed; meter gradient
  green `#39D98A` → yellow `#F5C518` → red `#F04747` near clip.
- **Track identity:** ~10 muted, desaturated hues as a thin color strip on each track/clip (never
  large fills).
- **Type:** geometric grotesk sans (DM Sans / Inter feel) for labels; **monospace** (JetBrains Mono
  feel) for all numbers. `solaris.` wordmark, trailing period in accent, top-left.
- **Shape/space:** 8px grid; radius 8px cards, 4px small controls, full-round thumbs; generous
  padding. Thin (1.5px) line icons, tinting to accent when active.

## 4. Motion & animation system (the heart of the feel)

**Rule 0 — nothing snaps.** Every change of position, size, opacity, color, or content is animated.
A value that must change instantly still eases over one or two frames. Motion is **smooth, quick,
and purposeful** — it shows what changed and where it came from, never decorative jitter.

**Motion tokens (durations + easing):**
- **Press feedback — 90ms, easeOut.** Any tappable scales to ~0.97 and lightens on touch-down;
  releases back. Immediate physicality.
- **Micro — 120ms, easeOutCubic.** Toggles, selection ring in/out, value bubble appear, icon tint,
  chip label swap.
- **Standard — 220ms, easeOutCubic.** Sheets/drawers open-close, panel expand/collapse, tab content
  swap, list insert/remove, snap-guide, meter peak-hold reset.
- **Large — 360ms, easeInOutCubic (or a soft spring).** View/screen transitions, shared-element
  morphs (chip→panel, clip→editor), branch reflow.
- **Continuous — real-time, linear.** Playhead travel, meter ballistics, waveform/scroll under the
  playhead. These track audio, not a tween.
- **Spring (soft, ~0.8 damping).** Draggable release settle (fader/knob thumb landing on value,
  sheet drag-release, elastic overscroll). Gives weight without bounce.
- **Stagger — 20–30ms per item.** When a list/grid reveals (tracks, device browser, matrix rows),
  items fade+slide in sequence, not all at once.

**Choreography (how each transition moves):**
- **Open a bottom sheet:** a scrim fades in (0→40% black, 220ms) as the sheet slides up from the
  bottom edge with a soft spring; its content **staggers** in. Drag the handle down to dismiss;
  release past a threshold springs it closed; the scrim fades out.
- **Open a side drawer** (browser, inspector, sends): scrim + slide-in from the edge, 220ms easeOut.
- **Expand in place (shared-element morph):** a **device chip grows into its full editor panel**,
  its label staying anchored while the body fades/expands around it (360ms). Same for a **track
  header → expanded header**, a **clip → its inspector**, a **collapsed section → open section**.
  Reverse on close so the panel visibly returns to the chip it came from.
- **Selection:** an accent ring/edge eases in (120ms); deselecting eases out; moving the selection
  between items **cross-fades** the highlight from the old to the new (the highlight slides, it does
  not jump).
- **Value change:** a fader/knob thumb **springs** to the new value; the numeric readout **rolls**
  (odometer count, not a hard replace); a value bubble pops in above the thumb and fades on release.
- **View switch (rail tabs):** the outgoing view fades and slides ~16px out; the incoming fades and
  slides in; the **rail, top bar, and transport do NOT animate** — they anchor the change (only the
  active-tab accent slides between icons, 220ms).
- **List insert/remove:** adding a track/clip/device — the new item **grows from 0 height and fades
  in** (220ms), pushing neighbors down smoothly; removing — it collapses and fades, neighbors close
  the gap.
- **Playhead & transport:** playhead moves continuously; on **seek** it **eases** to the new spot
  (140ms) rather than teleporting; play/stop cross-fades the button glyph; loop toggles a soft pulse
  on the loop region.
- **Routing connect:** tapping a matrix crosspoint fills it with an accent **ripple** from the touch
  point; a faint one-shot **signal-flow pulse** runs along the new route so the user sees the
  connection made.
- **Meters:** ballistic (fast attack, slow ~300ms release), a peak-hold dot that eases down; clip
  latches red until tapped, then fades to clear.
- **Zoom (pinch):** track heights/clip widths ease with the gesture (not stepped); the ruler's beat
  subdivisions **cross-fade** as they appear/disappear at zoom thresholds.
- **Empty→content, loading:** skeleton shapes shimmer softly; when data lands, real content
  cross-fades over the skeleton.
- **Reduced motion:** provide a variant where these collapse to quick cross-fades / instant final
  states (respect the OS setting) — but the default is fully animated.

## 5. Touch-first principles

- **≥48dp hit targets**; transport and faders larger. Thin visuals (clip edges, note ends, playhead)
  get invisible padded hit areas.
- **No hover UI.** Long-press → **context bottom sheet** replaces right-click; a dragging **value
  bubble** replaces hover tooltips.
- **Gesture vocabulary (consistent everywhere):** tap = select/toggle/open; double-tap = reset (or
  open editor on a clip); long-press = context sheet; drag = move; drag-on-handle = trim/resize;
  pinch = zoom (H time / V lane height); two-finger drag = pan/scroll; swipe = dismiss a sheet.
- **Fine control without a mouse:** during a knob/fader drag, a magnified value bubble shows the
  exact value; **hold a second finger** = fine mode; **long-press** = numeric keypad to type a value.
- **Thumb reach:** transport + primary actions anchored at the **bottom**; destructive actions never
  at a screen edge.
- **Panels over popovers; modes over modifiers:** use bottom sheets/drawers, and expose **tool
  modes** as big segmented controls (Select/Draw/Erase, Move/Trim/Split) since there are no keyboard
  modifiers.
- **Visible grid + snapping** with a large snap toggle and a snap-value selector; a snap guide line
  eases in while dragging. **Big grabbable handles** on clip trims, fades, note ends, and loop ends.

---

## 6. App shell & navigation

- **Left rail** — full height, ~72px, big icon tabs (icon + tiny label), one active. The **accent
  indicator slides** between tabs (220ms) on switch. Tabs: **Arrange · Mixer · Editor · Routing ·
  Versions · Browser**. (Phone: becomes a bottom tab bar.)
- **Top bar (~48px):** `solaris.` wordmark; **project name** (tap → inline rename field that expands
  in place); a **branch chip** with a status dot (blue = clean/synced, amber = needs attention);
  **Commit/Save**; overflow menu (sample rate, channel count, export). A small **LIVE / BOUNCED**
  indicator (tap = toggle) shows whether playback is live from the project or from a cached mixdown;
  toggling cross-fades the label and the affected clips' tint.
- **Transport bar** — persistent at the bottom (section 12).
- **View switches** animate per §4 (content cross-fades/slides; shell anchors).

---

## 7. Control catalogue — how each control opens, interacts, and animates

Design these as reusable components with explicit open/close and cross-component behavior.

- **Button / icon button.** Trigger: tap. Feedback: press-scale 0.97 + lighten (90ms), release back;
  a soft accent ring for primary. Disabled = 30% opacity (fades when enabled). Fires its action; if
  it opens a surface, that surface animates in per §4.
- **Toggle (Mute/Solo/Arm/Bypass/Loop/Metronome).** Tap flips state with a 120ms color/fill ease.
  Toggling **broadcasts**: Solo dims other tracks (opacity ease) across Arrange **and** Mixer; Mute
  dims that track's clips; Arm turns the track's record elements red and reveals its input chip.
- **Segmented control (tool modes, channel picker).** Tap a segment → the **highlight slides**
  between segments (220ms, blended corner radii) and the view's mode changes; the mode change may
  cross-fade affected affordances (e.g. Draw mode reveals the pencil cursor hint).
- **Knob.** Trigger: vertical drag over a large hit area. A **value bubble** pops above (120ms); the
  indicator arc fills; on release the pointer **springs** to rest. Double-tap = reset (thumb springs
  to default). Long-press = numeric keypad sheet. Second finger = fine. Editing a device knob
  **reflects live** in any linked automation lane and the mixer.
- **Fader (vertical).** Big thumb; drag with a value bubble; double-tap = 0 dB (springs); long-press
  = type. A Mixer fader and the track-header mini-gain in Arrange are the **same value** — moving one
  animates the other in real time. Meter beside it responds continuously.
- **Slider (pan, sends, mini-gain).** Drag with bubble; double-tap = center/default. Pan changes
  update the meter balance.
- **Value bubble / numeric keypad.** Bubble: appears on drag, follows the thumb, fades on release.
  Keypad: long-press opens a compact keypad **bottom sheet**; typing updates the control live; a
  soft confirm dismisses it downward.
- **Chip (input / output / routing / send-count).** Compact pill showing current routing. Tap →
  opens the **Routing sheet** focused on that node. When routing changes elsewhere, the chip's label
  **cross-fades** to the new value (never hard-swaps).
- **Device chip → Device panel (shared-element).** In a rack, a device is a small chip (name +
  bypass). Tap → it **morphs/expands into its full editor** (a bottom sheet or an expanded strip),
  label anchored, body fading in (360ms). Close → it returns into the chip. Reorder = drag (neighbors
  ease aside); remove = drag off / long-press → confirm; both animate the rack closing the gap. Add
  = "+" opens the **device browser sheet**; the chosen device's chip **grows in** at the insert point,
  and the matching Mixer insert slot appears with the same animation.
- **Clip (arrange).** Tap = select (accent edge eases in); drag = move (snapping, guide line); drag
  edge handles = trim; drag top corners = fades; long-press = **context sheet** (Split, Duplicate,
  Delete, Color, Rename, Reverse, Bounce, Properties); double-tap = open editor (MIDI clip → Piano
  Roll via shared-element morph; audio clip → clip inspector drawer). Selecting a clip **updates the
  inspector** (its content cross-fades) and highlights the owning track header.
- **Note (piano roll).** Draw mode: tap = add (grows in), drag = set length; drag = move; drag ends =
  resize; long-press = context (Quantize/Delete/Velocity/Legato). Editing a note updates the velocity
  lane bar (height animates) and any expression curve.
- **Bottom sheet.** The default surface for editors, context actions, pickers, keypad. Slides up +
  scrim + staggered content; draggable to half/full; swipe/handle down to dismiss (spring). Only one
  at a time — opening a second dismisses the first with a handoff.
- **Side drawer.** Browser (left/edge), Inspector & Sends (right). Slide-in + scrim; edge-swipe to
  open/close. The main view **shifts/scales slightly** to acknowledge the drawer (or the drawer
  overlays with scrim — pick one and keep it consistent).
- **Context sheet (long-press).** A compact bottom sheet of big labeled actions; appears from the
  touched item's vicinity; actions animate their result (e.g. Split drops a cut line that eases in).
- **Dialog / confirm (destructive only).** Center card, fade + scale-from-98%, scrim; primary =
  accent, destructive = red, cancel = ghost; dismiss by tapping the scrim (card scales back out).
- **Matrix crosspoint cell.** Tap toggles a connection with an accent ripple + signal-flow pulse
  (§4); long-press = level/latency options sheet.
- **Meter.** Continuous ballistics; peak-hold dot; clip latch. Never a static bar.
- **Playhead.** Continuous; grabbable head in the ruler; eases on seek.
- **Branch/commit node (versions).** Tap a commit = preview/jump (the whole project reflows with
  eased transitions to that state); long-press = context (branch from here, compare, tag).

---

## 8. Screen — Arrange / Timeline (home)

Tracks stacked vertically; time left→right. Minimal by default; details on demand.

- **Track header column (left, ~240px):** color strip · name (tap→inline rename) · **big round
  Arm/Mute/Solo** · a compact meter · a mini-gain. *That's it* — input chip, rack button, and the
  rest appear on **long-press → track sheet** or when armed. A **"+" add-track** at the bottom (grows
  a new empty track in). A **height grip** (or pinch V) resizes the track, eased.
- **Ruler:** bars\:beats grid + secondary mm\:ss; **loop region** as a bar with big end handles (drag
  = resize, pulses when active); tap = move playhead (eases).
- **Lanes:** rounded clips in the track color at low opacity — **audio = waveform, MIDI = mini note
  blocks** — with fade pucks and trim handles; **free overlap** drawn with slight transparency.
- **Interactions & motion:** pinch = zoom (heights/widths ease; ruler subdivisions cross-fade);
  two-finger = scroll; drag clip = move with snap guide; long-press clip = context sheet; double-tap
  clip = editor (shared-element morph). **Selecting a clip** cross-fades the right **Inspector
  drawer** to that clip and highlights its header. **Playing** moves the playhead here and in any
  open Piano Roll simultaneously; meters (if a strip is peeked) respond.
- **States:** empty (a calm centered "Add your first track" card + big +); recording (armed lanes
  draw a growing red region with a live waveform, header meters active); soloing dims other tracks.

## 9. Screen — Mixer

Horizontally scrolling channel strips; the console. Clean columns, lots of vertical breathing room.

- **Strip (~120px):** name + color · input/output chips (tap→routing sheet) · **insert slots** (small
  device chips; tap→device panel morph; "+" grows one in; drag reorders; bypass dot) · a **Sends**
  button (opens the **Sends drawer** with send sliders + pre/post toggles) · **pan** · a **big
  vertical fader** + dB scale · **meter** · **Mute/Solo/Arm**.
- **Master + buses pinned right;** master shows the project channel count (default 2, may be 4+).
- **Cross-component:** a fader here and the Arrange track-header mini-gain move together (real-time);
  opening a device panel here is the **same** morph as in Arrange; solo/mute dim consistently across
  both screens.
- **Motion:** faders spring on release; meters continuous; strip scroll eased with elastic overscroll;
  opening the Sends drawer slides it over the strip with a scrim.
- **Phone:** one strip as a full-width card, swipe between (paged, spring), master on a tab.

## 10. Screen — Piano Roll / MIDI editor

Opens from a MIDI clip via **shared-element morph** (the clip expands into the editor; closing
returns to it).

- **Left: playable vertical keyboard** (tap = audition, key lights); **note grid** center; **velocity
  lane** docked below.
- **Tool modes (segmented, top):** Draw · Select · Erase, plus snap/quantize and note-length
  selectors (highlight slides on change).
- **Motion/interactions:** draw = tap-grows a note / drag sets length; move/resize with handles;
  pinch zoom; long-press = context. Editing a note animates its velocity bar and any **expression
  curve** (pitch-bend / CC lanes, toggled below) which are **bezier-handled curves** with the same
  drag feel as automation. Recording MIDI: incoming notes **fade in live**.

## 11. Screen — Device & Rack editor

The instrument + effect chain.

- **Rack:** a horizontal chain of **device chips** (instrument first). Tap a chip → **morph to full
  panel** (§7). Reorder = drag (ease aside); add = "+" → device browser sheet.
- **Device panel:** big knobs/sliders in labeled sections; **Synth reuses Pulsar's layout**
  (Oscillators, Filter, ADSR, LFO, Macros); effects show their param clusters. Each control has a
  small **"A"** that opens/links its **automation lane** in Arrange (the lane slides in there).
  Editing a param **reflects live** in the mixer and automation. Bypass dims the panel (ease +
  strikethrough).

## 12. Screen — Routing Matrix / Patchbay

Touch-reliable **crosspoint grid** (not draggable cables).

- **Three matrices (section switcher, cross-fades between them):** Inputs→Tracks (record), Tracks/
  Buses→Buses/Master (routing + sends, with pre/post on send cells), Buses/Channels→**Hardware
  Outputs grouped by device** (so multi-device, each carrying different channels, is visible; any
  bus/channel can go directly out — master isn't the only path).
- **Cells:** big squares, clearly on (accent + connect glyph) / off; tap toggles with ripple +
  signal-flow pulse; long-press = level/latency. Cycles rejected with an inline error that eases in.
- **Cross-component:** any change here **cross-fades the input/output chips** on track headers and
  mixer strips. The grid pans two-finger; large matrices get a compact overview + eased zoom.

## 13. Screen — Versions / Branches (Nebula VCS)

Version the project like code, made approachable — not a git UI.

- **Branch bar:** current **branch chip** · switch/new-branch · **Commit** (sheet to type a short
  message) · a **status line** (clean = blue; "needs attention" = amber when an auto-rebase
  conflicts).
- **History graph:** a vertical **git-style tree** (nodes + branch lines in muted colors), current
  commit highlighted; tap a commit → the **whole project reflows** to that state with eased
  transitions (clips ease to positions); long-press = branch/compare/tag.
- **Living branch:** a subtle animated **"synced"** indicator shows a feature branch auto-updating
  when its base advances; on conflict, a list of **flagged items** appears (which clips/params
  clash) with per-item **Keep mine / theirs / both**, each resolution animating the item settling.
- **Merge & embed:** **Merge two projects** (pick main + imported → Concatenate / Overlay, previewed
  with an eased reflow); **Embed a project** (imported keeps its own master routed into this one),
  with the **LIVE / BOUNCED** toggle. Bouncing a track fades a "frozen" tint + badge onto its clips
  and flips the transport indicator.

## 14. Screen — Browser (side drawer)

Slide-in drawer (rail or edge-swipe). Tabs: **Samples** (resource pool, tiny waveforms — drag into a
lane), **Devices/Presets** (drop onto a rack), **Projects** (recent + embeddable). Big list rows,
search, drag-to-place (the dragged item shows a ghost; the drop target highlights; on drop the clip/
device grows into place).

## 15. Transport bar (persistent, bottom)

Thumb-reachable, always visible on Arrange/Mixer/Editor.

- **Center (large):** Play/Stop (glyph cross-fades) · **Record (red)** · **Loop** · return-to-start.
- **Position (mono):** `bars:beats:ticks` + `mm:ss:ms` (odometer-rolls during playback); tap = type a
  location (keypad sheet).
- **Tempo** (tap-tempo + drag) · time signature · **metronome** · count-in.
- **Right:** master mini-fader + meter · **CPU/xrun** (eases to amber on strain) · **LIVE/BOUNCED**.
- **Phone:** collapse to Play/Record/Loop + position; the rest behind a small expander (slides up).

## 16. Recording flow (active state)

Arm a track (red) → its input chip appears; pick the input in the header chip or Routing matrix →
enable monitoring → optional count-in → Record: the armed lane draws a **growing red region with a
live waveform**, header meter active; a clear "recording" banner; Stop drops a new audio clip
(grows into place). Make Stop unmistakable.

## 17. Global states

Design each screen's **empty** (calm, inviting CTA card), **loading** (soft skeleton shimmer →
cross-fade to content), **playing**, **recording**, **branch-conflict** (amber, actionable),
**offline/bounced** (indicator + frozen tint), **error/relink** (a missing sample = an offline clip
with a Relink action). Every state change animates — fades and eased slides, never a pop.

## 18. Responsive

- **Primary:** tablet landscape — full rail + summoned sheets/drawers.
- **Phone portrait:** rail → bottom tab bar; Mixer shows one strip at a time (swipe, paged spring);
  editors open as full-height sheets; transport collapses to essentials. Every action stays
  reachable; no hover, no tiny targets.

## 19. Family consistency

Match the Arstro suite look (as in Cosmo): the same near-black canvas, blue accent, flat
hairline-bordered surfaces, `name.` accent-dot wordmark, thin line icons, mono-for-numbers, and the
**everything-animates-nothing-snaps** motion feel — so Solaris, Cosmo, and Interstellar read as one
product line.
