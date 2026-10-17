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
| Any shortcut on this page | the DEFAULT keys: each is a keymap action (R-SET-4) — `settings set key.<action>=<combos>` re-keys it, the model's `settings.keys` has this machine's; `App::key` looks every press up there (a roll with the keys first). API.md › Keyboard lists them |
| Opening a song with a newer autosave › Recover · Open the Saved One | `project recover <path>` · `project discard-recovery <path>` (R-SAFE-2) |
| Settings › Interface › Autosave (combo) | `settings set autosave=<seconds|off>` (R-SAFE-1) |
| Browser › a `.mid` dragged onto an instrument's track · onto empty space | `pattern import <file> --lane <ln> --at <b>` · `… --instrument synth --at <b>` (R-MIDI-1) |
| Song bar › the `solaris.` wordmark | Home: `project close` (an unsaved song asks Save / Discard / Cancel first) — R-UI-3 amended |
| Home / song bar › Settings (beside the wordmark), Ctrl+, | `devices list`, then the sheet opens |
| Song bar › File › New Song… · Open… · Save · Save As… · Render… · Export… · Render Stems… · Home | picker → `project new` · picker → `project open` · `project save` · picker → `project save "<path>"` · picker → `render --out "<path>"` · the Export sheet, then picker → `render --out "<path>" --format <f> [--rate <Hz>] (--bits <b> \| --kbps <k>) [--normalize <LUFS> --ceiling <dBTP>] [--stems all]` (R-EXPORT-1) — with the video on (R-VIDEO-1): `render --out "<path>.mp4\|.mkv" --video [--rate <Hz>] [--normalize <LUFS> --ceiling <dBTP>]` · picker → `render --out "<path>" --stems <every strip>` · `project close` |
| Song bar › Edit › Undo · Redo · Duplicate Clip · Delete Clip | `undo` · `redo` · `clip duplicate <ac>` · `clip delete <ac>` |
| Song bar › Song › Add Mixer · Add Bus · Add Audio Line · Add Lane · Quantize Clip | `mixer add` · `strip add --kind bus` (placed before Main, feeding it) · `strip add --kind audio` · `lane add` · `pattern quantize <pt> --grid <the lanes' snap step, 1/32 when none>` (the selected note clip's pattern) |
| Song bar › View › Hide/Show Mixer · Hide/Show Browser · Metronome · Show/Hide IDs · Settings… | the view's · the view's · `settings set metronome=on\|off` · `settings set showIds=on\|off` (R-UI-11) · the sheet |
| Song bar › File › Collect and Save | `project collect --save` — every file the song names, every version's too, gathered into its folder's `Samples/` and saved; a toast says it (and what is still missing) (R-COLLECT-1) |
| Song bar › File › Relink… | `project files --missing` (a read: a file may have gone since), then the Relink window over the song (R-COLLECT-1) |
| Relink › a row's Locate… · Search a Folder… · Done, Escape, Enter, the scrim | the host's file picker → `project relink "<file>" --to "<picked>"` · the host's folder picker → `project relink --folder "<dir>"` · the view's — a found row fades out where it is, the rest slide up, the card shrinks with them |
| A song opened with files missing | a toast: "N files are missing — File › Relink…" (once per song opened) |
| Song bar › View › Show Session / Show Arrangement | the view's — the session grid cross-fades into the lanes' place (R-LAUNCH-1) |
| Session › a slot · its right-click › Rename… · Delete Slot | `launch <sl>` (stopped: the transport starts, it plays at once) · cosmo's field → `set <sl>.name="…"` · `slot delete <sl>` |
| Session › an empty cell | a menu of the song's patterns (an instrument's column) or the strip's clips (an audio column) → `slot add <sc> <ch> --pattern <pt>` / `--clip <ac>` |
| Session › a scene's ▶ · its name, right-click › Rename… · Delete Scene · "+ Scene" | `launch <sc>` · `set <sc>.name="…"` · `scene delete <sc>` · `scene add` |
| Session › a column's ■ · Back to Arrangement | `launch stop <ch>` · `launch back` — a playing slot fills with its strip's colour (eased), its loop's progress along its foot; a queued one's outline pulses until it lands |
| Song bar › View › Show Video / Hide Video · the Video window's × | the view's — the Video window (R-VIDEO-1): the frame at the model's `video.time`, from the HOST's decoder (`App::onVideoFrame`), asked only when the time, the file or the box changes; "Before / After the picture", "No picture in this song" outside it; nothing sent |
| Lanes › the picture's row (between the ruler and the lanes) · its span dragged sideways | the pointer's while held, nothing sent; on release ONE `set <vd>.at=<beat>` (on the grid you see; before the song too) — R-VIDEO-1 |
| Export › With the video | greyed while the song has no picture; on: Format MP4 / MKV, the bit depth, quality and stems greyed; Export… → `render --out <f> --video …` |
| Keyboard: Tab / Shift+Tab · Enter / Space · ← → ↑ ↓ (Shift: fine) · Escape | the focus to the next / previous control (`App::accessibles`, a modal's alone) · the click it stands for (the line a click sends) · a value's step (a fader ±0.5 dB, a pan ±5 %, a parameter by its registry step, a combo's next choice — ONE line) · let go; when the focus is not held these keys are the keymap's (R-ACCESS-1) |
| A screen reader (AT-SPI): a control's click · increment / decrement | `App::activateNode` · `App::stepNode` — the keyboard's, so the line a click sends; the list it reads is `App::accessibles`, given by the window's host through ATK (`host/Accessibility.h`) when a screen reader asks, re-read every 200 ms and when the focus moves (R-ACCESS-1) |
| Settings › Interface › Language | `settings set language=auto\|en\|vi\|en-XA` — Automatic (the system's, when Solaris has it), then each language by its own name; the window dissolves to it (R-ACCESS-2) |
| Settings › Interface › UI scale · High contrast | `settings set uiScale=<75…200>` · `settings set highContrast=on\|off` — the window zooms to the scale and the tokens fade, both eased (R-ACCESS-1) |
| Song bar › File › Conflicts… · (opened on its own when a sync asks) | the Conflicts sheet (R-COLLAB-1): a row's Keep Mine / Take Theirs → `project sync --take mine\|theirs <node>`; Keep All Mine / Take All Theirs → `project sync --take mine\|theirs` (inert with nothing asked); Done, Escape, Enter close it. A toast says each sync (`sync.said`) |
| Song bar › the version chip (after the menus: "main", or the open version's name, " · frozen" when it is) | its menu: the versions as a tree under main (four spaces a level; " · open", " · frozen", " · does not stand") → `version open main\|<vr>` · New Version… → cosmo's field → `version new "<name>" [--base <open vr>] --open` (R-VER) |
| Song bar › the version chip, a version open › Freeze / Thaw · Rebase · Rename… · Delete Version | `version freeze\|thaw <vr>` · `version rebase <vr>` · cosmo's field → `set <vr>.name="<name>"` · `version delete <vr>` — the chip's label cross-fades, its width eases to it (the name and the transport sliding with it), its accent ring eases in while a version, not main, is open |
| Settings › a section in the rail | nothing — its page cross-fades in, the highlight slides (the view's, R-SET-5) |
| Settings › a combo box, then a row of its list | `settings set output=<id>` · `input=<id>` · `sampleRate=<hz>` · `bufferSize=<frames>` · `metronomeLevel=<dB>` · `auditionLevel=<dB>` · `newBpm=<bpm>` · `newSig=<n/d>` (R-SET-6) |
| Settings › a toggle (Metronome, Reduced motion) | `settings set metronome=on\|off` · `reducedMotion=on\|off` |
| Settings › × on a folder | `folder remove "<path>"` |
| Settings › Keyboard › a key chip, then a key · "+", then a key | listens ("Press a key…"), then `settings set "key.<action>=<its keys, that slot replaced or added>"` — refused naming the action that has it (the toast) (R-SET-4) |
| Settings › Keyboard › Backspace · Escape while listening | `settings set "key.<action>=<the rest>"` (`none` when it was the last) · nothing — it stops listening |
| Settings › Keyboard › Reset to defaults | `settings set key.<a>=default key.<b>=default …` — every changed action, ONE line |
| Settings › Add folder… | the host's folder picker → `folder add "<path>"` |
| Song bar › Home | `project close` — behind cosmo's ConfirmDialog when unsaved (Save → `project save` then close) |
| Song bar › Play/Stop, Space | `transport play` / `transport stop --return` — back where the time bar was put (R-TIME-7) |
| Ctrl+Space | stopped: as Space; playing: `transport stop` — the time bar stays where it was heard, for now: the next play starts there, and Space's stop after it goes back to where the time bar was put (R-TIME-8) |
| Song bar › the tempo dragged up/down (Shift: fine) · double-clicked | `set project.bpm=<n>` per step that moved it · the App's field → one `set project.bpm=<typed>` (R-UI-13) |
| Song bar › Save, Ctrl+S | `project save` |
| Enter | `transport seek 0` |
| Ctrl+Z · Ctrl+Shift+Z / Ctrl+Y | `undo` · `redo` — up the history tree / down its newest branch (R-HIST-1) |
| Edit › Show History Tree… | nothing — cosmo's history view opens over the model's `history[]` (R-HIST-3) |
| History tree › a node | `history jump <id>` — the view stays, its highlight travels to the node |
| History tree › the undo / redo key · Escape · drag · wheel | `undo` / `redo` · closes it · pans · pans a row a notch (the view's) |
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
| Lanes › a group's chevron (on an open group's first lane, or its folded row) | `set <lg>.folded=on\|off` — the chevron turns, the lanes fold into one row, eased (R-ARR-3) |
| Lanes › a clip dragged on a folded group's row | `clips move "<ac> …" --by <beats>` on release — every clip of the group, carried with the pointer (R-ARR-3) |
| Lanes › a lane header right-clicked · a folded row's header right-clicked | … New group → `lane group <ln> --name "Group <n>"` · Into group… → `set <ln>.group=<lg>` · Leave group → `set <ln>.group=none` · Fold group · Ungroup → `lane ungroup <lg>` · — · Open group → `set <lg>.folded=off` · Rename group… → `set <lg>.name=<n>` · Ungroup · Copy ID |
| Mixer › a strip's R (audio and instrument strips) | `strip arm <ch>` · `strip arm none` when armed · an audio line with no input: "Arm with <port>" → `strip arm <ch> --input <port>`, or "Add an input port (In)" → `port add In --dir in` (R-REC-1) |
| Mixer › an audio line's menu › Input… · Monitor… | `set <ch>.input=<port>\|none` · `port add "<name>" --dir in` (named in cosmo's field) · `set <ch>.monitor=auto\|on\|off` (R-REC-1) |
| Lanes › a clip with takes, right-clicked › Show takes · Hide takes | nothing — the view's: a row per take under its lane (R-REC-2) |
| Lanes › an audio clip's fade handle (the square under its name at either end) dragged · its gain line dragged up/down | `set <ac>.fadeIn=<beats>` / `.fadeOut=<beats>` · `set <ac>.gain=<dB>` — ONE line when let go, the drawing the pointer's while held; the line is the amplitude, 0 dB a quarter up, +12 at the top (R-EDIT-1) |
| Lanes › an audio clip, right-clicked › Split at the time bar · Reverse / Play forwards · Consolidate (N clips) · Slice ▸ Every bar / beat / half beat · At its hits · To a kit, a slice a beat | `clip split <ac> --at <time bar>` · `set <ac>.reverse=true\|false` · `clip consolidate <ac> …` (the clips it runs into on its lane through its strip, touching or overlapping) · `clip slice <ac> --every <b>` · `--transients` · `--every 1 --kit` (R-EDIT-1; no Slice on a loop, no At its hits on a comped clip) |
| Ctrl+E (keymap `lanes.split`) · Edit › Split Clip at the Time Bar | `clip split <selected> --at <time bar>` (R-EDIT-1) |
| Ruler band › a tempo flag dragged · clicked · right-clicked › Tempo… · Glide into it / Step to it · Delete tempo change · Copy ID | `tempo move <tp> --at <b>` (when let go) · `transport seek <b>` · `tempo move <tp> --bpm <typed>` · `tempo move <tp> --ramp true\|false` · `tempo delete <tp>` (R-TEMPO-1) |
| Ruler › right-clicked › Tempo change here… | `tempo add --at <b> --bpm <typed>` (R-TEMPO-1) |
| Browser › Presets tab › an instrument's preset dragged onto the lanes (or double-clicked) | `clip add --instrument <type> --preset <name> --at <b> --length 4 [--lane …]` (R-PRESET-1) |
| Device window › Presets › a preset · Save as preset… | `preset load <dv> "<name>"` · `preset save <dv> "<typed>"` (R-PRESET-1) |
| Device window of a plugin › Editor (where Presets stands; lit while its window is open) | `device editor <dv>` · open: `device editor <dv> --close` (R-VST-6, N26) |
| Device window of a plugin, offline › the band under the header › Reload | `device reload <dv>` (R-VST-6) |
| A number's right-click (a fader, a pan, a send, the master, a device row or knob) › Learn MIDI CC · Forget CC <n> | `map learn <address>` · `map remove <n>` (R-CTRL-1) |
| Song bar › the MIDI learn pill (while a learn waits) | `map learn --cancel` (R-CTRL-1) |
| Settings › MIDI › a mapping's × | `map remove <cc>` (R-CTRL-1) |
| Settings › MIDI › Control surface · Surface port | `settings set surface=none\|mcu\|hui` · `settings set "surfacePort=<port>"` (R-CTRL-2) |
| Home › From template… · File › New from Template… › a template | the host asks where, then `project new <path> --template <name>` (R-PRESET-2) |
| File › Save as Template… | `template save "<typed>"` (R-PRESET-2) |
| Browser › Samples › Search samples… (typed) · Favourites · the search's first row | `browse --search <text>` · `browse --favourites` · nothing — back to the folders (R-PRESET-3) |
| Browser › a sound's star · right-click › Tag… · Remove tag · <tag> · Favourite / Not a favourite | `sample favourite <file> on\|off` · `sample tag <file> <typed>` · `sample untag <file> <tag>` · `sample favourite …` (R-PRESET-3) |
| Song bar › the tempo dragged / typed (with tempo changes) | the change in effect at the time bar: `tempo move <tp> --bpm <v>`, or `set project.bpm=<v>` before the first (R-TEMPO-1) |
| Lanes › an audio clip, right-clicked › Warp to the song's tempo / Unwarp · Tempo… (typed) · Pitch ▸ +12 … −12 | `set <ac>.warp=true\|false` (on with no tempo: heard first, refused when none is) · `set <ac>.bpm=<typed>` · `set <ac>.pitch=<n>`; the clip's tag "60 bpm · +7 st" cross-fades (R-WARP-1) |
| Lanes › a take's row › dragged across · double-clicked | `clip comp <ac> --take <k> --from <b> --to <b>` on release · `clip comp <ac> --take <k>` (the whole clip) |
| Lanes › the ruler right-clicked › Punch the loop's span · Punch off | `transport punch <loop from> <loop to>` · `transport punch off` — a red line atop the bars (R-REC-1) |
| Lanes › the ruler's top band › a marker's flag clicked · double-clicked · dragged | `transport seek <mk>` · `transport loop <mk>` · `marker move <mk> --at <b>` on release (R-ARR-1) |
| Lanes › a marker's flag right-clicked · the ruler right-clicked elsewhere | Loop its section · Rename… → `marker rename <mk> "<name>"` · Delete marker → `marker delete <mk>` · Copy ID · Add marker here → `marker add "Marker <n>" --at <b>` · Insert a bar here → `time insert --at <bar> --length <a bar>` · Delete this bar → `time delete --from <bar> --to <bar + a bar>` · Delete the loop's time (with a loop) → `time delete --from <loop> --to <loop end>` (R-ARR-2) |
| Lanes › the loop's brace dragged by its body · by an end (the lower half of the ruler's bars) | `transport loop <from> <to>` ONCE on release: moved with its length kept · resized, never past the other end (R-TIME-6) |
| Lanes › a click on a clip | nothing — selection is the view's (U4 links it to the strip) |
| Lanes › Delete / Backspace with a clip selected | `clip delete <ac>` |
| Lanes › Ctrl+D with a clip selected | `clip duplicate <ac>` (a note clip's copy is linked) |
| Pointer over a resize grip (a clip's end, the loop brace's ends, a note's end, the pattern's end · the dock's top edge, a velocity bar · a window's corner) | nothing — the cursor is ↔ · ↕ · ⤡ there and while it is held (`App::cursor`, R-UI-14) |
| Lanes › a clip dragged with Shift held when let go | `clip duplicate <ac> --at <beat> [--lane <ln>]` — the original stays; a "+" on the copy while Shift is held (R-CLIP-9) |
| Lanes › Ctrl+C · Ctrl+V (Edit › Copy Clip · Paste Clip) | nothing (the window keeps the clip; a toast) · `clip duplicate <ac> --at <the time bar>` — again, the time bar unmoved: right after the last copy (R-CLIP-10) |
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
| Dock › "+ Line" (after a page's last card) | Audio line · Bus · VCA · each instrument → `strip add --kind audio\|bus\|vca\|instrument [--instrument <type>] --mixer <mx>` |
| Dock › a strip, right-click › VCA ▸ (the song's VCAs; None when it follows one) | `set <ch>.vca=<vca>\|none` (R-MIX-19) — then "VCA <name> <dB>" over its fader, faded in, its dB easing as the VCA moves (sliding right beside a bound gain's tag) |
| Dock › a VCA's card (R-MIX-19) | "VCA · moves N", its members named where a rack would be; a fader (`set <vca>.gain=…`), M and S — no meter, pan, sends or route (none to press); its menu offers no Freeze, Bounce or VCA |
| Dock › a strip, right-click › Invert Phase (" · on" when on) | `set <ch>.phase=on\|off` (R-MIX-20) — then a "Ø" in the accent at the right of its card's sub line, faded in |
| Dock › a strip, right-click › Width ▸ (0 % (mono) · 50 · 100 · 150 · 200, " · now" on the current) | `set <ch>.width=<0…200>` — off 100 its card reads "<w>%" beside the Ø, faded in; the sub text ellipsizes to leave room |
| Dock › a bus, right-click › Solo-safe (" · on" when on; buses only) | `set <bus>.soloSafe=true\|false` — its S gets a ring, eased in: a solo elsewhere no longer silences it |
| Dock › the master's "Mono" pill (where a pan would be) | `transport mono on\|off` — the whole mix folded to mono at the output, a check, not an edit; the pill eases to the accent while on |
| Matrix › an open cell · double-click · a send cell · dragged up/down | `send add <ch> --to <col>` · `route <ch> --to <col>` · its menu (as a send) · `set <sd>.gain=<dB>` — the columns are the model's `matrix.columns` (every strip a route may reach, master, out ports — the same `matrix print` prints); a send cell reads its dB, `P` pre-fader, `K` a sidechain key |
| Dock › a fader · a pan · a send's level (card or matrix cell) · the master fader, right-click | the PARAMETER menu (`ParamMenu`, R-MIX-16) on `<ch>.gain` · `<ch>.pan` · `<sd>.gain` · `project.masterGain`: Create Automation → `auto create <address>` · Formula… → cosmo's field → `set <address>="=<typed>"` · Clear Binding → `bind clear <address>` · Reset to Default → `set <address>=0` · Copy Address · Copy Value · Copy as Formula → the host's clipboard |
| Dock › a device chip · an instrument strip's name, double-click | its WINDOW opens or comes forward (the view's) |
| Device window › a slider · a choice (left/right half) · double-click a slider | `set <dv>.<param>=<value in its unit>` · `set <dv>.<param>=<name>` · the registry default — an EFFECT's list |
| Instrument window (its analog face, R-UI-12) › a knob dragged up/down (Shift: 5× finer) · double-clicked · right-clicked | `set <dv>.<param>=<value>` at each step that moved it · the registry default · the parameter's menu (R-WIN-3); a bound knob names its binding and takes no drag |
| Instrument window › a switch's key | `set <dv>.<param>=<choice>` — its light slides there; a screen morphs through the shapes between |
| Device window › a row, right-click › Create Automation · Formula… · Clear Binding · Reset to Default · Copy Address · Copy Value · Copy as Formula | `auto create <dv>.<param>` · cosmo's field → `set <dv>.<param>="=<typed>"` · `bind clear <dv>.<param>` · `set <dv>.<param>=<default>` · the host's clipboard (`dv_1.filter.cutoff` · `900 Hz` · `=dv_1.filter.cutoff`; a choice: no binding items, no "as Formula") — the SAME menu as the dock's numbers (`ParamMenu`) |
| Device window › On/Bypassed · Remove | `set <dv>.bypass=…` · `device remove <dv>` |
| Device window (a sampler, a convolution reverb) › a browser sample dropped on it | `set <dv>.sample="<file>"` — ONE line; the window lights while it hovers (any type that takes a sample: its response, for the reverb) |
| Device window (a device that measures — the tuner) › its band under the header | nothing sent: drawn from `devices[].readings` (R-FX-17) — the note large (26 px, a display), a needle across −50 … +50 cents easing to each reading (`kCatchUpMs`; greener the nearer the centre, from the eased value), the rest small; no pitch: a dash, the needle home and faded. The rows below it (`readingsH`, `needleCents`, `pitchedAmount`) |
| Device window (an instrument) › Piano Roll | its strip's pattern's roll (a menu when several); none yet → `clip add --strip <ch>`, then its roll |
| Browser › a click on a sample · on the one being heard | `audition "<file>"` · `audition stop` (a double-click still places it) |
| Settings › Playback › Preview level | `settings set auditionLevel=<dB>` |
| Lanes › a note clip double-clicked | its pattern's piano-roll WINDOW (the view's) |
| Lanes › a clip, right-click | Play through ▸ (the strips of its kind) → `clip move <ac> --strip <ch>` · Piano Roll (a note clip) · Duplicate → `clip duplicate <ac>` · Copy ID → the host's clipboard · Delete → `clip delete <ac>` |
| Piano roll › a click on empty grid · a note dragged (on release) · its right edge dragged | `note add <pt> --pitch <p> --at <cell> --length <last>` · `note move <pt> --pitch <p> --at <b> --to-pitch <p2> --to-at <b2>` · `note move … --length <b>` |
| Piano roll › a velocity stem dragged · a note double-/right-clicked · the end dragged | `note move … --vel <v>` · `note delete <pt> --pitch <p> --at <b>` · `set <pt>.length=<b>` |
| Piano roll › a note added · pressed · dragged into a new row · a key pressed or swept · a Steps cell on | beside the edit's own line: `note preview <ch> <pitch> --vel <v> --length <b>` through the clip's track — never in undo (R-ROLL-7) |
| Piano roll › a click on a note · Shift+click · Ctrl+drag anywhere on the grid (even from a note) · Ctrl+A · Escape | the selection — the view's (R-ROLL-8) |
| Piano roll › the selection dragged · ↑/↓ (Shift: octave) · ←/→ | `notes move <pt> "<pitch@beat> …" --by <beats> --semi <n>` — ONE line |
| Piano roll › Delete/Backspace · Ctrl+D · Ctrl+C then Ctrl+V (any roll) | `notes delete <pt> "…"` · `notes add <pt> "<pitch@beat:len:vel> …"` (a copy after the selection) · the shared clipboard, then `notes add` at the same beats |
| Piano roll › ▶ (■ while its clip loops) · Space with a roll in front, stopped | `transport play --clip <ac>` (the clip it was opened from, else the pattern's first) · `transport stop --return` (R-TIME-7); Space stops whatever plays (R-ROLL-6) |
| Piano roll › Snap · Notes/Steps · Ctrl+wheel · wheel | nothing — the view's (Steps zooms to fit) |
| Piano roll › Length ▾ (its menu) | nothing — the length a click makes (the view's; R-ROLL-10) |
| A window's bottom-right corner dragged | nothing — its size (the view's; R-WIN-5) |
| Piano roll › Quantize… | `pattern quantize <pt> --grid <snap> [--swing 0.25\|0.5]` · `… --groove <name>` (R-ROLL-11); ` --notes "<sel>"` with a selection |
| Piano roll › Scale ▾ · its Change key… | `pattern scale <pt> <key> <scale>` · `pattern scale <pt> off` (R-ROLL-11) — the rows off it dim |
| Piano roll › Chord ▾ | nothing — the view's: a click on the grid stamps `note add <pt> --chord <root><q> --octave <o> --at <b> --length <l>` |
| Piano roll › Tools ▾ | `pattern humanise` · `pattern legato` · `pattern arpeggiate <pt> --rate <snap> --mode up\|down\|updown\|random` · `notes set <pt> "<sel>" --chance 0.5\|1` — on the selection (`--notes`), else the pattern (R-ROLL-11) |
| Piano roll › Steps › a cell | `note add <pt> --pitch <p> --at <b> --length 0.25` · `note delete …` when one is there |
| A window › its title dragged · × · a click on it | the view's: moved exactly, closed (eased), raised |
| Automation row › a click · a point dragged (on release) · a point double-clicked | `auto point add <au> --at <b> --value <v>` (sent once the double-click has had its chance, ~350 ms; a ghost point at once) · `auto point move <au> --at <b> --to <b2> --value <v>` · `auto point delete <au> --at <b>` — `<b>`/`<b2>` on the lanes' snap step |
| Automation row › right-click a point · the row | Linear/Hold/Smooth/Bezier → `auto point shape <au> --at <b> --shape …`, Delete Point · Copy ID (`au_1`) · Copy as Formula (`=au_1`) → the host's clipboard · Delete Automation → `auto delete <au> --unbind` |
| Automation row › Alt-drag a point · drag a handle · Alt-drag a handle (on release) | ONE `auto point shape <au> --at <b> --shape bezier --speed-in <v> --influence-in <%> --speed-out <v> --influence-out <%>` — symmetric handles · the opposite mirrored · that side alone |
| Automation row › double-click its header · its curve away from a point | its `auto:<au>` WINDOW (the view's); the click's pending point is dropped |
| Automation row (looping) › the span's start · end grip dragged (on release) | `set <au>.at=<b> <au>.length=<n>` (the end kept) · `set <au>.length=<n>` — the resize cursor over them; a click in a repeat adds its point in the cycle (R-AUTO-12) |
| Automation window › Loop (combo) | `set <au>.loop=<beats>` — Off = 0 (R-AUTO-12) |
| Right-click a number › Create Pump | `auto create <address> --pump` — a one-beat dip, looping (R-AUTO-13) |
| Automation window › Rename | cosmo's field → `set <au>.name="<typed>"` |
| Mixer › a meter's clip light (any strip, the master) | `meters clear` — every light out, the holds zeroed, the master's true peak and integrated loudness started again (R-METER-2) |
| Song bar › ● (before ▶) | `transport record` · `transport stop` while recording — the take lands as one undo (R-MIDI-2) |
| Settings › Playback › Record quantize · Count-in (combos) | `settings set recordQuantize=off\|1/4\|…\|1/32` · `settings set countIn=<bars>` (R-MIDI-2) |
| Lanes › an instrument track's header › the arm ring | `strip arm <ch>` · `strip arm none` when it is armed (R-MIDI-2) |
| Settings › Audio › MIDI input (combo) | `settings set midiInputs=all\|none\|<port>` (R-MIDI-2) |
| Piano roll › Vel · Bend · Mod · Chance (beside the lower lane) | nothing — the view's: what the lane shows (R-MIDI-3, R-ROLL-11) |
| Piano roll › the Chance lane › a stem dragged | `note move <pt> --pitch <p> --at <b> --chance <0…1>` on release (R-ROLL-11) |
| Piano roll › a wheel's lane › a click · a point dragged up/down · a point double-clicked | `expr add <pt> --lane bend\|cc1 --at <b> --value <v>` · the same at its beat, on release · `expr delete <pt> --lane … --at <b>` |
| Settings › Audio › Ports › a port's device field (menu) | `settings set port.<name>=<device>:<channel>` — or `port.<name>=` (back on the clock / the default input) (R-DEV-1, R-DEV-3); the line under its name says where it plays — playing, how its device is (R-DEV-5) |
| Settings › Audio › Driver (combo) | `settings set driver=pulse\|pipewire` (R-DEV-9) |
| Mixer › right-click a strip › Freeze / Unfreeze | `strip freeze <ch>` / `strip unfreeze <ch>` (R-PERF-3) — Freeze only on a strip nothing feeds (`strips[].fromStrips` empty), Unfreeze on a frozen one; frozen, its rack recedes under a FROZEN pill (`frozenAmount`, eased) |
| Mixer › right-click a strip › Bounce to a new strip | `strip bounce <ch>` (R-PERF-3) — its own sound as a clip at beat 0 on a new strip; only on a strip nothing feeds |
| Mixer › right-click a strip › Duck Under ▸ <strip> | `strip duck <ch> --under <key>` — a keyed compressor and its pre-fader key in one line (R-MIX-18); the strips offered are those whose `keyTargets` include it |
| Mixer › the master's LUFS chip (where a strip has M / S) | its `loudness` WINDOW (the view's; R-METER-1) |
| Loudness window › Reset | `meters clear` |

A refusal is the toast, with the service's own sentence (`App::dispatch`). A copy is the toast too — "Copied
<text>", outlined in the accent, not the destructive red (`App::copy`); the text went to the HOST's clipboard
(`App::onCopy`: GTK's CLIPBOARD and PRIMARY in `linux_main.cpp`; `Rig::copied` in the tests).

## Shots — `solaris_app_shots [--outdir D] [--only S] [--size WxH] [--check]`

The REAL App over the REAL service (`tests/Rig.h`): home-empty · home-cards (a long name, a missing
song) · settings-open (the Audio page: combo boxes) · settings-ports (Settings › Audio's ports: Phones on the USB interface, Mic on its input, each with its line — R-DEV-1/5) · settings-ports-arriving (a port added, its row mid-grow) · settings-folders (its page) · settings-mid-open (mid-fade) · settings-combo-open (a list
dropping over the page) · settings-page-changing (Audio fading out as Playback fades in, the highlight on its way) · history-tree (a branched history in cosmo's view) · clip-copy-dragging (Shift held: the original at full strength, a "+" on the copy in hand) · automation-pump (a pump looping from beat 4 for 8: the first cycle bright, the repeats faint, its brace; its window's Loop combo) · history-tree-jumping (a node clicked, the highlight mid-way) · settings-keyboard (re-keyed actions dotted, Reset live) · settings-key-listening (a chip mid cross-fade to "Press a key…") · project-open · home-to-project-mid (mid cross-fade) · toast-refusal ·
browser-instruments · browser-folder · drag-sample-mid (the ghost and the drop hint) ·
clip-dragging · clip-selected-zoomed · lanes-zoomed-in (bars to thirty-seconds, the beats named, Snap 1/128) · lanes-zoomed-out (bars only, Snap Bar) · lanes-zoom-mid (mid Ctrl+wheel: a level mid-fade, the step's names cross-fading) · menu-file-open (Edit open, naming its undo) · mixer-sources · mixer-buses · mixer-tab-mid (pages mid
cross-fade) · mixer-matrix · mixer-folded · device-panel (its window) · device-window-bound (two windows, a bound row, the last change lit) · automation-rows (two curves: smooth, hold, log Hz) · automation-bezier (bezier points with their handle stems, a log-Hz row and a pan row) · automation-window (an automation's window: its facts, a bezier point, two formulas reading it, its value at the playhead) · piano-roll (a bassline, velocities) · piano-roll-note-in (a note mid-fade) · piano-roll-bend (the lane on Bend: a dip and a rise through its points — R-MIDI-3) · piano-roll-tools (D minor: the rows off it dimmed, a stamped chord, the lane on Chance, faint notes, Chord m7 — R-ROLL-11) · piano-roll-scale-fading (D minor → D major, the rows mid-fade) · piano-roll-playing (its ▶ lit, the clip looping, the playhead in the pattern — R-ROLL-6) · step-mode (a kit's pads, a beat) · browser-audition (a sample being heard, its row filling) · sampler-window (a sampler naming its sound) · markers (the ruler's band: Intro, Verse, Chorus as a section, looped, Outro — R-ARR-1) · markers-arriving (a marker fading in, another easing to where a shell moved it) · lane-groups (Drums and Bass a group, open: its bar, its chevron and name — R-ARR-3) · lane-group-folded (ONE row, each lane's clips in its band, "▸ 2 LANES") · lane-group-folding (mid-fold) · takes (the vocal's three takes shown under it: take 1 lit over its start, take 3 the rest — R-REC-2) · clip-edit (a drum loop's fades and lifted gain line, a second over its end crossfading — the X — and a reversed vocal — R-EDIT-1) · clip-edit-dragging (the fade in held by the pointer, two beats long) · clip-warp (the drums warped at 120, the vocal at 60 and +7: their tags, clear of the fades and the gain lines — R-WARP-1) · tempo-map (140 from bar 3 gliding to 100 by bar 5: the flags and the slope; the song bar at 130, the glide's tempo at the time bar — R-TEMPO-1) · home-templates (Home's From template… open: the shipped templates — R-PRESET-2) · browser-library (a sample folder: the favourite's star lit, tags beside two sounds — R-PRESET-3) · browser-presets (the Presets tab: the shipped ones by type, the user's "My Bass" beside them — R-PRESET-1) · device-presets-menu (a synth's window, its Presets open) · plugin-device (another maker's effect's window: its list as percentages, Editor lit while its own window is open — R-VST-6) · plugin-offline (an instrument plugin that crashed: the band under the header, why, Reload) · plugin-offline-mid (the band sliding open, the rows pushed with it) · midi-learn (a fader's menu with Learn MIDI CC and Forget CC 74; the song bar's pill, a learn waiting — R-CTRL-1) · settings-midi (Settings › MIDI: a Mackie Control surface connected — its note; three mappings, one the song lacks dimmed — R-CTRL-1/2) · recording-audio (the vocal line armed — its R lit red — a punch over the loop, R-REC-1) · ruler-menu (the ruler right-clicked: a marker, a bar opened or taken out, the loop's time — R-ARR-1/2) · loop-region (the brace and the tint) · loop-dragging (mid Shift-drag) · device-face-turning (a shell's change mid-turn: knobs on their way, a wave mid-morph, the filter's peak growing — R-UI-12) · effect-window (an effect keeps its list) · tuner-window (a tuner while a synth holds A4: the note, the needle in tune and green, 440.0 Hz · 0 ct — R-FX-17) · wavetable-window (the wavetable's face from the registry: two table oscillators, their table switches — R-INST-7) · fm-window (the FM synth's face: four operators, each with its envelope — R-INST-8) · piano-window (the piano's face from the registry: Hammer, Strings, Body, Pedals, Output — R-INST-5) · ruler-scrubbing (the playhead held by the pointer, R-TIME-6) · ruler-cue (playing: the cue's outlined mark where Stop takes the time bar back, R-TIME-7) · midi-list (the Song tab: New MIDI, patterns with length, notes and players, then samples — R-BROWSE-4) · clip-end-dragging (a MIDI clip's end held, its pattern repeating, seams marked — R-CLIP-7) · loop-brace-moving (the brace taken by its body) · mixer-sidechain (a key in amber, a limiter on the master) · mixer-frozen (the Bass frozen: its rack receded under FROZEN — R-PERF-3) · mixer-frozen-mid (the same, mid-ease) · mixer-add-line (the "+ Line" menu) · mixer-vca (a VCA "Synths" at −6 dB moving two synth lines: its card names them, no meter; theirs say "VCA Synths -6.0" — R-MIX-19) · relink-window (File › Relink… on a song whose three samples moved: name, old folder, what uses it — a version's clip as vr_1:… — R-COLLECT-1) · relink-found-mid (one found: its row fading out, the rest sliding up, the card shrinking) · session-view (View › Session: Verse playing, its loops' progress; the bass's Drop slot queued, pulsing; ■ under each column — R-LAUNCH-1) · session-view-mid (the lanes giving way to the grid) · launch-recorded (what a recorded launch leaves: the bass cut, its tail starting into its pattern, the riff between) · video-window (View › Show Video: the song's picture at the time bar, letterboxed — the rig's dusk sky, its amber band's edge the frame's time; the foot: the file, its size and rate, the picture's time — R-VIDEO-1) · video-window-mid (a jump mid cross-dissolve: both frames' bands) · video-lane (the picture's row between the ruler and the lanes: its tiles, each band's edge its own time, its name pinned left) · video-lane-dragging (its span held two beats on, ringed) · conflicts-sheet (another window changed the song's file — the bass and the tempo asked, each side told apart; the merge's toast — R-COLLAB-1) · conflicts-answered-mid (the tempo answered: its row fading, the bass's sliding up) · ui-scale-150 (the window drawn at 150 % — R-ACCESS-1) · ui-scale-mid (mid-zoom, laid out at the scale drawn) · high-contrast (black, white text, light quiet text, solid borders) · keyboard-focus (the keyboard's focus ring on the Bass fader — R-ACCESS-1) · keyboard-focus-mid (the ring travelling to its pan) · home-vi (Home in Vietnamese — R-ACCESS-2) · menus-vi (the File menu open, Tệp) · settings-vi (Settings › Interface, the Language list open) · home-pseudo (Home in en-XA: every word the table's, a third longer, bracketed) · settings-pseudo (Settings › Audio in en-XA — the longest words fitting) · language-dissolve-mid (English giving way to Vietnamese, the whole window, a third of the way) · lanes-vi (the song in Vietnamese: the browser on Nhạc cụ, the lanes, the ruler) · lanes-pseudo (the same in en-XA) · dock-vi (the mixer in Vietnamese, a drum machine's face and the roll over it) · dock-pseudo (the roll and the loudness window in en-XA) · song-versions ("Radio edit" open on a frozen Instrumental: the chip ringed, its menu the tree and what a version can do — R-VER) · song-versions-mid (main opened from a shell: the label cross-fading, the ring going out, the chip narrowing) · mixer-tools (the synth's phase inverted and 150 % wide — "Ø 150%" on its card; the Verb bus solo-safe, its S ringed; the master's Mono on — R-MIX-20) · clip-play-through (a clip's "Play through ▸") · mixer-bound (faders, a pan, a send and the master driven by formulas: their tags, readouts in the accent, values where the transport is) · show-ids (every id: lanes, clips, an automation, the dock, a device window's addresses) · show-ids-mid (the ids mid-fade) · copied-toast (a copy said, in the accent) · dock-folded · meters-playing (the RMS core under the peak, held lines, a hot strip — R-METER-2; the song bar's CPU readout — R-PERF-1) · loudness-window (played and stopped: the readings, the needle, the 61 bands — R-METER-1) · export-sheet (File › Export…: MP3 chosen, its quality up, the bit depth greyed, a loudness target — R-EXPORT-1) · export-with-video (With the video on: MKV, the bit depth, quality and stems greyed — R-VIDEO-1) · recording (the Bass armed, its ring red; Record lit, playing — R-MIDI-2) · confirm-unsaved.
Each at 1440×900 and 1024×640. **Look at them** after a change.

## Borrowed, not copied

`interstellar/app/widgets/TextFit.h`, `EasedScroll.h`, `Glyphs.h`, `AnimatedRows.h`, `FadePage.h` are included in place (namespace
`interstellar_v1`): they draw with the cosmo palette, so they draw teal here. Their right home is
Artboard — a task in `docs/PROGRESS.md`. Cosmo's `ConfirmDialog`, `ContextMenu`, `SliderRow` (with
its opt-in `formatValue`), `Icons`, `Theme`, `EmbeddedFonts` are compiled from `apps/cosmo`.

## Gotchas found building U1
- **Words (R-ACCESS-2).** Draw a word with `tr("…")` at paint. A word a widget STORES (a menu label, a dialog line, a
  toast, a combo's choice, a note that cross-fades) is `say("…", data…)` / `sayn(n, "…", "…", …)` and shown with `shown()` —
  never stored translated, or a language switch cannot dissolve it. Unmarked text is data and is never translated. An
  English literal in an array is `N_("…")`. A new word fails `test_the_table_has_every_word_the_window_says` until
  `lang/vi.strings` has its line; a word drawn raw fails the en-XA test.

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

## Gotchas found building N4 (the meters)

- **The window cannot measure.** It draws at 60 Hz and sees one block's peak in twenty; RMS, a held peak and a clip
  latch computed from that would miss what played between frames. The player measures them per block and the model
  carries them (`strips[].rms/.hold/.clip`) — the widget only eases them.
- **A clear is a message, not a flag.** `meters clear` right after `set <ch>.gain=0` must not be undone by a block
  rendered at the old gain: the clear rides the same queue as the edit (`Live::ClearMeters`), and the service reads no
  holds or latches while it is still on its way (`Player::clearing`).
- **The rig plays at a quarter speed** (`SilentOut`): a UI test that needs a reading sleeps for real time — 1.6 s of
  wall clock is 0.4 s of audio, one integrated-loudness gating block.
- **A sample's strip is not `strips.back()`** in the rig's songs (a later mixer's bus comes after it in processing
  order): take it from the clip (`clips.front().track`).

## Gotchas found building N6 (delivery formats)

- **A default render is dithered now** (24 bits drop bits from float). A test that compares a render to an
  analytic signal, or to the live stream, asks for the mix as mixed: `--bits 32f`.
- **The rig's host has no encoders** (`writeAudio` unset): an Export to MP3 in a UI test is refused, and the
  refusal is the toast — which is the path worth testing there; the service tests' fake records the format.
- **Rendering at another rate re-tunes the library** (`AudioConfig` is one per process): `render` puts the rate back
  after, and refuses `--rate` while playing.

**A multi-sample's instrument (R-INST-9).** The device panel's header asks for "an .sfz" (`takesDefinition`); give it
with `set <dv>.sample=<file.sfz>` — a browser drop of an .sfz onto the device is not yet wired.
