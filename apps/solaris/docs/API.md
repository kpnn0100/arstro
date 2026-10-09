# Solaris — API

> **Generated** by `solaris-cc api --md` from the tables the code runs on — the grammar table, the event table, the model's field table and the DSP library's device registry. Do not edit: a test regenerates this file and fails on any difference (R-API-1).

A line is `<verb…> <positional…> [--flag value]…`; chain lines with ` : ` on the command line, or one per line with `--script`. An unknown verb, flag, address or parameter is refused, naming the nearest candidates.

## Commands

| usage | what | asked by |
|---|---|---|
| `project new <path.slp> [--bpm <n>] [--sig <n/d>] [--rate <hz>] [--name <text>]` | Create a song and open it: Mixer 1 "Sources", Mixer 2 "Buses" with the bus "Main" → master, and the output port "Main" (R-MIX-3). | R-HOME-1 |
| `project open <path.slp>` | Open a song. | R-HOME-1 |
| `project save [path.slp]` | Write the .slp (a path saves it there). | R-FMT-2 |
| `project close` | Close the song and return Home. | R-UI-1 |
| `set <address>=<value> …` | Write addresses: project.bpm, <strip>.gain, <clip>.at, <device>.<param> (any DSP registry parameter, in its unit), … — `api` lists every one. An unknown address or parameter is refused. A value starting with `=` is a FORMULA that drives a number (R-AUTO); a plain number clears it. | R-SVC-3 |
| `get <address>` | Print an address's stored value. | R-SVC-1 |
| `undo` | Undo the last edit (prints what). Consecutive `set`s of the same addresses — a fader dragged — are ONE step. A machine setting, the transport and a save are not edits; a new or opened song starts a new history. | R-EDM-1 |
| `redo` | Redo what `undo` took back; a new edit clears it. | R-EDM-1 |
| `mixer add [name]` | Add a mixer page after the last one. | R-MIX-3 |
| `mixer delete <mx>` | Delete an empty mixer. Refused while strips live on it. | R-MIX-3 |
| `mixer move <mx> [--to <index>]` | Move a mixer to position <index> (0 = first). Refused if it would make any route point backward. | R-MIX-4 |
| `strip add [--kind <audio\|instrument\|bus>] [--name <text>] [--mixer <mx>] [--instrument <synth\|drums>] [--out <target>]` | Add a strip. Default mixer: the first for audio/instrument, the second for a bus; default output: the first bus on a later mixer ("Main"), else master. An instrument strip gets its instrument (default synth) as its first device. | R-MIX-1 |
| `strip delete <ch> [--with-clips]` | Delete a strip and its rack. Refused while clips play through it or strips route to it, unless --with-clips (its clips go too). | R-MIX-1 |
| `strip move <ch> [--mixer <mx>] [--order <n>]` | Move a strip to another mixer and/or position. Refused if a route would point backward. | R-MIX-3 |
| `route <ch> [--to <ch\|master\|port>]` | Set a strip's main output: a strip on a LATER mixer, master, or an output port. | R-MIX-4 |
| `send add <ch> [--to <ch\|master\|port>] [--gain <dB>] [--pre]` | Add a send (post-fader unless --pre). Same forward-only rule as `route`. | R-MIX-5 |
| `send delete <sd>` | Remove a send. | R-MIX-5 |
| `device add <ch\|master> [--type <registry type>] [--at <index>]` | Insert a DSP registry device into a rack (default: at the end). An instrument goes only first on an instrument strip. | R-FX-5 |
| `device remove <dv>` | Remove a device from its rack. An instrument strip keeps its instrument. | R-FX-5 |
| `device move <dv> [--to <index>]` | Move a device within its rack. | R-FX-5 |
| `lane add [name]` | Add a timeline lane at the bottom. | R-LANE-1 |
| `lane delete <ln> [--with-clips]` | Delete a lane. Refused while clips are drawn on it, unless --with-clips. | R-LANE-1 |
| `clip add [--src <file>] [--strip <ch>] [--instrument <type>] [--pattern <pt>] [--lane <ln>] [--at <beats>] [--length <beats>] [--in <s>] [--out <s>]` | Place a clip. With --src: an audio clip; a file the song has not used gets its own new strip on the first mixer (R-MIX-2), a used one reuses its strip, and with no --lane it gets a new lane. With --strip <instrument>: a note clip of --pattern (a new empty pattern if none). With --instrument <type>: the same on a NEW instrument strip of that type — what dropping an instrument does (R-BROWSE-3). | R-MIX-2 |
| `clip move <ac> [--at <beats>] [--lane <ln>] [--strip <ch>]` | Move a clip in time, to another lane (where it is drawn), and/or another strip (what it sounds through). | R-LANE-2 |
| `clip duplicate <ac> [--at <beats>]` | A copy right after it (or at --at). A note clip's copy plays the SAME pattern — linked. | R-CLIP-3 |
| `clip unique <ac>` | Give a note clip its own copy of its pattern. | R-CLIP-3 |
| `clip delete <ac>` | Remove a clip. | R-LANE-1 |
| `pattern new [--name <text>] [--length <beats>]` | Create an empty pattern. | R-CLIP-2 |
| `note add <pt> [--pitch <0-127>] [--at <beats>] [--length <beats>] [--vel <1-127>]` | Add a note to a pattern — every clip of it changes. | R-CLIP-2 |
| `note delete <pt> [--pitch <0-127>] [--at <beats>]` | Remove the note at that pitch and time. | R-CLIP-2 |
| `note move <pt> [--pitch <0-127>] [--at <beats>] [--to-pitch <0-127>] [--to-at <beats>] [--length <beats>] [--vel <1-127>]` | Change the note at --pitch / --at: move it (--to-pitch, --to-at), resize it (--length), set its velocity (--vel) — one gesture, one line. Refused onto another note. | R-ROLL-2 |
| `pattern quantize <pt> [--grid <beats>] [--swing <0-0.75>]` | Move every note's start onto the grid (default a sixteenth, 0.25), delaying every second grid step by --swing of a step. Notes that land on one another merge (the louder stays). | R-ROLL-4 |
| `auto create <address>` | Automate a number: a new automation named "<owner> · <parameter>", ranged as it, holding its value from beat 0 to the song's end, shown on the timeline — and the address bound to `=au_n`. Prints the id. | R-AUTO-5 |
| `auto add [--name <text>] [--min <v>] [--max <v>] [--unit <text>]` | An automation from nothing (default range 0…1). It moves nothing until a formula reads it. Prints the id. | R-AUTO-4 |
| `auto delete <au> [--unbind]` | Delete an automation. Refused while a formula reads it, unless --unbind (those bindings are cleared). | R-AUTO-4 |
| `auto point add <au> [--at <beats>] [--value <v>] [--shape <linear\|hold\|smooth>]` | Add a point (one at the same beat is replaced). Its shape governs the segment after it. | R-AUTO-6 |
| `auto point move <au> [--at <beats>] [--to <beats>] [--value <v>]` | Move the point at --at to another beat and/or value. | R-AUTO-6 |
| `auto point delete <au> [--at <beats>]` | Remove the point at --at. | R-AUTO-6 |
| `auto point shape <au> [--at <beats>] [--shape <linear\|hold\|smooth>]` | Set how the curve leaves the point at --at. | R-AUTO-6 |
| `bind clear <address>` | Clear an address's formula: it plays its own stored value again. (`set <address>=<number>` clears and sets.) | R-AUTO-1 |
| `eval <address> [--at <beats>] [--explain]` | The value an address plays at a beat (default 0); --explain shows its formula and every name it reads. | R-AUTO-8 |
| `render [--out <file.wav>] [--from <beats>] [--to <beats>] [--stems <ch,…>] [--ports] [--bits <24\|32f>]` | Render offline: the master to --out; with --stems, each named strip's post-fader output to <out>.<ch>.wav; with --ports, each output port to <out>.<port>.wav. The tail runs until −90 dBFS or 10 s. | R-RENDER-2 |
| `matrix print [--json]` | Every route and send at once: rows = strips, columns = destinations. | R-MIX-9 |
| `audit` | The mix report: unused strips, clips on muted strips, unreachable strips, single-input buses, offline media, unknown devices, clipping. | R-MIX-10 |
| `state print [--json] [--stable]` | The whole AppModel; --stable omits what changes with time. | R-SVC-2 |
| `api [--json] [--md]` | This document: every command, event, model field and device parameter. | R-API-1 |
| `settings set <key>=<value> …` | The MACHINE's settings (never a song's): sampleRate (new songs, and the clock device's rate), bufferSize (frames), output / input (device ids from `devices list`; empty = the system default), port.<name>=<device>:<channel> (where a song's port plays on this machine), metronome (on\|off), metronomeLevel (dB), newBpm / newSig (what a new song starts at), reducedMotion (on\|off). Saved at once. | R-SET-1 |
| `settings print [--json]` | The machine's settings. | R-SET-1 |
| `folder add <path>` | Add a sample folder to the browser's quick-access list. | R-SET-1 |
| `folder remove <path>` | Remove a sample folder from the list (the folder itself is untouched). | R-SET-1 |
| `folder move <path> [--to <index>]` | Reorder the sample folders. | R-SET-1 |
| `devices list` | List this machine's audio devices (ids for `settings set output=…`). | R-DEV-1 |
| `browse <folder>` | List a folder for the browser: sub-folders, audio files, songs. | R-BROWSE-1 |
| `recents remove <path>` | Take a song off Home's recent list (the file is untouched). | R-HOME-1 |
| `transport play [--from <beats>]` | Play on the clock device (settings output), from --from or where the transport stands. Edits while playing are heard: a gain, a pan, a mute, a solo, a device parameter at once; anything structural by a new engine swapped in at the same position. | R-PLAY-1 |
| `transport stop` | Stop; the transport stays where it was heard. | R-TIME-4 |
| `transport seek <beats>` | Move the transport (playing or not). | R-TIME-4 |
| `transport loop <from\|off> [to]` | Loop between two beats while playing; `off` ends it. | R-TIME-4 |
| `wait <seconds>` | Let time pass (playback goes on, the model's transport and meters update) — for scripts that listen. | R-PLAY-3 |

## Addresses (`set` / `get`)

| address | value |
|---|---|
| `project.name` · `project.bpm` · `project.sig` · `project.masterGain` · `project.sampleRate` | text · 20…999 · n/d · dB · Hz |
| `<strip>.name` · `.gain` · `.pan` · `.mute` · `.solo` · `.colour` | text · dB (−120…12) · −1…1 · bool · bool · −1…15 |
| `<clip>.name` · `.at` · `.length` · `.fadeIn` · `.fadeOut` · `.gain` · `.loop` · `.in` · `.out` | text · beats · beats · beats · beats · dB · bool · s · s |
| `<lane>.name` · `.colour` · `<mixer>.name` · `<send>.gain` · `.pre` · `<pattern>.name` · `.length` · `<port>.name` · `.channels` | |
| `<device>.bypass` · `<device>.<param>` | bool · any parameter of its type below, in its unit; a choice by name |
| `<automation>.name` · `.unit` · `.min` · `.max` (read: also `.from` · `.points`) | text · text · number · number |
| any NUMBER above (a strip's gain/pan, a send's gain, `project.masterGain`, a numeric device parameter) `=<formula>` | binds it (R-AUTO-1): numbers, `+ - * / ^ ( )`, `sin cos tan abs sign min max clamp lerp pow exp log sqrt floor ceil round frac`, `pi`, `beat bar bpm t`, an automation id (`au_1`), another numeric address (a link). `get` prints the formula; a plain number clears it |

## Events

Each is one line: `[evt] <name> key=value …` — the log line, the `--watch` stream.

| event | fields | when |
|---|---|---|
| `info` | text | A note worth logging; carries no state. |
| `error` | why | Something failed. Also set as AppModel.lastError. |
| `command.rejected` | line, why | A line was refused (R-SVC-3): unknown verb, flag, address or parameter; a refused edit. |
| `screen.changed` | screen | home \| project. |
| `project.opened` | path, strips, clips | A song is open. |
| `project.saved` | path | The .slp was written. |
| `project.closed` |  | Back to Home. |
| `project.changed` | what, node | A node was added, removed, moved or re-routed. what = e.g. strip.added, clip.moved, route.set. |
| `params.changed` | address, value | An address was written; value is what was STORED (clamped). |
| `render.finished` | out, frames, peak | A render was written; peak is the master's, dBFS. |
| `render.failed` | why | A render stopped with an error. |
| `audit.report` | findings | How many findings `audit` printed. |
| `settings.changed` | what | A machine setting changed and was saved: sampleRate, bufferSize, output, input, port.<name>, folders. |
| `devices.changed` | count | `devices list` found this many devices. |
| `browse.changed` | path, entries | The browser now lists this folder. |
| `recents.changed` | count | Home's recent songs changed. |
| `transport.changed` | playing, position, loop | Play, stop, seek or loop: playing 1/0, position in beats, loop `from-to` or `off`. |

## Model (`state print --json`)

| field | type | meaning |
|---|---|---|
| `screen` | string | home \| project |
| `projectPath` | string | the open .slp |
| `projectName` | string | the song's name |
| `dirty` | bool | unsaved changes |
| `bpm` | number | tempo |
| `sig` | string | meter, n/d |
| `sampleRate` | int | the project's rate |
| `masterGain` | number | dB |
| `masterGainFormula` | string | the formula driving it, "" = none (R-AUTO-1) |
| `undoLabel` | string | what `undo` would take back, "" = nothing (R-EDM-1) |
| `redoLabel` | string | what `redo` would put back |
| `undoDepth` | int | edits `undo` can take back |
| `redoDepth` | int | steps `redo` can put back |
| `masterOut` | string[] | the port ids the master feeds |
| `mixers` | object[] | mixer pages in order |
| `mixers[].id` | string |  |
| `mixers[].name` | string |  |
| `mixers[].order` | int | processing and tab order |
| `mixers[].strips` | string[] | its strips in processing order |
| `strips` | object[] | every strip, in processing order |
| `strips[].id` | string |  |
| `strips[].name` | string |  |
| `strips[].kind` | string | audio \| instrument \| bus |
| `strips[].mixer` | string | the mixer it lives on (resolved) |
| `strips[].order` | int | position on its mixer |
| `strips[].out` | string | master, a strip id, or a port id (resolved) |
| `strips[].gain` | number | dB |
| `strips[].pan` | number | −1 … +1 |
| `strips[].gainFormula` | string | the formula driving the gain, "" = none (R-AUTO-1) |
| `strips[].panFormula` | string | the formula driving the pan, "" = none |
| `strips[].mute` | bool |  |
| `strips[].solo` | bool |  |
| `strips[].audible` | bool | false when muted or silenced by another strip's solo (R-MIX-7) |
| `strips[].colour` | int | an index into the track colours: its own, else from its id — never −1 |
| `strips[].sends` | object[] | its sends |
| `strips[].sends[].id` | string |  |
| `strips[].sends[].to` | string | a strip on a later mixer, master, or a port |
| `strips[].sends[].gain` | number | dB |
| `strips[].sends[].pre` | bool | pre-fader |
| `strips[].sends[].gainFormula` | string | the formula driving the send's gain, "" = none |
| `strips[].devices` | object[] | the rack, in order (an instrument strip's first is its instrument) |
| `strips[].devices[].id` | string |  |
| `strips[].devices[].type` | string | the DSP registry type |
| `strips[].devices[].label` | string | the registry's label |
| `strips[].devices[].instrument` | bool |  |
| `strips[].devices[].bypass` | bool |  |
| `strips[].devices[].known` | bool | false when this build's registry lacks the type (kept, not played) |
| `strips[].devices[].lastChanged` | string | the parameter last written, by anyone (R-WIN-2) |
| `strips[].devices[].params` | object[] | every registry parameter, stored or default (R-UI-5) |
| `strips[].devices[].params[].name` | string | the registry name, e.g. filter.cutoff |
| `strips[].devices[].params[].label` | string |  |
| `strips[].devices[].params[].unit` | string | Hz, dB, ms, st, ct, oct, or empty |
| `strips[].devices[].params[].text` | string | as stored: a number, or a choice's name |
| `strips[].devices[].params[].value` | number | engineering units; a choice = its index |
| `strips[].devices[].params[].min` | number |  |
| `strips[].devices[].params[].max` | number |  |
| `strips[].devices[].params[].def` | number |  |
| `strips[].devices[].params[].choices` | string[] | empty unless a choice |
| `strips[].devices[].params[].logScale` | bool | a control's taper: moves in ratios (frequencies, times) |
| `strips[].devices[].params[].integer` | bool | whole steps only |
| `strips[].devices[].params[].formula` | string | the formula driving it, "" = its own value plays (R-AUTO-1) |
| `strips[].peak` | number[] | the last played block's peaks, L/R (R-PLAY-3) *(not in `--stable`)* |
| `strips[].clipCount` | int | fed by: clips playing through it (R-MIX-8) |
| `strips[].fromLanes` | string[] | fed by: the lanes those clips are drawn on |
| `strips[].fromStrips` | string[] | fed by: strips whose output or a send lands here |
| `strips[].targets` | string[] | where its output or a send may go (R-MIX-4): strips on later mixers, master, output ports |
| `masterDevices` | object[] | the master's rack, shaped like strips[].devices |
| `lanes` | object[] | timeline rows, in order |
| `lanes[].id` | string |  |
| `lanes[].name` | string |  |
| `lanes[].order` | int |  |
| `lanes[].colour` | int | −1 = none |
| `clips` | object[] | every clip |
| `clips[].id` | string |  |
| `clips[].name` | string |  |
| `clips[].track` | string | the strip it sounds through |
| `clips[].lane` | string | the lane it is drawn on ("" = its strip's own row) |
| `clips[].kind` | string | audio \| note |
| `clips[].src` | string | audio: the file |
| `clips[].pattern` | string | note: the pattern it plays |
| `clips[].at` | number | beats |
| `clips[].length` | number | beats, resolved |
| `clips[].in` | number | seconds into the file |
| `clips[].out` | number | seconds into the file |
| `clips[].gain` | number | dB |
| `clips[].fadeIn` | number | beats |
| `clips[].fadeOut` | number | beats |
| `clips[].loop` | bool |  |
| `clips[].offline` | bool | the file could not be read |
| `clips[].linked` | int | clips playing the same pattern, this one included (R-CLIP-3) |
| `patterns` | object[] | every pattern |
| `patterns[].id` | string |  |
| `patterns[].name` | string |  |
| `patterns[].length` | number | beats |
| `patterns[].clips` | int | clips playing it |
| `patterns[].strip` | string | the strip its first clip plays through |
| `patterns[].instrument` | string | that strip's instrument type (what names its keys) |
| `patterns[].notes` | object[] | sorted by (at, pitch) |
| `patterns[].notes[].pitch` | int | 0…127 |
| `patterns[].notes[].at` | number | beats from the pattern's start |
| `patterns[].notes[].length` | number | beats |
| `patterns[].notes[].vel` | int | 1…127 |
| `automations` | object[] | every automation (R-AUTO-4) |
| `automations[].id` | string | au_n — what a formula names |
| `automations[].name` | string | "<owner> · <parameter>" when made from one |
| `automations[].unit` | string |  |
| `automations[].from` | string | the address it was made from (a hint, not a link) |
| `automations[].min` | number |  |
| `automations[].max` | number |  |
| `automations[].points` | object[] | sorted by at |
| `automations[].points[].at` | number | beats |
| `automations[].points[].value` | number | in the automation's unit |
| `automations[].points[].shape` | string | linear \| hold \| smooth — the segment after it |
| `automations[].usedBy` | string[] | addresses whose formula reads it |
| `bindings` | object[] | every formula (R-AUTO-1/9) |
| `bindings[].address` | string | what it drives |
| `bindings[].formula` | string | as typed, with its leading = |
| `bindings[].reads` | string[] | the automations and addresses it reads |
| `bindings[].ok` | bool | false = INERT: the address's own value plays |
| `bindings[].problem` | string | why it is inert |
| `ports` | object[] | logical ports (R-DEV-3) |
| `ports[].id` | string |  |
| `ports[].name` | string |  |
| `ports[].dir` | string | in \| out |
| `ports[].channels` | int |  |
| `lengthBeats` | number | where the last clip ends |
| `audit` | string[] | the last `audit`'s findings |
| `lastError` | string | the last refusal or failure |
| `recents` | object[] | Home's cards, newest first (R-HOME-1) |
| `recents[].path` | string |  |
| `recents[].name` | string |  |
| `recents[].bpm` | number |  |
| `recents[].lengthBeats` | number |  |
| `recents[].strips` | int |  |
| `recents[].missing` | bool | the file is gone or no longer a song |
| `settings` | object | the MACHINE's settings (R-SET-2) |
| `settings.sampleRate` | int | new songs, and the clock device's rate |
| `settings.bufferSize` | int | frames per device write |
| `settings.latencyMs` | number | what that buffer costs at that rate |
| `settings.output` | string | the clock device ('' = the system default) |
| `settings.input` | string |  |
| `settings.folders` | string[] | the browser's quick-access folders, in order |
| `settings.ports` | string[] | <port>=<device>:<channel> on this machine |
| `settings.metronome` | bool | clicks while playing (R-TIME-4), never in a render |
| `settings.metronomeLevel` | number | dB |
| `settings.newBpm` | number | a new song's tempo without --bpm |
| `settings.newSig` | string | a new song's meter without --sig |
| `settings.reducedMotion` | bool | the UI's tweens collapse (with the OS's own setting) |
| `devices` | object[] | from the last `devices list` |
| `devices[].id` | string | what `settings set output=` takes |
| `devices[].name` | string |  |
| `devices[].dir` | string | out \| in |
| `devices[].channels` | int |  |
| `devices[].rate` | int |  |
| `transport` | object | play, position, loop (R-TIME-4) |
| `transport.playing` | bool |  |
| `transport.position` | number | beats — what is heard *(not in `--stable`)* |
| `transport.loopFrom` | number | beats |
| `transport.loopTo` | number | beats; equal to loopFrom = no loop |
| `transport.latencyMs` | number | the clock device's *(not in `--stable`)* |
| `transport.device` | string | the clock device ('' = the system default) |
| `transport.masterPeak` | number[] | L/R peaks of the last played block *(not in `--stable`)* |
| `deviceTypes` | object[] | the DSP registry: every instrument and effect a strip can host (R-BROWSE-1) |
| `deviceTypes[].name` | string | the registry type, what `device add --type` and `--instrument` take |
| `deviceTypes[].label` | string |  |
| `deviceTypes[].kind` | string | instrument \| effect |
| `deviceTypes[].noteNames` | object[] | a kit's keys, named (REQ-device-6) |
| `deviceTypes[].noteNames[].note` | int |  |
| `deviceTypes[].noteNames[].name` | string |  |
| `browser` | object | the folder last browsed |
| `browser.path` | string |  |
| `browser.entries` | object[] | folders first, then by name |
| `browser.entries[].name` | string |  |
| `browser.entries[].path` | string |  |
| `browser.entries[].kind` | string | dir \| audio \| song \| other |
| `revision` | int | bumps on every change *(not in `--stable`)* |

## Devices — the DSP library's registry

Every instrument and effect is a module of `core/DigitalSignalProcessing`; this section is read from its `DeviceRegistry` (R-DSP-2).

### `synth` — Basic Synth (instrument)

Two oscillators and noise through a resonant filter and an amplitude envelope; 16 voices.

| parameter | range | default |
|---|---|---|
| `osc1.wave` | sine · saw · square · triangle | saw |
| `osc1.octave` | -3.0 … 3.0 oct | 0.0 oct |
| `osc1.semi` | -12.0 … 12.0 st | 0.0 st |
| `osc1.fine` | -100.0 … 100.0 ct | 0.0 ct |
| `osc1.level` | 0.0 … 1.0 | 0.8 |
| `osc1.voices` | 1.0 … 5.0 | 1.0 |
| `osc1.detune` | 0.0 … 100.0 ct | 15.0 ct |
| `osc2.wave` | sine · saw · square · triangle | square |
| `osc2.octave` | -3.0 … 3.0 oct | -1.0 oct |
| `osc2.semi` | -12.0 … 12.0 st | 0.0 st |
| `osc2.fine` | -100.0 … 100.0 ct | 7.0 ct |
| `osc2.level` | 0.0 … 1.0 | 0.5 |
| `osc2.voices` | 1.0 … 5.0 | 1.0 |
| `osc2.detune` | 0.0 … 100.0 ct | 15.0 ct |
| `noise` | 0.0 … 1.0 | 0.0 |
| `filter.mode` | lowpass · bandpass · highpass · notch | lowpass |
| `filter.cutoff` | 20.0 … 20000.0 Hz | 2400.0 Hz |
| `filter.res` | 0.0 … 1.0 | 0.25 |
| `filter.env` | -8.0 … 8.0 oct | 2.0 oct |
| `filter.keytrack` | 0.0 … 1.0 | 0.5 |
| `fenv.attack` | 0.0 … 10000.0 ms | 3.0 ms |
| `fenv.decay` | 0.0 … 10000.0 ms | 350.0 ms |
| `fenv.sustain` | 0.0 … 1.0 | 0.25 |
| `fenv.release` | 0.0 … 10000.0 ms | 300.0 ms |
| `amp.attack` | 0.0 … 10000.0 ms | 3.0 ms |
| `amp.decay` | 0.0 … 10000.0 ms | 120.0 ms |
| `amp.sustain` | 0.0 … 1.0 | 0.8 |
| `amp.release` | 0.0 … 10000.0 ms | 250.0 ms |
| `volume` | -60.0 … 6.0 dB | -12.0 dB |
| `velocity` | 0.0 … 1.0 | 0.8 |

### `drums` — Drum Machine (instrument)

Ten synthesized pads on the GM drum notes (36 kick … 56 cowbell); the closed hat chokes the open hat.

| parameter | range | default |
|---|---|---|
| `kick.tune` | -12.0 … 12.0 st | 0.0 st |
| `kick.decay` | 10.0 … 4000.0 ms | 450.0 ms |
| `kick.tone` | 0.0 … 1.0 | 0.5 |
| `kick.level` | -60.0 … 6.0 dB | 0.0 dB |
| `kick.pan` | -1.0 … 1.0 | 0.0 |
| `rim.tune` | -12.0 … 12.0 st | 0.0 st |
| `rim.decay` | 10.0 … 4000.0 ms | 45.0 ms |
| `rim.tone` | 0.0 … 1.0 | 0.5 |
| `rim.level` | -60.0 … 6.0 dB | 0.0 dB |
| `rim.pan` | -1.0 … 1.0 | 0.0 |
| `snare.tune` | -12.0 … 12.0 st | 0.0 st |
| `snare.decay` | 10.0 … 4000.0 ms | 220.0 ms |
| `snare.tone` | 0.0 … 1.0 | 0.5 |
| `snare.level` | -60.0 … 6.0 dB | 0.0 dB |
| `snare.pan` | -1.0 … 1.0 | 0.0 |
| `clap.tune` | -12.0 … 12.0 st | 0.0 st |
| `clap.decay` | 10.0 … 4000.0 ms | 300.0 ms |
| `clap.tone` | 0.0 … 1.0 | 0.5 |
| `clap.level` | -60.0 … 6.0 dB | 0.0 dB |
| `clap.pan` | -1.0 … 1.0 | 0.0 |
| `ltom.tune` | -12.0 … 12.0 st | 0.0 st |
| `ltom.decay` | 10.0 … 4000.0 ms | 500.0 ms |
| `ltom.tone` | 0.0 … 1.0 | 0.5 |
| `ltom.level` | -60.0 … 6.0 dB | 0.0 dB |
| `ltom.pan` | -1.0 … 1.0 | 0.0 |
| `chat.tune` | -12.0 … 12.0 st | 0.0 st |
| `chat.decay` | 10.0 … 4000.0 ms | 60.0 ms |
| `chat.tone` | 0.0 … 1.0 | 0.5 |
| `chat.level` | -60.0 … 6.0 dB | 0.0 dB |
| `chat.pan` | -1.0 … 1.0 | 0.0 |
| `mtom.tune` | -12.0 … 12.0 st | 0.0 st |
| `mtom.decay` | 10.0 … 4000.0 ms | 420.0 ms |
| `mtom.tone` | 0.0 … 1.0 | 0.5 |
| `mtom.level` | -60.0 … 6.0 dB | 0.0 dB |
| `mtom.pan` | -1.0 … 1.0 | 0.0 |
| `ohat.tune` | -12.0 … 12.0 st | 0.0 st |
| `ohat.decay` | 10.0 … 4000.0 ms | 450.0 ms |
| `ohat.tone` | 0.0 … 1.0 | 0.5 |
| `ohat.level` | -60.0 … 6.0 dB | 0.0 dB |
| `ohat.pan` | -1.0 … 1.0 | 0.0 |
| `htom.tune` | -12.0 … 12.0 st | 0.0 st |
| `htom.decay` | 10.0 … 4000.0 ms | 360.0 ms |
| `htom.tone` | 0.0 … 1.0 | 0.5 |
| `htom.level` | -60.0 … 6.0 dB | 0.0 dB |
| `htom.pan` | -1.0 … 1.0 | 0.0 |
| `cowbell.tune` | -12.0 … 12.0 st | 0.0 st |
| `cowbell.decay` | 10.0 … 4000.0 ms | 350.0 ms |
| `cowbell.tone` | 0.0 … 1.0 | 0.5 |
| `cowbell.level` | -60.0 … 6.0 dB | 0.0 dB |
| `cowbell.pan` | -1.0 … 1.0 | 0.0 |
| `volume` | -60.0 … 6.0 dB | -6.0 dB |

### `compressor` — Compressor (effect)

Feed-forward peak compressor.

| parameter | range | default |
|---|---|---|
| `threshold` | -60.0 … 0.0 dB | -18.0 dB |
| `ratio` | 1.0 … 20.0 :1 | 4.0 :1 |
| `attack` | 0.1 … 200.0 ms | 5.0 ms |
| `release` | 5.0 … 2000.0 ms | 100.0 ms |
| `makeup` | 0.0 … 24.0 dB | 0.0 dB |

### `eq` — EQ (effect)

Low cut, low shelf, three peaks, high shelf, high cut.

| parameter | range | default |
|---|---|---|
| `lowcut.on` | off · on | off |
| `lowcut.freq` | 20.0 … 1000.0 Hz | 30.0 Hz |
| `lowshelf.freq` | 20.0 … 1000.0 Hz | 100.0 Hz |
| `lowshelf.gain` | -18.0 … 18.0 dB | 0.0 dB |
| `peak1.freq` | 20.0 … 20000.0 Hz | 250.0 Hz |
| `peak1.gain` | -18.0 … 18.0 dB | 0.0 dB |
| `peak1.q` | 0.1 … 10.0 | 1.0 |
| `peak2.freq` | 20.0 … 20000.0 Hz | 1000.0 Hz |
| `peak2.gain` | -18.0 … 18.0 dB | 0.0 dB |
| `peak2.q` | 0.1 … 10.0 | 1.0 |
| `peak3.freq` | 20.0 … 20000.0 Hz | 4000.0 Hz |
| `peak3.gain` | -18.0 … 18.0 dB | 0.0 dB |
| `peak3.q` | 0.1 … 10.0 | 1.0 |
| `highshelf.freq` | 1000.0 … 20000.0 Hz | 8000.0 Hz |
| `highshelf.gain` | -18.0 … 18.0 dB | 0.0 dB |
| `highcut.on` | off · on | off |
| `highcut.freq` | 1000.0 … 20000.0 Hz | 18000.0 Hz |

### `reverb` — Reverb (effect)

Feedback-delay-network reverb, decorrelated stereo.

| parameter | range | default |
|---|---|---|
| `size` | 10.0 … 300.0 ms | 100.0 ms |
| `decay` | 100.0 … 10000.0 ms | 1500.0 ms |
| `lowcut` | 20.0 … 2000.0 Hz | 100.0 Hz |
| `highcut` | 1000.0 … 20000.0 Hz | 8000.0 Hz |
| `width` | 0.0 … 1.0 | 0.6 |
| `mix` | 0.0 … 1.0 | 0.25 |

### `delay` — Delay (effect)

Feedback echo, each repeat darker.

| parameter | range | default |
|---|---|---|
| `time` | 1.0 … 2000.0 ms | 375.0 ms |
| `feedback` | 0.0 … 0.95 | 0.4 |
| `tone` | 200.0 … 20000.0 Hz | 4000.0 Hz |
| `mix` | 0.0 … 1.0 | 0.3 |

### `chorus` — Chorus (effect)

LFO-modulated short delay.

| parameter | range | default |
|---|---|---|
| `rate` | 0.05 … 10.0 Hz | 1.5 Hz |
| `depth` | 0.0 … 10.0 ms | 3.0 ms |
| `delay` | 1.0 … 40.0 ms | 12.0 ms |
| `mix` | 0.0 … 1.0 | 0.5 |

### `drive` — Drive (effect)

tanh waveshaper with a tone low-pass.

| parameter | range | default |
|---|---|---|
| `drive` | 1.0 … 50.0 x | 2.0 x |
| `tone` | 500.0 … 20000.0 Hz | 5000.0 Hz |
| `level` | -24.0 … 12.0 dB | 0.0 dB |

### `filter` — Filter (effect)

The synth's resonant state-variable filter, with a dry/wet mix.

| parameter | range | default |
|---|---|---|
| `mode` | lowpass · bandpass · highpass · notch | lowpass |
| `cutoff` | 20.0 … 20000.0 Hz | 1000.0 Hz |
| `res` | 0.0 … 1.0 | 0.3 |
| `mix` | 0.0 … 1.0 | 1.0 |
