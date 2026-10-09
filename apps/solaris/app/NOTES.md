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
| Song bar › Song › Add Mixer · Add Bus · Add Audio Line · Add Lane | `mixer add` · `strip add --kind bus` · `strip add --kind audio` · `lane add` |
| Song bar › View › Hide/Show Mixer · Hide/Show Browser · Metronome · Settings… | the view's · the view's · `settings set metronome=on\|off` · the sheet |
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
| Browser › a sample dragged onto the lanes | `clip add --src "<file>" --at <beat> [--lane <ln>]` — below the last lane: no `--lane`, a new one |
| Browser › an instrument dragged onto the lanes | `clip add --instrument <type> --at <beat> --length 4 [--lane <ln>]` — ONE line (R-BROWSE-3) |
| Browser › a double-click | the same, at the playhead |
| Browser › an effect dragged onto the lanes | nothing; a notice (effects go on a strip — U3) |
| Lanes › a clip dragged | `clip move <ac> [--at <beat>] [--lane <ln>]` on release (snapped to 1/4 beat; between lanes only) |
| Lanes › a ruler click | `transport seek <beat>` (snapped to 1/4 beat) |
| Lanes › Shift-drag on the ruler · a click inside the loop's brace | `transport loop <from> <to>` on release (both snapped) · `transport loop off` |
| Lanes › a click on a clip | nothing — selection is the view's (U4 links it to the strip) |
| Lanes › Delete / Backspace with a clip selected | `clip delete <ac>` |
| Lanes › Ctrl+D with a clip selected | `clip duplicate <ac>` (a note clip's copy is linked) |
| Lanes › Ctrl+wheel · wheel · Shift+wheel | nothing — zoom about the pointer, scroll, scroll sideways (the view's) |
| Dock › a mixer tab · the chevron · its top edge dragged · a fold header | nothing — the page shown, the dock folded or sized, a group folded (the view's) |
| Dock › "+" in the tabs | `mixer add` |
| Dock › a mixer tab, right-click | Rename… → `set <mx>.name="…"` · Delete mixer → `mixer delete <mx>` |
| Dock › a fader dragged (each step) · double-click | `set <ch>.gain=<dB>` (master: `set project.masterGain=<dB>`) · `…=0` |
| Dock › pan dragged · double-click | `set <ch>.pan=<−1…1>` · `…=0` |
| Dock › M · S | `set <ch>.mute=true\|false` · `set <ch>.solo=true\|false` |
| Dock › "→ out" | a menu of `strips[].targets` → `route <ch> --to <target>` |
| Dock › "+ Effect" | a menu of the registry's effects → `device add <ch\|master> --type <t>` |
| Dock › "+N more" chip | a menu of the rest of the rack, each opening its window |
| Dock › a send · dragged sideways | Pre/Post-fader → `set <sd>.pre=…` · Make it the main output → `route` then `send delete` · Remove → `send delete <sd>` · `set <sd>.gain=<dB>` |
| Dock › a strip, right-click | Rename… → `set <ch>.name="…"` · Move its clips to ▸ (the strips of its kind) → `strip relink <ch> --to <ch2>` · Delete strip → `strip delete <ch>` |
| Dock › a strip, right-click › Sidechain to ▸ (its `keyTargets`) | `send add <ch> --to <ch2> --sidechain` — shown "key <target>" in amber |
| Dock › "+ Line" (after a page's last card) | Audio line · Bus · each instrument → `strip add --kind audio\|bus\|instrument [--instrument <type>] --mixer <mx>` |
| Matrix › an open cell · double-click · a send cell · dragged up/down | `send add <ch> --to <col>` · `route <ch> --to <col>` · its menu (as a send) · `set <sd>.gain=<dB>` |
| Dock › a device chip · an instrument strip's name, double-click | its WINDOW opens or comes forward (the view's) |
| Device window › a slider · a choice (left/right half) · double-click a slider | `set <dv>.<param>=<value in its unit>` · `set <dv>.<param>=<name>` · the registry default |
| Device window › a row, right-click › Create Automation · Formula… · Clear Binding · Reset to Default | `auto create <dv>.<param>` · cosmo's field → `set <dv>.<param>="=<typed>"` · `bind clear <dv>.<param>` · `set <dv>.<param>=<default>` |
| Device window › On/Bypassed · Remove | `set <dv>.bypass=…` · `device remove <dv>` |
| Device window (a sampler) › a browser sample dropped on it | `set <dv>.sample="<file>"` — ONE line; the window lights while it hovers |
| Device window (an instrument) › Piano Roll | its strip's pattern's roll (a menu when several); none yet → `clip add --strip <ch>`, then its roll |
| Browser › a click on a sample · on the one being heard | `audition "<file>"` · `audition stop` (a double-click still places it) |
| Settings › Playback › PREVIEW LEVEL | `settings set auditionLevel=<dB>` |
| Lanes › a note clip double-clicked | its pattern's piano-roll WINDOW (the view's) |
| Lanes › a clip, right-click | Play through ▸ (the strips of its kind) → `clip move <ac> --strip <ch>` · Piano Roll (a note clip) · Duplicate → `clip duplicate <ac>` · Delete → `clip delete <ac>` |
| Piano roll › a click on empty grid · a note dragged (on release) · its right edge dragged | `note add <pt> --pitch <p> --at <cell> --length <last>` · `note move <pt> --pitch <p> --at <b> --to-pitch <p2> --to-at <b2>` · `note move … --length <b>` |
| Piano roll › a velocity stem dragged · a note double-/right-clicked · the end dragged | `note move … --vel <v>` · `note delete <pt> --pitch <p> --at <b>` · `set <pt>.length=<b>` |
| Piano roll › Snap · Notes/Steps · Ctrl+wheel · wheel | nothing — the view's (Steps zooms to fit) |
| Piano roll › Quantize… | `pattern quantize <pt> --grid <snap> [--swing 0.25\|0.5]` |
| Piano roll › Steps › a cell | `note add <pt> --pitch <p> --at <b> --length 0.25` · `note delete …` when one is there |
| A window › its title dragged · × · a click on it | the view's: moved exactly, closed (eased), raised |
| Automation row › a click · a point dragged (on release) · a point double-clicked | `auto point add <au> --at <b> --value <v>` · `auto point move <au> --at <b> --to <b2> --value <v>` · `auto point delete <au> --at <b>` |
| Automation row › right-click a point · the row | Linear/Hold/Smooth → `auto point shape <au> --at <b> --shape …`, Delete Point · Delete Automation → `auto delete <au> --unbind` |

A refusal is the toast, with the service's own sentence (`App::dispatch`).

## Shots — `solaris_app_shots [--outdir D] [--only S] [--size WxH] [--check]`

The REAL App over the REAL service (`tests/Rig.h`): home-empty · home-cards (a long name, a missing
song) · settings-open · settings-mid-open (mid-fade) · settings-chip-changing (a chip's fill
mid-ease) · project-open · home-to-project-mid (mid cross-fade) · toast-refusal ·
browser-instruments · browser-folder · drag-sample-mid (the ghost and the drop hint) ·
clip-dragging · clip-selected-zoomed · menu-file-open (Edit open, naming its undo) · mixer-sources · mixer-buses · mixer-tab-mid (pages mid
cross-fade) · mixer-matrix · mixer-folded · device-panel (its window) · device-window-bound (two windows, a bound row, the last change lit) · automation-rows (two curves: smooth, hold, log Hz) · piano-roll (a bassline, velocities) · piano-roll-note-in (a note mid-fade) · step-mode (a kit's pads, a beat) · browser-audition (a sample being heard, its row filling) · sampler-window (a sampler naming its sound) · loop-region (the brace and the tint) · loop-dragging (mid Shift-drag) · mixer-sidechain (a key in amber, a limiter on the master) · mixer-add-line (the "+ Line" menu) · clip-play-through (a clip's "Play through ▸") · dock-folded · confirm-unsaved.
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
