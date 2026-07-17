# Solaris — UI Design Brief (for a Figma AI)

Paste this into a Figma AI / generative design tool. It describes the UI for **Solaris**, a
touch-first Digital Audio Workstation. Design the screens below as a cohesive dark, professional,
touch-optimized app. Primary form factor: **tablet in landscape** (e.g. 1280×800 to 1600×1000);
also provide a reduced **phone portrait** layout where noted.

Screens to design: (1) Arrange / Timeline, (2) Mixer, (3) Piano Roll / MIDI editor, (4) Device &
Rack editor, (5) Routing Matrix / Patchbay, (6) Versions / Branches, (7) Browser, plus the
persistent (8) Transport bar and (9) App shell / navigation. Design the key states of each.

---

## 1. What it is (one paragraph)

Solaris is a professional multi-track DAW: arrange audio and MIDI clips on tracks, play a synth
instrument and an effect rack per track, mix through a flexible routing graph, record from multiple
inputs, and version the whole project like code (branches that auto-update, merges). It is part of
the **Arstro** creative suite alongside a photo editor (Cosmo) and a video editor (Interstellar), so
it must feel like a family member of those apps: dark, calm, precise, flat. It is used by scoring
artists and music producers, on a touch screen — so it must stay dense-yet-tappable, never relying
on a mouse hover or a right-click.

## 2. Visual language (make it feel like a precision instrument in a dark studio)

- **Theme: dark only.** Canvas / deep background `#0A0A0A`. Panels `#141414`. Raised surfaces / cards
  `#1C1C1C`. Hairline borders `rgba(255,255,255,0.08)` — separate with 1px lines and subtle fills,
  **not** heavy drop shadows (flat, like a modern pro tool).
- **One accent:** electric blue `#4F7EF7` — active states, playhead, selection, primary buttons, the
  logo dot. Use it sparingly so it means "active/primary".
- **Semantic colors:** record / armed = red `#F04747`; solo = amber `#F5A623`; mute = dimmed gray;
  positive meter = green `#39D98A` → yellow `#F5C518` → red `#F04747` gradient near clip.
- **Track identity colors:** a palette of ~10 muted, desaturated hues (teal, violet, coral, olive,
  slate, rose, cyan, amber, indigo, moss) used as a thin color strip on each track/clip — never as
  large fills.
- **Typography:** a clean geometric/grotesk sans (DM Sans / Inter feel) for labels and names; a
  **monospace** (JetBrains Mono feel) for all numbers — time position, tempo, dB, Hz, ms — so
  digits align.
- **Wordmark:** `solaris.` in lowercase, the trailing period in the accent blue. Small, top-left.
- **Shape & spacing:** 8px spacing grid; corner radius 8px for cards/sheets, 4px for small controls,
  full-round for knobs/faders thumbs. Generous padding.
- **Icons:** thin (1.5px) line icons, lucide-style, monochrome, tinting to accent when active.
- **Waveforms/notes:** waveforms drawn in the track's identity color at ~40% on a slightly lighter
  clip body; MIDI notes as small rounded bars in the track color.

## 3. Touch-first principles (apply to every screen — this is the core constraint)

- **Minimum hit target 48×48 dp**; primary transport and faders larger. No control smaller than a
  fingertip; add invisible padded hit areas around thin visuals (clip edges, note edges, playhead).
- **No hover-dependent UI.** Everything is reachable by tap, long-press, or drag. Replace right-click
  with **long-press → context bottom sheet**. Replace hover tooltips with a **value bubble** that
  appears while dragging.
- **Gesture vocabulary (use consistently):**
  - *Tap* = select / toggle / open.
  - *Double-tap* = reset to default (faders, knobs) or open editor (a clip).
  - *Long-press* = context menu as a **bottom sheet** of big actions.
  - *Drag* = move (clips, notes, faders); *drag on an edge handle* = trim/resize.
  - *Pinch* = zoom (horizontal = time, vertical = track/lane height).
  - *Two-finger drag* = pan/scroll the canvas.
  - *Swipe* on a strip/sheet = dismiss.
- **Fine adjustment without a mouse:** while dragging a knob/fader, a **magnified value bubble**
  shows the exact value; a **second finger held down** switches to fine (slow) mode; long-press opens
  a numeric keypad to type an exact value.
- **Thumb reach:** the **transport bar and primary actions live at the bottom** (or a bottom-anchored
  cluster) so they are reachable one-handed on a held tablet. Destructive actions are never at a
  screen edge where a stray touch hits them.
- **Panels over popovers:** use **bottom sheets** (editors, context actions, pickers) and **side
  drawers** (browser, sends, inspector) instead of small floating popovers. Sheets are draggable to
  half / full height.
- **Modes instead of modifiers:** since there are no keyboard modifiers, expose **tool modes** as a
  big segmented control (e.g. Select / Draw / Erase in the piano roll; Move / Trim / Split in the
  arrange view).
- **Grid & snapping:** a visible time grid; snapping on by default with a large snap toggle and a
  snap-value selector; dragging shows a snap guide line.
- **Big handles:** clip trim handles, note resize handles, loop-region ends, and fade handles are
  drawn as clear grabbable pucks, not 1px edges.

## 4. App shell & navigation

- **Left rail (primary nav), full height, ~72px wide, big icon tabs** (icon + tiny label), one active
  at a time, accent highlight slides between them: **Arrange · Mixer · Editor · Routing · Versions ·
  Browser**. (On phone portrait, move this to a bottom tab bar.)
- **Top bar (~48px):** `solaris.` wordmark, the **project name** (tap to rename), a **branch chip**
  showing the current branch + a small status dot (clean = blue, needs-attention = amber), a
  **Commit / Save** button, and an overflow menu (project settings, sample rate, channel count,
  export). A subtle **"LIVE / BOUNCED" indicator** shows whether playback is live from the project or
  from a cached mixdown.
- **Bottom: the persistent Transport bar** (section 8), visible on Arrange, Mixer, Editor.
- The main area between rail, top bar, and transport is the active view.

## 5. Screen — Arrange / Timeline (the home screen)

The core editing surface: tracks stacked vertically, time flowing left→right.

- **Track header column (left, ~240px, fixed):** per track, top to bottom of a row —
  a **color strip** (identity), the **track name** (tap to rename), a row of **big round toggle
  buttons: Arm (red), Mute, Solo (amber)**, an **input/monitor chip** (for record routing), a
  **mini gain slider or knob + small meter**, and a **device/rack button** (opens the rack). A
  **height grip** at the bottom-right of the header drags to resize the track (or pinch vertically).
  A **"+" add-track** button sits at the bottom of the column; long-press a header = track context
  sheet (color, duplicate, delete, freeze/bounce).
- **Ruler (top of timeline):** bars\:beats grid (since time is in beats @ 960 PPQ, single tempo),
  with a secondary mm\:ss readout; the **loop region** shown as a draggable bar with big end handles;
  tap the ruler to move the playhead.
- **Lanes (timeline body):** **clips** are rounded, filled in the track color at low opacity, with
  the clip name, and content: **audio clips show a waveform**, **MIDI clips show mini note blocks**.
  Clips can **freely overlap** (draw overlaps with slight transparency + a stacking edge). Each clip
  has **fade-in/out handles** (top corners) and **trim handles** (left/right edges) as grabbable
  pucks, and a small **loop badge** if looped.
- **Playhead:** a thin accent vertical line with a grabbable head in the ruler; smooth motion during
  playback.
- **Gestures here:** pinch = zoom time (H) / track height (V); two-finger drag = scroll; drag clip =
  move (snapping); drag clip edge = trim; drag fade puck = fade; long-press clip = context sheet
  (Split at playhead, Duplicate, Delete, Color, Rename, Reverse, Properties); double-tap clip =
  open the Piano Roll (MIDI) or a clip inspector (audio). Tap empty lane = create-clip menu or start
  drawing a MIDI clip.
- **Selection:** tap selects; a **Select mode** allows marquee (drag a box). Multi-select shows a
  floating action bar (bottom sheet) with batch actions.
- **States:** empty project (a friendly "Add a track" call-to-action card + big + button);
  recording (armed tracks pulse red, a moving record region draws on the lane); playing (playhead
  moves, meters live).

## 6. Screen — Mixer

A horizontally scrolling row of channel strips; the mixing console.

- **Channel strip (each ~120px wide):** top → bottom —
  **track name + color**, an **input chip** and **output/routing chip** (tap → routing sheet),
  **insert slots** (a small vertical stack of device chips; tap a slot to open the device editor;
  "+" to add; drag to reorder; bypass dot each), a **sends** button (opens a sends drawer with send
  knobs, each pre/post toggle), a **pan control** (a horizontal slider or small arc knob), a **large
  vertical fader** with a big thumb and a dB scale, a **stereo/multichannel meter** beside the fader
  (green→yellow→red), and a row of **big Mute / Solo / Arm** buttons with the record indicator.
- **Master + bus section (pinned right):** the master strip (with the project's channel count, e.g.
  2 by default, but could be 4+), and any bus strips. Master shows the main output meter.
- **Touch:** faders are big vertical drags with a value bubble; double-tap fader = 0 dB; long-press =
  type value; pan is drag with a bubble; strip scroll is horizontal two-finger or edge-swipe.
- **States:** clipping (meter peak turns red and latches a clip dot until tapped); soloed tracks
  dim the others; a track being recorded pulses.
- **Phone portrait:** show one strip at a time as a full-width card, swipe between strips, master
  reachable via a tab.

## 7. Screen — Piano Roll / MIDI editor (as a full view and as a bottom sheet)

Opens from a MIDI clip. Notes on a grid against a vertical keyboard.

- **Left: a vertical piano keyboard** (playable — tap a key to audition), scrollable, octave labels.
- **Center: the note grid** — bar/beat lines, notes as rounded bars in the track color; a
  **velocity lane** docked below (each note a bar whose height = velocity, draggable).
- **Tool modes (big segmented control, top):** Draw · Select · Erase. Plus a **snap/quantize**
  selector and a **note-length** selector.
- **Gestures:** in Draw mode, tap = add a note (default length) and drag = set length; drag a note =
  move (snapping); drag note ends = resize (big handles); pinch = zoom; two-finger = scroll;
  long-press a note/selection = context sheet (Quantize, Delete, Velocity, Duplicate, Legato). In
  Select mode, drag = marquee; selected notes get a floating batch bar.
- **Expression lanes (toggleable strips below velocity):** pitch-bend and MIDI CC / mod-wheel drawn
  as editable **bezier-handled curves** (same curve interaction as automation); per-note expression
  where enabled.
- **States:** empty clip (grid with a hint to draw); recording MIDI (incoming notes appear live).

## 8. Screen — Device & Rack editor

The per-track processing chain and the instrument.

- **Rack header:** a horizontal chain of **device cards** (instrument first, then effects), each with
  a name, a **bypass toggle**, and a drag handle to reorder; a **"+" add-device** opens a categorized,
  searchable **device browser** sheet (Instruments: Synth, Sampler(later); Effects: Reverb, EQ,
  Chorus, Overdrive, Compressor, etc.). Drag a card off / long-press = remove.
- **Device panel (tap a card to expand full editor):** big, legible controls — **large knobs** and
  **sliders** with value bubbles, grouped into labeled sections. For the **Synth instrument**, reuse
  the Pulsar synth layout: Oscillators (waveform, detune, spread, level, tune), Filter (cutoff,
  resonance), ADSR Envelope, LFO, Macros. For **effects**, expose their parameters as knob/slider
  clusters (e.g. Reverb: mix, size, decay; EQ: bands).
- **Knob interaction:** vertical drag over a large hit area, value bubble, double-tap = default,
  long-press = numeric entry, second-finger = fine mode.
- **Automation affordance:** a small "A" on each control opens/links its **automation lane** in the
  arrange view (bezier curve).
- **States:** bypassed device (dimmed card + strikethrough on the panel).

## 9. Screen — Routing Matrix / Patchbay (the "audio matrix")

The flexible routing graph, presented as a touch-reliable **matrix grid** (rows × columns of
tappable crosspoints — easier on touch than dragging cables).

- **Three stacked matrices (or a section switcher):**
  1. **Inputs → Tracks** (record routing): hardware input ports as rows, record-armed tracks as
     columns; tap a cell to wire an input to a track.
  2. **Tracks / Buses → Buses / Master** (internal routing + sends): tap a crosspoint to route/send;
     a small pre/post toggle on send cells.
  3. **Buses / Channels → Hardware Outputs** (output routing): buses/master channels as rows,
     **hardware output ports as columns — grouped by device**, so *multiple audio devices each
     carrying different channels* is visible; any bus or channel can be wired directly to any output
     (the master is not the only path out).
- **Crosspoint cell:** a big square, clearly on (filled accent + connect glyph) or off (empty);
  optional small level control on active cells (tap to reveal).
- **Port groups** are labeled headers (device name, channel names). Support 2/4/multichannel and
  multiple devices.
- **Touch:** tap toggles a connection; long-press a cell = level/latency options; the grid scrolls
  two-finger. Provide a compact overview + a zoom for large matrices.
- **States:** an invalid/feedback routing attempt shows a clear inline error (cycles rejected).

## 10. Screen — Versions / Branches (project version control)

Version the project like code — the suite's signature feature. Make it approachable, not a git UI.

- **Branch bar (top):** the current **branch chip**, a **branch list / switch** button, **New
  Branch**, and a **Commit** button (opens a sheet to type a short message). A **status line**: clean
  (blue) or "needs attention" (amber) when an auto-rebase hit a conflict.
- **History graph (center):** a vertical **git-style tree** of commits (nodes + branch lines in the
  track/accent colors), newest at top; the current commit highlighted. Tap a commit to **preview /
  jump**; long-press = context (branch from here, tag, compare).
- **Living-branch explainer:** show that a feature branch auto-updates when its base advances — a
  small animated "synced" indicator; when a conflict occurs, list the **flagged items** (which
  clips/params clash) with a simple **Keep mine / Keep theirs / both** choice per item.
- **Merge & embed actions:** **Merge two projects** (pick main + imported, choose **Concatenate** —
  imported song after the main — or **Overlay**); **Embed a project** (import another Solaris project
  that keeps its own master routed into this one) with a **LIVE / BOUNCED** toggle to play from a
  cached mixdown for CPU.
- **Touch:** big buttons, sheets for commit/merge; the graph pans two-finger, pinch to zoom.
- **States:** clean, uncommitted-changes (a dot on Commit), rebase-conflict (amber banner + the
  flagged list), merged (confirmation).

## 11. Screen — Browser (side drawer)

A slide-in drawer for content, openable from the rail or by an edge-swipe.

- Tabs: **Samples** (audio files / the resource pool, searchable, with tiny waveforms — drag into a
  lane), **Devices/Presets** (instruments/effects to drop on a rack), **Projects** (recent + projects
  to embed). Big list rows, search field, drag-to-place.

## 12. Transport bar (persistent, bottom)

Always visible on Arrange/Mixer/Editor; the most-used controls, thumb-reachable.

- **Center cluster (large):** Play / Stop (toggle), **Record (red)**, **Loop** toggle, and a
  return-to-start.
- **Position readout (mono font):** `bars:beats:ticks` and `mm:ss:ms`, big and legible; tap to type a
  location.
- **Tempo** (BPM, tap-tempo + drag to change), **time signature**, **metronome** toggle, **count-in**
  toggle.
- **Right:** master volume (small fader) + master meter, a **CPU / xrun** indicator (turns amber on
  strain), and the **LIVE / BOUNCED** state.
- On phone, collapse to Play/Record/Loop + position, with the rest behind a small expander.

## 13. Recording flow (design the active state)

Arm a track (red), pick its input in the Routing matrix (or the header input chip), enable input
monitoring, optional count-in, hit Record: the armed lane draws a growing red region with a live
waveform; a level meter on the header; stop drops a new audio clip. Show a clear "recording"
banner and make Stop unmistakable.

## 14. Global states to design

Design each screen's: **empty** (no tracks/clips — inviting call-to-action), **loading** (opening a
project — a calm branded spinner / skeleton on the dark canvas), **playing**, **recording**,
**branch-conflict** (amber, actionable), **offline/bounced** (indicator), and **error/relink**
(a missing sample shown as an offline clip with a Relink action). Everything animates smoothly
(fades, eased slides) — nothing pops or jumps.

## 15. Responsive

- **Primary:** tablet landscape. Full rail + all panels.
- **Secondary:** phone portrait — rail becomes a bottom tab bar; Mixer shows one strip at a time
  (swipe); editors open as full-height sheets; the transport collapses to essentials. Keep every
  action reachable; never hide a primary control behind hover or tiny targets.

## 16. Family consistency

Match the Arstro suite look (as in Cosmo, the photo editor): the same near-black dark canvas, the
same blue accent, the flat hairline-bordered panels, the `name.` accent-dot wordmark, thin line
icons, and the mono-for-numbers rule — so Solaris, Cosmo, and Interstellar read as one product line.
