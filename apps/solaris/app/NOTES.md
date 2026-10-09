# solaris_app — notes for whoever changes a widget

Read `arstro.design.rule` first; values are cosmo's (aliased in `Theme.h`), the accent is teal
`#159387` (R-UI-2). This file is the app's own record: what each control SENDS, the shots, and the
helpers it borrows.

## Every control is a command line

| control | sends |
|---|---|
| Home › New song / the empty state's button | the host's save picker → `project new "<path>.slp"` |
| Home › Open song… | the host's open picker → `project open "<path>"` |
| Home › a card | `project open "<path>"` |
| Home › a card, right-click | `recents remove "<path>"` |
| Home / song bar › Settings (beside Home), Ctrl+, | `devices list`, then the sheet opens |
| Song bar › File › New Song… · Open… · Save · Save As… · Render… · Render Stems… · Home | picker → `project new` · picker → `project open` · `project save` · picker → `project save "<path>"` · picker → `render --out "<path>"` · picker → `render --out "<path>" --stems <every strip>` · `project close` |
| Song bar › Edit › Undo · Redo · Duplicate Clip · Delete Clip | `undo` · `redo` · `clip duplicate <ac>` · `clip delete <ac>` |
| Song bar › Song › Add Mixer · Add Bus · Add Audio Line · Add Lane · Quantize Clip | `mixer add` · `strip add --kind bus` (placed before Main, feeding it) · `strip add --kind audio` · `lane add` · `pattern quantize <pt> --grid <the lanes' snap step, 1/32 when none>` (the selected note clip's pattern) |
| Song bar › View › Hide/Show Mixer · Hide/Show Browser · Metronome · Show/Hide IDs · Settings… | the view's · the view's · `settings set metronome=on\|off` · `settings set showIds=on\|off` (R-UI-11) · the sheet |
| Settings › a chip | `settings set output=<id>` · `input=<id>` · `sampleRate=<hz>` · `bufferSize=<frames>` · `metronome=on\|off` · `metronomeLevel=<dB>` · `newBpm=<bpm>` · `newSig=<n/d>` · `reducedMotion=on\|off` |
| Settings › × on a folder | `folder remove "<path>"` |
| Settings › Add folder… | the host's folder picker → `folder add "<path>"` |
| Song bar › Home | `project close` — behind cosmo's ConfirmDialog when unsaved (Save → `project save` then close) |
| Song bar › Play/Stop, Space | `transport play` / `transport stop` |
| Song bar › Save, Ctrl+S | `project save` |
| Enter | `transport seek 0` |
| Ctrl+Z · Ctrl+Shift+Z / Ctrl+Y | `undo` · `redo` |
| Browser › a tab | nothing — the list is the model's (`settings.folders`, `deviceTypes`, the song's clips) |
| Browser › a sample folder, a sub-folder, the row back up | `browse "<path>"` |
| Browser › Samples with no folders, a click | `devices list`, then the settings sheet |
| Browser › a sample dragged onto the lanes | `clip add --src "<file>" --at <beat> --lane <ln\|new>` — `<beat>` on the lanes' snap step; below the last lane: `--lane new`, a new one (said explicitly since R-SVC-8: with no `--lane` a clip joins its strip's lane — a used sample would have landed on its old row) |
| Browser › an instrument dragged onto the lanes | `clip add --instrument <type> --at <beat> --length 4 --lane <ln\|new>` — ONE line (R-BROWSE-3); onto another instrument's track: `--lane new` (R-LANE-3), as for a sample |
| Browser › a double-click | the same, at the playhead |
| Browser › an effect dragged onto the lanes | nothing; a notice (effects go on a strip — U3) |
| Lanes › a clip dragged | `clip move <ac> [--at <beat>] [--lane <ln>]` on release (on the snap step; between lanes only) |
| Lanes › a ruler click | `transport seek <beat>` — on the grid you see: the finest level with room, named in the ruler's corner ("Snap 1/8"); at the deepest zoom ("Off") the exact tick (R-TIME-5, R-UI-10) |
| Lanes › a double-click on an instrument's track where no clip is · its right-click › New MIDI Clip | `clip add --lane <ln> --at <beat on the snap step> --length <a bar>` — a MIDI clip of a new pattern, through the track's instrument (R-CLIP-6) |
| Lanes › a clip's right edge dragged | `set <ac>.length=<beats>` ONCE on release, the end on the snap step and never shorter than a step; the length is the pointer's while held, a shell's eases (R-CLIP-7) |
| Browser › Song › New MIDI · a MIDI row double-clicked · its menu | `pattern new --name "MIDI <n>"` · its piano roll · Rename… (`set <pt>.name=`) / Piano Roll / Duplicate (`pattern duplicate`) / Copy ID / Delete (`pattern delete`) (R-BROWSE-4) |
| Browser › Song › a MIDI row dragged onto the lanes | onto a track: `clip add --pattern <pt> --lane <ln> --at <beat>`; elsewhere: `… --strip <its newest clip's strip> --at <beat> --lane <ln\|new>`; unplayed, off a track: a notice, nothing sent (R-BROWSE-4) |
| Lanes › a lane header, right-click | Track of ▸ (the instrument strips) → `set <ln>.strip=<ch>` · Plain Lane → `set <ln>.strip=none` · Copy ID — a track names its instrument under its name, the line fading in and out (R-LANE-3) |
| Lanes › a clip dragged onto an instrument's track | `clip move <ac> --lane <ln>` — the service re-routes a note clip to that instrument (its colour eases); audio is refused there and eases home (R-LANE-3) |
| Lanes › Shift-drag on the ruler · a click inside the loop's brace | `transport loop <from> <to>` on release (both on the snap step) · `transport loop off` |
| Lanes › the ruler dragged (anywhere but the brace) | `transport seek <beat>` at each new line of the snap step while held — the playhead is the pointer's — and once more where let go if it moved on (R-TIME-6) |
| Lanes › the loop's brace dragged by its body · by an end (the ruler's lower half) | `transport loop <from> <to>` ONCE on release: moved with its length kept · resized, never past the other end (R-TIME-6) |
| Lanes › a click on a clip | nothing — selection is the view's (U4 links it to the strip) |
| Lanes › Delete / Backspace with a clip selected | `clip delete <ac>` |
| Lanes › Ctrl+D with a clip selected | `clip duplicate <ac>` (a note clip's copy is linked) |
| Lanes › Ctrl+wheel · wheel · Shift+wheel | nothing — zoom about the pointer (×1.25 a notch, 4.7–637 px a beat; the grid's levels fade with it and the snap step follows), scroll, scroll sideways (the view's) |
| Dock › a mixer tab · the chevron · its top edge dragged · a fold header | nothing — the page shown, the dock folded or sized, a group folded (the view's) |
| Dock › "+" in the tabs | `mixer add` |
| Dock › a mixer tab, right-click | Rename… → `set <mx>.name="…"` · Copy ID → the host's clipboard · Delete mixer → `mixer delete <mx>` |
| Dock › a fader dragged (each step) · double-click | `set <ch>.gain=<dB>` (master: `set project.masterGain=<dB>`) · `…=0` |
| Dock › pan dragged · double-click | `set <ch>.pan=<−1…1>` · `…=0` |
| Dock › M · S | `set <ch>.mute=true\|false` · `set <ch>.solo=true\|false` |
| Dock › "→ out" | a menu of `strips[].targets` → `route <ch> --to <target>` |
| Dock › "+ Effect" | a menu of the registry's effects → `device add <ch\|master> --type <t>` |
| Dock › "+N more" chip | a menu of the rest of the rack, each opening its window |
| Dock › a send · dragged sideways | Pre/Post-fader → `set <sd>.pre=…` · Make it the main output → `route` then `send delete` · Remove → `send delete <sd>` · `set <sd>.gain=<dB>` |
| Dock › a strip, right-click (not on a number) | Rename… → `set <ch>.name="…"` · Copy ID · Move its clips to ▸ (the strips of its kind) → `strip relink <ch> --to <ch2>` · Delete strip → `strip delete <ch>` |
| Dock › a strip, right-click › Sidechain to ▸ (its `keyTargets`) | `send add <ch> --to <ch2> --sidechain` — shown "key <target>" in amber |
| Dock › "+ Line" (after a page's last card) | Audio line · Bus · each instrument → `strip add --kind audio\|bus\|instrument [--instrument <type>] --mixer <mx>` |
| Matrix › an open cell · double-click · a send cell · dragged up/down | `send add <ch> --to <col>` · `route <ch> --to <col>` · its menu (as a send) · `set <sd>.gain=<dB>` — the columns are the model's `matrix.columns` (every strip a route may reach, master, out ports — the same `matrix print` prints); a send cell reads its dB, `P` pre-fader, `K` a sidechain key |
| Dock › a fader · a pan · a send's level (card or matrix cell) · the master fader, right-click | the PARAMETER menu (`ParamMenu`, R-MIX-16) on `<ch>.gain` · `<ch>.pan` · `<sd>.gain` · `project.masterGain`: Create Automation → `auto create <address>` · Formula… → cosmo's field → `set <address>="=<typed>"` · Clear Binding → `bind clear <address>` · Reset to Default → `set <address>=0` · Copy Address · Copy Value · Copy as Formula → the host's clipboard |
| Dock › a device chip · an instrument strip's name, double-click | its WINDOW opens or comes forward (the view's) |
| Device window › a slider · a choice (left/right half) · double-click a slider | `set <dv>.<param>=<value in its unit>` · `set <dv>.<param>=<name>` · the registry default |
| Device window › a row, right-click › Create Automation · Formula… · Clear Binding · Reset to Default · Copy Address · Copy Value · Copy as Formula | `auto create <dv>.<param>` · cosmo's field → `set <dv>.<param>="=<typed>"` · `bind clear <dv>.<param>` · `set <dv>.<param>=<default>` · the host's clipboard (`dv_1.filter.cutoff` · `900 Hz` · `=dv_1.filter.cutoff`; a choice: no binding items, no "as Formula") — the SAME menu as the dock's numbers (`ParamMenu`) |
| Device window › On/Bypassed · Remove | `set <dv>.bypass=…` · `device remove <dv>` |
| Device window (a sampler) › a browser sample dropped on it | `set <dv>.sample="<file>"` — ONE line; the window lights while it hovers |
| Device window (an instrument) › Piano Roll | its strip's pattern's roll (a menu when several); none yet → `clip add --strip <ch>`, then its roll |
| Browser › a click on a sample · on the one being heard | `audition "<file>"` · `audition stop` (a double-click still places it) |
| Settings › Playback › PREVIEW LEVEL | `settings set auditionLevel=<dB>` |
| Lanes › a note clip double-clicked | its pattern's piano-roll WINDOW (the view's) |
| Lanes › a clip, right-click | Play through ▸ (the strips of its kind) → `clip move <ac> --strip <ch>` · Piano Roll (a note clip) · Duplicate → `clip duplicate <ac>` · Copy ID → the host's clipboard · Delete → `clip delete <ac>` |
| Piano roll › a click on empty grid · a note dragged (on release) · its right edge dragged | `note add <pt> --pitch <p> --at <cell> --length <last>` · `note move <pt> --pitch <p> --at <b> --to-pitch <p2> --to-at <b2>` · `note move … --length <b>` |
| Piano roll › a velocity stem dragged · a note double-/right-clicked · the end dragged | `note move … --vel <v>` · `note delete <pt> --pitch <p> --at <b>` · `set <pt>.length=<b>` |
| Piano roll › Snap · Notes/Steps · Ctrl+wheel · wheel | nothing — the view's (Steps zooms to fit) |
| Piano roll › Quantize… | `pattern quantize <pt> --grid <snap> [--swing 0.25\|0.5]` |
| Piano roll › Steps › a cell | `note add <pt> --pitch <p> --at <b> --length 0.25` · `note delete …` when one is there |
| A window › its title dragged · × · a click on it | the view's: moved exactly, closed (eased), raised |
| Automation row › a click · a point dragged (on release) · a point double-clicked | `auto point add <au> --at <b> --value <v>` (sent once the double-click has had its chance, ~350 ms; a ghost point at once) · `auto point move <au> --at <b> --to <b2> --value <v>` · `auto point delete <au> --at <b>` — `<b>`/`<b2>` on the lanes' snap step |
| Automation row › right-click a point · the row | Linear/Hold/Smooth/Bezier → `auto point shape <au> --at <b> --shape …`, Delete Point · Copy ID (`au_1`) · Copy as Formula (`=au_1`) → the host's clipboard · Delete Automation → `auto delete <au> --unbind` |
| Automation row › Alt-drag a point · drag a handle · Alt-drag a handle (on release) | ONE `auto point shape <au> --at <b> --shape bezier --speed-in <v> --influence-in <%> --speed-out <v> --influence-out <%>` — symmetric handles · the opposite mirrored · that side alone |
| Automation row › double-click its header · its curve away from a point | its `auto:<au>` WINDOW (the view's); the click's pending point is dropped |
| Automation window › Rename | cosmo's field → `set <au>.name="<typed>"` |

A refusal is the toast, with the service's own sentence (`App::dispatch`). A copy is the toast too — "Copied
<text>", outlined in the accent, not the destructive red (`App::copy`); the text went to the HOST's clipboard
(`App::onCopy`: GTK's CLIPBOARD and PRIMARY in `linux_main.cpp`; `Rig::copied` in the tests).

## Shots — `solaris_app_shots [--outdir D] [--only S] [--size WxH] [--check]`

The REAL App over the REAL service (`tests/Rig.h`): home-empty · home-cards (a long name, a missing
song) · settings-open · settings-mid-open (mid-fade) · settings-chip-changing (a chip's fill
mid-ease) · project-open · home-to-project-mid (mid cross-fade) · toast-refusal ·
browser-instruments · browser-folder · drag-sample-mid (the ghost and the drop hint) ·
clip-dragging · clip-selected-zoomed · lanes-zoomed-in (bars to thirty-seconds, the beats named, Snap 1/128) · lanes-zoomed-out (bars only, Snap Bar) · lanes-zoom-mid (mid Ctrl+wheel: a level mid-fade, the step's names cross-fading) · menu-file-open (Edit open, naming its undo) · mixer-sources · mixer-buses · mixer-tab-mid (pages mid
cross-fade) · mixer-matrix · mixer-folded · device-panel (its window) · device-window-bound (two windows, a bound row, the last change lit) · automation-rows (two curves: smooth, hold, log Hz) · automation-bezier (bezier points with their handle stems, a log-Hz row and a pan row) · automation-window (an automation's window: its facts, a bezier point, two formulas reading it, its value at the playhead) · piano-roll (a bassline, velocities) · piano-roll-note-in (a note mid-fade) · step-mode (a kit's pads, a beat) · browser-audition (a sample being heard, its row filling) · sampler-window (a sampler naming its sound) · loop-region (the brace and the tint) · loop-dragging (mid Shift-drag) · ruler-scrubbing (the playhead held by the pointer, R-TIME-6) · midi-list (the Song tab: New MIDI, patterns with length, notes and players, then samples — R-BROWSE-4) · clip-end-dragging (a MIDI clip's end held, its pattern repeating, seams marked — R-CLIP-7) · loop-brace-moving (the brace taken by its body) · mixer-sidechain (a key in amber, a limiter on the master) · mixer-add-line (the "+ Line" menu) · clip-play-through (a clip's "Play through ▸") · mixer-bound (faders, a pan, a send and the master driven by formulas: their tags, readouts in the accent, values where the transport is) · show-ids (every id: lanes, clips, an automation, the dock, a device window's addresses) · show-ids-mid (the ids mid-fade) · copied-toast (a copy said, in the accent) · dock-folded · confirm-unsaved.
Each at 1440×900 and 1024×640. **Look at them** after a change.

## Borrowed, not copied

`interstellar/app/widgets/TextFit.h`, `EasedScroll.h`, `Glyphs.h`, `AnimatedRows.h`, `FadePage.h` are included in place (namespace
`interstellar_v1`): they draw with the cosmo palette, so they draw teal here. Their right home is
Artboard — a task in `docs/PROGRESS.md`. Cosmo's `ConfirmDialog`, `ContextMenu`, `SliderRow` (with
its opt-in `formatValue`), `Icons`, `Theme`, `EmbeddedFonts` are compiled from `apps/cosmo`.

## Gotchas found building U1

- A path with a space must be quoted in a command line — the grammar tokenizes on spaces.
- After a click changes a list, render a frame before aiming at a control below it: the geometry is
  measured in the paint and the button has moved.
- `ConfirmDialog::advance` is protected: host it in a plain Segment (`mModalRoot`) whose tree advance
  reaches it.
- The embedded Roboto has no "→" — say "to".

## Gotchas found building U2

- A list drawn as `model[i]` at `i × rowH` snaps when the model changes shape. Key it
  (`AnimatedRows`, or a per-id live state as `Timeline::ClipLive`) — and a clip keyed by id needs
  its own eased beat and row, eased with the SAME duration as the rows so it travels with its lane.
- A drag's release is direct manipulation: `set` the live position to where the pointer let go and
  set the target too, before sending the line — or the next `advance` eases it back to the old
  target for a frame until the model arrives.
- Send a command AFTER iterating your own live state: the model it brings back can re-key it.
- The model lists strips in PROCESSING order — the Main bus comes after Mixer 1's strips, so the
  newest strip is not `strips.back()`.
- After a zoom a clip may begin off-screen: aim a test's click at `max(clip.x, kHeaderW)`.

## Gotchas found building U3

- A label measured in the weight it is DRAWN in moves its neighbours when the weight changes (the
  current tab is Medium): the highlight's tween is retargeted and restarts — a one-frame stall a
  mid-tween test caught. Measure geometry in one weight.
- A widget built lazily must build in `advance`, not `bind`: `bind` runs only when the model's
  revision moves, and a click that only SHOWS something (a device panel) moves nothing.
- A culled row takes no input: aim a test at a row only after `reveal` has scrolled it in.
- The arrow "→" is drawn (`arrowText`): the embedded Roboto has no U+2192; "●" is not there either.
- `Rig` publishes `world()`, `cx()`, `cy()` for the shots and the UI tests alike.

## Gotchas found building B3/B4

- **Right-clicks and clicks do not bubble** in Artboard (`dispatchGesture`): the topmost child takes
  them whether it handles them or not. A row's slider swallows a right-click meant for the row — the
  App offers right-clicks to the window layer FIRST (`ProjectScreen::contextClick`).
- **A long readout in a SliderRow runs left over its track** (it right-aligns by estimate): cut it to
  the value column before handing it over.
- **A settings sheet taller than the window scrolls** — a test aims at a control only after
  `revealRect` has brought it in.

## Gotchas found building C1 (the grid follows the zoom)

- **A threshold on the zoom is a pop**: "label every 2nd bar below 56 px" swapped labels in one frame
  while the zoom eased past it. Derive an ALPHA from the eased value (`gridAlpha`, the labels' room)
  instead of a switch on it.
- **`%g` keeps six significant digits**: past beat 1000 a printed beat lost ticks. Print beats with
  `Timeline::beatText` (to the tick, the fewest decimals).
- **One snap for every gesture on the lanes** — `Timeline::snap`. A widget beside the lanes (the
  browser's drop) asks the timeline; it never rounds a beat itself.

## Gotchas found building C3 (R-MIX-16, R-UI-11)

- **A value that follows the engine is SET while playing, but a new driver is EASED onto first** — a
  formula typed while playing jumps the evaluated value; `MixerDock`'s `follow` eases until the catch-up
  ends (retargeting with the time LEFT, so it lands exactly then) and only then sets each frame. Play
  pressed mid-ease starts a catch-up too. Restarting a 220 ms ease every frame on a moving target never
  ends — it chases forever.
- **`bindings[].live` moves without a revision** — the App binds every frame while playing, so the dock
  sees it; stopped, it only moves with a command (a seek, an edit), which bumps the revision.
- **Draw ids from ONE eased amount** (`ProjectScreen::idsAmount`), passed to every widget as a hook — a
  per-widget copy would let the lanes and the dock fade out of step.
- **A label a child draws cannot be faded by its parent**: the device window's addresses are a child
  added AFTER the sliders (`ParamIds`), painting a pill over their labels; Artboard's overlay pass is
  unclipped and over the whole tree, so a lower window's ids would show through a higher one.
- **The UI rig's output plays at quarter speed** (`Rig.h`'s `SilentOut`): a test of something that moves
  while playing must let REAL time pass between frames (`sleep_for`), the rig's clock is fake.

## Gotchas found building C2 (bezier automation, its window)

- **A click that a double-click must be able to cancel waits.** Artboard's recognizer emits `Click` on
  the first release and `DoubleClick` on the second (no `Click`), so a click that adds a point would
  land before the double-click that should open the window. The row's click waits out the
  double-click (`Timeline::advanceAuto`) and draws a ghost point at once (R2: a response this frame).
  A test that clicks an automation row then asserts the `auto point add` must pump ~350 ms first.
- **A handle's release sends the numbers it DRAWS**: format them, parse them back, put THOSE in the
  live state — the model returns exactly the text, so nothing eases after the release.
- **A pan's row is ~36 px of range**: a test's handle drag must stay inside the row, or the drawn end
  clamps at the edge and is no longer under the pointer.
- **At 1024×640 the automation rows sit under the dock**: a shot or a test wheels the lanes
  (`App::wheel`, negative notches = down) before aiming at them.

## The plugins' own editor (R-VST-7, `apps/solaris/plugins`)

The same DevicePanel, inside a VST3 host: `InstrumentEditor` builds a model of one device from the host's
values and turns the panel's lines into the host's edits. Its controls, each the host's edit (the plugin
normalises by the shared mapping):

| gesture | what the host is told |
|---|---|
| a slider dragged | `beginEdit(id)` on the first step, `performEdit(id, normalizedFromValue(value))` per step that moved, `endEdit(id)` at release |
| a choice clicked (left / right half) | begin, perform (the previous / next name), end — at once |
| a slider double-clicked | begin, perform (the registry's default), end |
| the host moves a value (automation, a preset) | nothing back; the thumb springs there and the row is lit |
| a pad pressed · released (Drum Machine) | no edit: the pad heard through the processor (`playNote` → `arstro.note`), the pad picked (its ring eases in, its hit flashes and dies away), the panel scrolled so that pad's group begins at the top · its note-off |

Shots: `solaris_plugin_editor` writes `plugin-editor-synth.png`, `plugin-editor-synth-small.png` (360×320),
`plugin-editor-drums.png`, `plugin-editor-drums-pads.png` (Closed Hat picked, CHAT at the top) into its working directory. Live: `vst3_editorhost build/vst3/ArstroBasicSynth.vst3`.
Gotcha: the SDK's editor host reads `_XEMBED_INFO` from the plugin's window on its CreateNotify — set it
before mapping, in the same batch as the window's creation, or the host exits ("XGetWindowProperty for
_XEMBED_INFO failed").
