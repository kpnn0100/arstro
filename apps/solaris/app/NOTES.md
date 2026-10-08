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

A refusal is the toast, with the service's own sentence (`App::dispatch`).

## Shots — `solaris_app_shots [--outdir D] [--only S] [--size WxH] [--check]`

The REAL App over the REAL service (`tests/Rig.h`): home-empty · home-cards (a long name, a missing
song) · settings-open · settings-mid-open (mid-fade) · settings-chip-changing (a chip's fill
mid-ease) · project-open · home-to-project-mid (mid cross-fade) · toast-refusal · confirm-unsaved.
Each at 1440×900 and 1024×640. **Look at them** after a change.

## Borrowed, not copied

`interstellar/app/widgets/TextFit.h`, `EasedScroll.h`, `Glyphs.h` are included in place (namespace
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
