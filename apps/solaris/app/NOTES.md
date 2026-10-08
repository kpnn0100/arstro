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
| Home / song bar › Settings, Ctrl+, | `devices list`, then the sheet opens |
| Settings › a chip | `settings set output=<id>` · `input=<id>` · `sampleRate=<hz>` · `bufferSize=<frames>` |
| Settings › × on a folder | `folder remove "<path>"` |
| Settings › Add folder… | the host's folder picker → `folder add "<path>"` |
| Song bar › Home | `project close` — behind cosmo's ConfirmDialog when unsaved (Save → `project save` then close) |
| Song bar › Play/Stop, Space | `transport play` / `transport stop` |
| Song bar › Save, Ctrl+S | `project save` |
| Enter | `transport seek 0` |
| Browser › a tab | nothing — the list is the model's (`settings.folders`, `deviceTypes`, the song's clips) |
| Browser › a sample folder, a sub-folder, the row back up | `browse "<path>"` |
| Browser › Samples with no folders, a click | `devices list`, then the settings sheet |
| Browser › a sample dragged onto the lanes | `clip add --src "<file>" --at <beat> [--lane <ln>]` — below the last lane: no `--lane`, a new one |
| Browser › an instrument dragged onto the lanes | `clip add --instrument <type> --at <beat> --length 4 [--lane <ln>]` — ONE line (R-BROWSE-3) |
| Browser › a double-click | the same, at the playhead |
| Browser › an effect dragged onto the lanes | nothing; a notice (effects go on a strip — U3) |
| Lanes › a clip dragged | `clip move <ac> [--at <beat>] [--lane <ln>]` on release (snapped to 1/4 beat; between lanes only) |
| Lanes › a ruler click | `transport seek <beat>` (snapped to 1/4 beat) |
| Lanes › a click on a clip | nothing — selection is the view's (U4 links it to the strip) |
| Lanes › Delete / Backspace with a clip selected | `clip delete <ac>` |
| Lanes › Ctrl+D with a clip selected | `clip duplicate <ac>` (a note clip's copy is linked) |
| Lanes › Ctrl+wheel · wheel · Shift+wheel | nothing — zoom about the pointer, scroll, scroll sideways (the view's) |

A refusal is the toast, with the service's own sentence (`App::dispatch`).

## Shots — `solaris_app_shots [--outdir D] [--only S] [--size WxH] [--check]`

The REAL App over the REAL service (`tests/Rig.h`): home-empty · home-cards (a long name, a missing
song) · settings-open · settings-mid-open (mid-fade) · settings-chip-changing (a chip's fill
mid-ease) · project-open · home-to-project-mid (mid cross-fade) · toast-refusal ·
browser-instruments · browser-folder · drag-sample-mid (the ghost and the drop hint) ·
clip-dragging · clip-selected-zoomed · confirm-unsaved.
Each at 1440×900 and 1024×640. **Look at them** after a change.

## Borrowed, not copied

`interstellar/app/widgets/TextFit.h`, `EasedScroll.h`, `Glyphs.h`, `AnimatedRows.h` are included in place (namespace
`interstellar_v1`): they draw with the cosmo palette, so they draw teal here. Their right home is
Artboard — a task in `docs/PROGRESS.md`. Cosmo's `ConfirmDialog`, `Icons`, `Theme`, `EmbeddedFonts`
are compiled from `apps/cosmo`.

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
