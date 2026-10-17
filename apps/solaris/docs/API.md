# Solaris — API

> **Generated** by `solaris-cc api --md` from the tables the code runs on — the grammar table, the event table, the model's field table and the DSP library's device registry. Do not edit: a test regenerates this file and fails on any difference (R-API-1).

A line is `<verb…> <positional…> [--flag value]…`; chain lines with ` : ` on the command line, one per line with `--script`, or a session on stdin with `shell` (one song, undo kept). `#` starts a comment, also at the end of a line. An unknown verb, flag, address or parameter is refused, naming the nearest candidates. A command that makes nodes prints the id it made first, then `made: kind=id …` for anything else it made. How an agent writes a song: `docs/AGENTS.md`.

## Commands

| usage | what | asked by |
|---|---|---|
| `project new <path.slp> [--bpm <n>] [--sig <n/d>] [--rate <hz>] [--name <text>] [--template <name>]` | Create a song and open it: Mixer 1 "Sources", Mixer 2 "Buses" with the bus "Main" → master, and the output port "Main" (R-MIX-3). --template <name>: the template's song instead (R-PRESET-2) — its tempo, meter and rate unless --bpm, --sig, --rate say; yours of a name before the factory's. | R-HOME-1 |
| `project open <path.slp>` | Open a song. If it has an autosave newer than it (unsaved edits from a session that never saved), says `recovery: …` and the model's `recovery` names it (R-SAFE-2). | R-HOME-1 |
| `project save [path.slp]` | Write the .slp (a path saves it there). | R-FMT-2 |
| `project close` | Close the song and return Home (its autosave is dropped: closing is choosing). | R-UI-1 |
| `project recover <path.slp>` | Open the song from its autosave (`<path>.autosave`, written while it had unsaved edits — R-SAFE-1): the song as it was, unsaved, saved to <path> by `project save`. | R-SAFE-2 |
| `project discard-recovery <path.slp>` | Delete a song's autosave — the saved song is the one kept. | R-SAFE-2 |
| `set <address>=<value> …` | Write addresses: project.bpm, <strip>.gain, <clip>.at, <device>.<param> (any DSP registry parameter, in its unit), … — `api` lists every one. An unknown address or parameter is refused. A value starting with `=` is a FORMULA that drives a number (R-AUTO); a plain number clears it. | R-SVC-3 |
| `get <address>` | Print an address's stored value. | R-SVC-1 |
| `undo` | Undo the last edit (prints what): back to the history node's parent. Consecutive `set`s of the same addresses — a fader dragged — are ONE step. A machine setting, the transport and a save are not edits; a new or opened song starts a new history. | R-EDM-1 |
| `redo` | Redo: forward to the NEWEST branch below the current node (prints what). An edit after an undo no longer clears it — it starts another branch, and the old ones stay (R-HIST-1). | R-EDM-1 |
| `history [--json]` | The song's history as a tree: one node a line — `*` at the current one, its id, its parent's (`-` at the root), what made it. --json: the same as `history[]` in the model. | R-HIST-2 |
| `history jump <id>` | Make history node <id> the song (prints what made it) — as undo does, it moves through the tree and is no node itself; the song is unsaved and the player hears it. | R-HIST-2 |
| `mixer add [name]` | Add a mixer page after the last one. | R-MIX-3 |
| `mixer delete <mx>` | Delete an empty mixer. Refused while strips live on it. | R-MIX-3 |
| `mixer move <mx> [--to <index>]` | Move a mixer to position <index> (0 = first). Refused if it would make any route point backward. | R-MIX-4 |
| `strip add [--kind <audio\|instrument\|bus>] [--name <text>] [--mixer <mx>] [--instrument <type>] [--plugin <id\|name>] [--sample <file>] [--out <target>]` | Add a strip. Default mixer: the first for audio/instrument, the second for a bus; default output: "Main" — the bus that leaves the nearest later mixer with buses, else the one leaving its own mixer (the new strip is then placed BEFORE it, so the route goes forward), else master. An instrument strip gets its instrument (default synth) as its first device; --sample <file> gives a sampler its sound (R-EDM-8); --plugin <id\|name> makes it another maker's instrument plugin, from `plugins list` (R-VST-6). | R-MIX-1 |
| `strip delete <ch> [--with-clips]` | Delete a strip and its rack. Refused while clips play through it or strips route to it, unless --with-clips (its clips go too). | R-MIX-1 |
| `strip move <ch> [--mixer <mx>] [--order <n>]` | Move a strip to another mixer (at its end) and/or to position --order <n> on it (0 = first); its mixer's strips renumber in their new order, as `mixer move` renumbers mixers. Refused if a route would point backward. | R-MIX-3 |
| `strip freeze <ch>` | Render a strip's OWN sound — its clips or notes through its instrument and rack, at unity, before its fader, to the song's end and its tail — beside the song (Frozen/<strip> <id>.wav, 32-bit float) and play that instead: its clips, notes and devices kept but idle; its fader, pan, sends and output as ever. Only a strip nothing feeds; the song saved (R-PERF-3). | R-PERF-3 |
| `strip unfreeze <ch>` | Play a frozen strip's own work again (the file stays) (R-PERF-3). | R-PERF-3 |
| `strip bounce <ch>` | Render a strip's own sound as a freeze does (Bounces/<strip> <n>.wav) and put it as an audio clip at beat 0 on a NEW strip; the strip itself untouched. Prints the clip, then `made:` (R-PERF-3). | R-PERF-3 |
| `strip relink <ch> [--to <ch>]` | Move EVERY clip playing through <ch> to strip --to, one edit. Refused across kinds: audio clips need an audio strip, note clips an instrument strip; a bus plays no clips. | R-MIX-14 |
| `strip arm <ch\|none> [--input <port>]` | Arm an instrument strip for the MIDI keyboard (R-MIDI-2): what is played sounds through it at once — through the song while it plays, through an input monitor (the song's mix without its clips) while stopped — the sustain pedal held; `none` disarms. Not an edit: the song does not keep it. Opens the MIDI inputs `settings midiInputs` names. Or arm an AUDIO line for its input (R-REC-1): heard through it as `<ch>.monitor` says, recorded by `transport record`; --input <port> sets its input first (that part an edit). | R-MIDI-2 |
| `port add <name> [--dir <in\|out>] [--channels <1\|2>]` | A logical port (R-DEV-3): an output the master or a strip can feed, or an INPUT (--dir in) an audio line records (R-REC-1); this machine's settings say which device it is (`settings set port.<name>=<device>`). Prints its id. | R-REC-1 |
| `strip duck <ch> --under <key-strip> [--amount <dB>]` | A side-chain in one line: a Compressor at the end of <ch>'s rack, its Sidechain on, keyed by a pre-fader sidechain send from --under — attack 1 ms, release half a beat, threshold −30 dB and the ratio that makes a full-scale key cut --amount dB (default 8, 1 … 24). The key must come EARLIER in processing order (R-MIX-4). One edit, one undo; prints the compressor, then `made:`. | R-MIX-18 |
| `route <ch> [--to <ch\|master\|port>]` | Set a strip's main output: a strip LATER in processing order (a later mixer, or after it on its own), master, or an output port. | R-MIX-4 |
| `send add <ch> [--to <ch\|master\|port>] [--gain <dB>] [--pre] [--sidechain]` | Add a send (post-fader unless --pre). Same forward-only rule as `route`. With --sidechain it is a KEY: it feeds the target's compressors' detectors (their Sidechain switch), not its input, and goes to a strip (R-MIX-15). | R-MIX-5 |
| `send delete <sd>` | Remove a send. | R-MIX-5 |
| `device add <ch\|master> [--type <registry type>] [--plugin <id\|name>] [--at <index>] [--preset <name>]` | Insert a DSP registry device into a rack (default: at the end). An instrument goes only first on an instrument strip. --plugin <id\|name> (instead of --type) inserts another maker's effect plugin, from `plugins list`: it plays in a process of its own, its parameters are p<id>, numbers 0 … 1 (R-VST-6). | R-FX-5 |
| `device remove <dv>` | Remove a device from its rack. An instrument strip keeps its instrument. | R-FX-5 |
| `device move <dv> [--to <index>]` | Move a device within its rack. | R-FX-5 |
| `lane add [name] [--strip <ch>]` | Add a timeline lane at the bottom. With --strip <instrument>: that instrument's TRACK — what is put on it plays through it, and it shows the strip's name until given its own (R-LANE-3). | R-LANE-1 |
| `lane delete <ln> [--with-clips]` | Delete a lane. Refused while clips are drawn on it, unless --with-clips. | R-LANE-1 |
| `lane move <ln\|lg> [--to <index>]` | Move a lane to row <index> (0 = the top); the lanes renumber in their new order. A lane moved out of its group's run leaves it, one moved into another group's run joins it; a group (lg_n) moves whole, its first lane to <index> (R-ARR-3). | R-LANE-1 |
| `lane group <ln> … [--name <text>]` | Gather lanes into a NEW named group (R-ARR-3): side by side at the first one's place, in their order; a lane already in another group leaves it. Folded (`set <lg>.folded=on`) the group is one row showing all its clips. Prints its id. | R-ARR-3 |
| `lane ungroup <lg>` | Let a group's lanes go; the group is gone, the lanes stay where they are. | R-ARR-3 |
| `clip add [--src <file>] [--strip <ch>] [--instrument <type>] [--plugin <id\|name>] [--sample <file>] [--pattern <pt>] [--lane <ln\|new>] [--at <beats>] [--length <beats>] [--in <s>] [--out <s>] [--warp] [--preset <name>]` | Place a clip. With --src: an audio clip; a file the song has not used gets its own new strip on the first mixer (R-MIX-2), a used one reuses its strip. With --strip <instrument>: a note clip of --pattern (a new empty 4-beat pattern if none). With --instrument <type>: the same on a NEW instrument strip of that type — what dropping an instrument does (R-BROWSE-3); --plugin <id\|name> the same with another maker's instrument plugin (R-VST-6). With no --lane it goes on the lane its strip's clips are on (the newest one's), a new lane only for a strip with none; --lane new asks for a new one (R-SVC-8). On an instrument's TRACK (--lane <ln>) a note clip plays through that instrument — --strip may be left out, --pattern places that pattern; audio and --instrument are refused there (R-LANE-3). --warp (with --src): it follows the song's tempo, its own detected now (R-WARP-1; refused when none is heard). Prints the clip, then `made:` and whatever else it made. | R-MIX-2 |
| `clip move <ac> [--at <beats>] [--lane <ln>] [--strip <ch>]` | Move a clip in time, to another lane (where it is drawn), and/or another strip (what it sounds through). Onto an instrument's TRACK a note clip is re-routed to that instrument (and says so); a clip on a track told --strip <another> goes to that one's track; audio is refused on a track (R-LANE-3). | R-LANE-2 |
| `clip duplicate <ac> [--at <beats>] [--count <n>] [--lane <ln>]` | A copy right after it (or at --at); --count N makes N copies end to end; --lane puts them on another lane (onto an instrument's TRACK a note clip's copy plays through that instrument; audio is refused there, as a move is — R-CLIP-8). A note clip's copy plays the SAME pattern — linked. Prints the first copy, then `made:` the rest. Shift-drag and Ctrl+V in the window are this line (R-CLIP-9/10). | R-CLIP-3 |
| `clip unique <ac>` | Give a note clip its own copy of its pattern. | R-CLIP-3 |
| `clip delete <ac>` | Remove a clip. | R-LANE-1 |
| `pattern new [--name <text>] [--length <beats>]` | Create an empty pattern. | R-CLIP-2 |
| `note add <pt> [--pitch <pitch>] [--at <beats>] [--length <beats>] [--vel <1-127>] [--chance <0-1>] [--chord <name>] [--inversion <n>] [--octave <n>]` | Add a note to a pattern — every clip of it changes. A pitch is a number, a name (C4 = 60, F#3, Bb2) or a kit's pad (kick). --chord <Cm7> adds a chord instead (its root in --octave, default 4; --inversion n raises its n lowest notes an octave) as ONE edit — the qualities are under Notation. --chance: the odds it sounds in a pass (1). A pattern locked to a scale (`pattern scale`) puts each note on the scale's nearest pitch, a tie down — a major chord on any degree becomes that degree's own triad. | R-CLIP-2 |
| `note delete <pt> [--pitch <pitch>] [--at <beats>]` | Remove the note at that pitch and time. | R-CLIP-2 |
| `note preview <ch> <pitch> [--vel <1-127>] [--length <beats>]` | Hear one note through instrument strip <ch> — its instrument and its rack, at its fader and pan, as it sounds in the song — on a stream of its own, whether the song plays or not (the piano roll's sound when a note is made, a key pressed). A pitch is a number, a name or the strip's kit's pad; --length beats (default 0.5), --vel (default 100). A preview started while one rings takes over with a short fade. Not an edit, never in a render. | R-ROLL-7 |
| `note move <pt> [--pitch <pitch>] [--at <beats>] [--to-pitch <pitch>] [--to-at <beats>] [--length <beats>] [--vel <1-127>] [--chance <0-1>]` | Change the note at --pitch / --at: move it (--to-pitch, --to-at), resize it (--length), set its velocity (--vel) or its chance (--chance) — one gesture, one line. Refused onto another note. In a pattern locked to a scale, --to-pitch goes on to the scale's next pitch the way it moved. | R-ROLL-2 |
| `pattern quantize <pt> [--grid <beats>] [--swing <0-0.75>] [--groove <name>] [--strength <0-1>] [--notes <"pitch@beat …">]` | Move every note's start onto the grid (default a sixteenth, 0.25), delaying every second grid step by --swing of a step — or onto a --groove (R-ROLL-11: a template's steps, offsets and accents, under Notation; it carries its own grid and swing). --strength moves each start (and velocity) that part of the way (1). --notes: only those notes (the roll's selection). Notes that land on one another merge (the louder stays). | R-ROLL-4 |
| `clips move "<ac> …" --by <beats>` | Move several clips in time as ONE edit (R-ARR-3 — a folded group's clips dragged together; any selection): each --by beats (negative = earlier). Refused whole, naming the first, when a clip is not the song's or would start before beat 0. | R-ARR-3 |
| `notes add <pt> "<note> …" [--length <beats>] [--vel <1-127>]` | Many notes in ONE edit (one undo step): each <note> is <pitch>@<beat>[:<length>[:<vel>[:<chance>]]] — C4@0:0.5:90, kick@1, E4@2::70, hat@1.5:::0.5. --length / --vel are the defaults (0.25, 100). A bad token is refused by name and nothing lands. Prints how many. A note past the pattern's end grows the pattern to the end of that bar, and a clip that played it whole with it (R-ROLL-9). A scale-locked pattern puts each on its scale (R-ROLL-11). | R-SVC-8 |
| `notes move <pt> "<pitch@beat> …" [--by <beats>] [--semi <n>]` | Move several notes as ONE edit (one undo step) — the piano roll's selection dragged, or moved by the arrow keys: each named by <pitch>@<beat>, all moved --by beats (default 0) and --semi semitones (default 0). Refused whole, naming the first, when a name matches no note, or a note would land on one not moved, below beat 0 or outside 0–127. | R-ROLL-8 |
| `notes delete <pt> "<pitch@beat> …"` | Remove several notes as ONE edit — the piano roll's selection deleted. Refused whole, naming the first, when a name matches no note. | R-ROLL-8 |
| `pattern steps <pt> "<steps>" [--pitch <pitch>] [--step <beats>] [--vel <1-127>] [--at <beats>] [--length <beats>]` | A step row: x = a note, X = an accent (velocity 127), . or - = a rest, one per --step (default 0.25) from --at (default 0); spaces and \| are ignored. REPLACES that pitch's notes in the span; --length defaults to a step. One edit. | R-SVC-8 |
| `pattern duplicate <pt> [--name <text>]` | A new pattern with the same notes and length (named "<name> copy"). Prints its id. | R-SVC-8 |
| `pattern clear <pt> [--pitch <pitch>]` | Remove every note (or only --pitch's). | R-SVC-8 |
| `pattern delete <pt>` | Delete a pattern. Refused while a clip plays it (naming the clips). | R-SVC-8 |
| `expr add <pt> --lane <bend\|mod\|cc<N>> --at <beats> --value <v>` | A point on one of a pattern's expression lanes (R-MIDI-3): the pitch wheel (`bend`, −1 … +1) or a controller (`cc0` … `cc127`, 0 … 1; `mod` is `cc1`) — linear to the next point, held before the first and after the last; a point already at that beat is moved to the value. The instruments play `bend` and `cc1` (their wheels); every lane goes out in a MIDI file. | R-MIDI-3 |
| `expr delete <pt> --lane <lane> --at <beats>` | Remove the lane's point at --at. | R-MIDI-3 |
| `expr clear <pt> [--lane <lane>]` | Remove a lane's points (--lane), or every lane's. | R-MIDI-3 |
| `marker add <name> --at <beats> [--length <beats>] [--note <text>]` | A named place in the song (R-ARR-1 — Interstellar's marker, in beats): with --length a SECTION, else a point whose section runs to the next marker (or the song's end). On the ruler; `transport seek <mk>` goes there, `transport loop <mk>` loops its section. Prints its id. | R-ARR-1 |
| `marker rename <mk> <name> [--note <text>]` | Rename a marker (and set its --note). | R-ARR-1 |
| `marker move <mk> [--at <beats>] [--length <beats>]` | Move a marker (--at) and/or set its section's --length (0: a point again). | R-ARR-1 |
| `marker delete <mk>` | Delete a marker. | R-ARR-1 |
| `tempo add --at <beats> --bpm <20-999> [--ramp]` | Change the song's tempo (R-TEMPO-1): --bpm from --at (beats after 0; the start's tempo is `project.bpm`) on — or, with --ramp, reached there gliding from the tempo before. Every beat-to-time conversion follows the map: the render, playback, the metronome, recording, warped clips, the MIDI file. Prints its id. | R-TEMPO-1 |
| `tempo move <tp> [--at <beats>] [--bpm <20-999>] [--ramp <true\|false>]` | Move a tempo change (--at), change its --bpm, or whether it is reached gliding (--ramp true\|false). | R-TEMPO-1 |
| `tempo delete <tp>` | Delete a tempo change: the tempo before it holds on. | R-TEMPO-1 |
| `preset save <dv> <name> [--replace]` | Save a device's set values (and a sampler's sound) as a preset of its type in your presets folder (R-PRESET-1) — a name you have is refused without --replace. The machine's, not the song's: no undo. | R-PRESET-1 |
| `preset load <dv> <name>` | Set a device to a preset of its type — every value back to its default, then the preset's, ONE edit; yours of a name before the factory's (R-PRESET-1). `device add --preset` and `clip add --instrument --preset` drop one in. | R-PRESET-1 |
| `preset list [type]` | The presets, yours and the factory's, by type (R-PRESET-1). | R-PRESET-1 |
| `template save <name> [--replace]` | Keep the open song as a template in your templates folder (R-PRESET-2) — as it is, every file it names made absolute; a name you have refused without --replace. The machine's, not the song's: no undo. `project new <path> --template <name>` starts a song from it. | R-PRESET-2 |
| `template list` | The song templates, yours and the factory's (R-PRESET-2). | R-PRESET-2 |
| `template delete <name>` | Delete a template of yours (a factory's is refused) — no undo (R-PRESET-2). | R-PRESET-2 |
| `preset delete <type> <name>` | Delete a preset of yours (a factory's is refused) — the machine's, no undo (R-PRESET-1). | R-PRESET-1 |
| `time insert --at <beats> --length <beats>` | Open a gap in the whole song (R-ARR-2), ONE edit: every clip, automation point, marker and the loop at or after --at moves --length beats later; a section, a looping automation's span or the loop that holds --at grows by it. A clip that starts before --at and crosses it stays (it is named — split it to open the gap through it, R-EDIT-1). The loop is the session's: undo leaves it. | R-ARR-2 |
| `time delete --from <beats> --to <beats>` | Take a span out of the whole song (R-ARR-2), ONE edit: what lies wholly inside it goes (clips, automation points, markers), what follows moves earlier by its length; a section, a looping span or the loop that crosses it shrinks. A clip that repeats its material (a note clip, an audio clip set to loop) and starts before --from is shortened by the overlap; any other clip crossing --from or --to is refused, naming it — split it first (R-EDIT-1) or move it. | R-ARR-2 |
| `pattern scale <pt> <key> <scale> \| off` | Lock a pattern to a scale (R-ROLL-11) — `pattern scale pt_1 D dorian`: a note placed (`note add`, its --chord, `notes add`) lands on the scale's nearest pitch, a tie going down (so a major chord stamped on any degree is that degree's own triad); a note moved in pitch (`note move --to-pitch`, `notes move --semi`) goes on to the scale's next pitch the way it moved. A move in time, an import, a step row, a recording and `pattern transpose` keep their pitches; notes already written stay. `off` unlocks it. The scales are under Notation. Prints its notes. | R-ROLL-11 |
| `notes set <pt> "<pitch@beat> …" [--vel <1-127>] [--chance <0-1>] [--length <beats>]` | Set several notes' --vel, --chance and/or --length as ONE edit — the piano roll's selection. Refused whole, naming the first, when a name matches no note. | R-ROLL-11 |
| `pattern humanise <pt> [--timing <beats>] [--vel <0-127>] [--seed <n>] [--notes <"pitch@beat …">]` | Loosen the notes (R-ROLL-11): each start moved within ± --timing beats and each velocity within ± --vel (with neither: 0.02 and 8), a triangular spread from a seed — the same --seed on the same notes writes the same notes (by default a hash of the notes; printed). A note never goes before beat 0, nor onto another of its pitch (it keeps its time). --notes: only those notes (the roll's selection). | R-ROLL-11 |
| `pattern legato <pt> [--notes <"pitch@beat …">]` | End every note where the next start begins — a chord's notes together, the last at the pattern's end; an overlap is shortened as a gap is filled (R-ROLL-11). --notes: only those notes, the next start among them. | R-ROLL-11 |
| `pattern arpeggiate <pt> [--rate <beats>] [--mode <up\|down\|updown\|random>] [--octaves <1-4>] [--gate <0.05-1>] [--seed <n>] [--notes <"pitch@beat …">]` | A MIDI effect written as notes (R-ROLL-11): on every --rate step of the beat grid (0.25) where notes are held, the next pitch of what is held there — its pitches over --octaves (1), up, down, updown (the turns not repeated) or a seeded random (--seed) — --gate of a step long (1), at the held note's velocity and chance; it starts again from the first after a rest. Replaces the notes it arpeggiated (every note, or --notes); a step on another note of its pitch replaces that one. Prints how many. | R-ROLL-11 |
| `pattern import <file.mid> [--track <n>] [--at <beats>] [--lane <ln>] [--instrument <type>] [--name <text>]` | Make patterns of a Standard MIDI File (format 0/1/2; ticks or SMPTE time): one per track with notes, or --track n's alone; whole bars long; named after the track. With --lane <an instrument's track> or --instrument <type>: ONE pattern (every track merged, or --track's) and a clip of it at --at, by `clip add`'s rules — what dropping a .mid does. Prints the (first) pattern; `made:` the rest. | R-MIDI-1 |
| `pattern export <pt> <file.mid>` | Write a pattern as a MIDI file (format 1, 960 ticks a beat — Solaris's own, so it imports back exact; channel 10 when a kit plays it). | R-MIDI-1 |
| `project export-midi <file.mid>` | Write the song's notes as a MIDI file: a track per instrument strip, every note clip's pattern where the clip plays it (repeated to fill it), the song's tempo and meter. | R-MIDI-1 |
| `pattern transpose <pt> [--semi <n>]` | Move every note by --semi semitones (negative = down). Refused if a note would leave 0–127. | R-SVC-8 |
| `pattern print <pt>` | Its notes, one per line in the `notes add` notation (pitch as a name or pad), the number after `#`. | R-SVC-8 |
| `auto create <address> [--pump]` | Automate a number: a new automation named "<owner> · <parameter>", ranged as it, holding its value from beat 0 to the song's end, shown on the timeline — and the address bound to `=au_n`. Prints the id. --pump: already LOOPING every beat (R-AUTO-13) — down on the beat (18 dB for a level, else to 20 % of its range), back to its value by the middle of the beat: the side-chain feel. Loop any automation with `set <au>.loop=<beats>` (`.at`, `.length`: where it repeats, R-AUTO-12). | R-AUTO-5 |
| `auto add [--name <text>] [--min <v>] [--max <v>] [--unit <text>]` | An automation from nothing (default range 0…1). It moves nothing until a formula reads it. Prints the id. | R-AUTO-4 |
| `auto delete <au> [--unbind]` | Delete an automation. Refused while a formula reads it, unless --unbind (those bindings are cleared). | R-AUTO-4 |
| `auto point add <au> [--at <beats>] [--value <v>] [--shape <linear\|hold\|smooth\|bezier>]` | Add a point (one at the same beat is replaced). Its shape governs the segment after it (a bezier point's handles start flat: speed 0, influence 33.333 %). | R-AUTO-6 |
| `auto point move <au> [--at <beats>] [--to <beats>] [--value <v>]` | Move the point at --at to another beat and/or value. | R-AUTO-6 |
| `auto point delete <au> [--at <beats>]` | Remove the point at --at. | R-AUTO-6 |
| `auto point shape <au> [--at <beats>] [--shape <linear\|hold\|smooth\|bezier>] [--speed-in <v/beat>] [--influence-in <%>] [--speed-out <v/beat>] [--influence-out <%>]` | Set how the curve leaves the point at --at. A BEZIER point is Interstellar's key (R-ANIM-2): on each side a speed — the slope there, in the automation's unit per beat — and an influence, how far into the neighbouring segment it pulls (0 < x ≤ 100 %); flags not given are kept. One line per handle gesture. | R-AUTO-10 |
| `bind clear <address>` | Clear an address's formula: it plays its own stored value again. (`set <address>=<number>` clears and sets.) | R-AUTO-1 |
| `eval <address> [--at <beats>] [--explain]` | The value an address plays at a beat (default 0); --explain shows its formula and every name it reads. | R-AUTO-8 |
| `render --out <file> [--format <wav\|flac\|mp3\|ogg\|opus>] [--rate <Hz>] [--bits <16\|24\|32f>] [--kbps <n>] [--normalize <LUFS>] [--ceiling <dBTP>] [--from <beats>] [--to <beats>] [--stems <ch,…\|all>] [--ports] [--measure]` | Render offline: the master to --out; with --stems, each named strip's (`all`: every strip's) post-fader output to <out>.<ch>.<ext>; with --ports, each output port to <out>.<port>.<ext>. The tail runs until −90 dBFS or 10 s. Every file starts at --from exactly: the latency of a lookahead device (a limiter) is compensated and trimmed (R-MIX-17). The format is the extension — .wav, .flac, .mp3, .ogg (Vorbis), .opus — or --format (R-EXPORT-1). --rate renders the song AT that rate (Opus: 48000, its default; MP3 to 48000); --bits 16 \| 24 (WAV and FLAC; default 24) \| 32f (WAV) — 16 and 24 TPDF-dithered; --kbps for the lossy ones (MP3 32…320, default 320; Ogg 45…500, 256; Opus 6…510, 192). --normalize gains every file alike to that integrated loudness, held so the master's true peak stays under --ceiling (dBTP, default −1) — said on a `normalize` line. --measure prints the written master's integrated loudness, the most of its momentary and short-term, its true peak and its lowest correlation (R-METER-1). | R-RENDER-2 |
| `matrix print [--json]` | Every route and send at once: rows = strips, columns = destinations. | R-MIX-9 |
| `audit` | The mix report: unused strips, clips on muted strips, unreachable strips, single-input buses, offline media, unknown devices, clipping. | R-MIX-10 |
| `state print [--json] [--stable] [--compact]` | The whole AppModel; --stable omits what changes with time. --json --compact: the SONG only (no registry, machine, recents or browser), on one line; a device lists only its non-default parameters as `name=value`, a pattern its notes in the `notes add` notation. | R-SVC-2 |
| `ls` | The song as a tree of ids and names: mixers → strips (kind, devices, output, sends), the master, lanes → clips (at, length, pattern or file), patterns (length, notes, clips), automations. | R-SVC-8 |
| `show <id>` | One node's fields that differ from their defaults; a device's non-default parameters with their units (and the default); `show project` for the song. | R-SVC-8 |
| `api [--json] [--md]` | This document: every command, event, model field and device parameter. | R-API-1 |
| `settings set <key>=<value> …` | The MACHINE's settings (never a song's): sampleRate (new songs, and the clock device's rate), bufferSize (frames), output / input (device ids from `devices list`; empty = the system default), port.<name>=<device>:<channel> (where a song's port plays on this machine), metronome (on\|off), metronomeLevel (dB), auditionLevel (dB, the browser's preview), newBpm / newSig (what a new song starts at), reducedMotion (on\|off), showIds (on\|off: every object's id beside its name, R-UI-11), autosave (seconds between autosaves of an unsaved song, or off — R-SAFE-1), key.<action>=<combos> (R-SET-4: a keyboard action's keys — a comma list such as "Ctrl+Shift+Z, Ctrl+Y", `none`, or `default`; a combo another action of the same keyboard has is refused naming it — the actions are in this document's Keyboard section). Saved at once. | R-SET-1 |
| `settings print [--json]` | The machine's settings. | R-SET-1 |
| `folder add <path>` | Add a sample folder to the browser's quick-access list. | R-SET-1 |
| `folder remove <path>` | Remove a sample folder from the list (the folder itself is untouched). | R-SET-1 |
| `folder move <path> [--to <index>]` | Reorder the sample folders. | R-SET-1 |
| `devices list` | List this machine's audio devices (ids for `settings set output=…`). | R-DEV-1 |
| `browse [folder] [--search <text>] [--favourites]` | List a folder for the browser: sub-folders, audio files, songs. --search <text> (R-PRESET-3): the audio files whose name holds it or that carry a tag starting with it — in the sample folders (or <folder>) eight levels down, the first 200, and the library's tagged files; --favourites: the favourites. | R-BROWSE-1 |
| `sample tag <file> <tag>` | Tag a sound in the machine's library (R-PRESET-3): a word of letters, digits or dashes, kept in lower case. No undo. | R-PRESET-3 |
| `sample untag <file> <tag>` | Take a tag off a sound (R-PRESET-3). | R-PRESET-3 |
| `sample favourite <file> [on\|off]` | Mark a sound a favourite, or not (R-PRESET-3; on when not said). | R-PRESET-3 |
| `plugins scan` | Find the VST3 plugins where this machine keeps them (R-VST-6: SOLARIS_VST3_PATH, else ~/.vst3, /usr/lib/vst3, /usr/local/lib/vst3) — each bundle read in a process of its own, so one that crashes loses only itself (said). Prints each found and each problem; the list is the machine's, kept. | R-VST-6 |
| `plugins list` | The VST3 plugins the last scan found (R-VST-6). | R-VST-6 |
| `device reload <dv>` | Start a plugin device's process again — after a crash or an overrun took it offline — from its last state (R-VST-6). Not an edit. Prints `playing`, or `offline: <why>`. | R-VST-6 |
| `map add <cc> <address>` | Bind a MIDI control — `<n>` (0–127, on any channel) or `<ch>/<n>` (on channel 1–16) — to a number a formula can drive (R-CTRL-1): moved, it sets it over its travel (a gain by the fader law, a device parameter by its taper). One address per control. The machine's setting (`map.<cc>` in its file), not an edit. | R-CTRL-1 |
| `map remove <cc>` | Forget a MIDI control's mapping (R-CTRL-1). | R-CTRL-1 |
| `map list` | Every MIDI mapping: `<cc> <address>`, `(inert)` where the open song has no such address (R-CTRL-1). | R-CTRL-1 |
| `map learn [<address>] [--cancel]` | Wait for the next MIDI control moved and bind it — on its channel — to <address> (said: `map.learned`; its value is not taken); --cancel stops waiting (R-CTRL-1). | R-CTRL-1 |
| `device editor <dv> [--close]` | Open a plugin device's OWN editor in a window of its own — the plugin's process makes it (R-VST-6) — or, with --close, close it. What is turned there reaches the song as `set <dv>.p<id>`. Not an edit. Prints `open` or `closed`. | R-VST-6 |
| `recents remove <path>` | Take a song off Home's recent list (the file is untouched). | R-HOME-1 |
| `transport play [--from <beats>] [--clip <ac>]` | Play on the clock device (settings output), from --from or where the transport stands. Edits while playing are heard: a gain, a pan, a mute, a solo, a device parameter at once; anything structural by a new engine swapped in at the same position. --clip plays that clip's span looped, the whole song sounding there (the piano roll's play, R-ROLL-6): it follows the clip as it is edited, and the song's own loop comes back at `transport stop`, a plain `transport play` or a `transport loop`. | R-PLAY-1 |
| `transport stop [--return]` | Stop; the transport stays where it was heard — or, with --return, goes back to the cue: where the time bar was last put (a seek, or a play's --from or --clip; `transport.cue`). A plain stop's place is temporary: the next play starts there but the cue stays. | R-TIME-7 |
| `transport seek <beats\|mk>` | Move the transport (playing or not) — to a beat, or to a marker (R-ARR-1). | R-TIME-4 |
| `midi devices` | This machine's MIDI inputs (ALSA sequencer ports), each marked when Solaris listens to it — the names `settings set midiInputs=` takes. | R-MIDI-2 |
| `meters clear` | Clear the meters: every clip light and held peak, and the master's true peak and integrated loudness start again (the song is not edited; a click on a clip light). | R-METER-2 |
| `transport record [--from <beats>] [--count-in <bars>] [--overdub]` | Play and record the armed strip's keyboard (R-MIDI-2): at `transport stop` what was played becomes a new note clip on its track from the bar it began in — or, with --overdub, notes added to the strip's clip under the start — each note where it was HEARD, its start on the `recordQuantize` grid; one undo takes it back. --count-in (else `settings countIn`) bars of click first, the song waiting. With an AUDIO line armed (R-REC-1): its input from where recording begins to the stop, written beside the song (`Recordings/<strip> <n>.wav`) and put on the strip as a clip where it began — moved earlier by the output's and the input's latency, kept only inside `transport punch` — one undo. | R-MIDI-2 |
| `clip comp <ac> --take <n> [--from <beats>] [--to <beats>]` | Choose which take plays where (R-REC-2): take <n> (1-based) from --from to --to (song beats; the whole clip without them) — ONE edit, the range overwriting what it covers. Where no range says, the last take plays; the seams are crossfaded 5 ms. A loop recording makes the takes. | R-REC-2 |
| `clip split <ac> --at <beats>` | Cut an audio clip in two at --at (song beats, inside it) — ONE edit (R-EDIT-1): the first half keeps the id and the fade in, the second (printed) begins at --at with the fade out; together they play what the one did. A reversed clip's first half is its span's end; a clip with takes keeps them all, its comp divided. A looping clip is cut only where a repetition begins; a note clip not at all (its notes are its pattern's). | R-EDIT-1 |
| `clip consolidate <ac> [<ac> …]` | Join audio clips of one strip into ONE (R-EDIT-1): what they play — gain, fades, crossfades, reverse, comps — rendered raw (before the strip's devices, fader and pan) from the first's start to the last's end, written beside the song as Bounces/<strip> <n>.wav (32-bit float) and put in their place on the first's lane; one edit, the new clip printed. A note clip is refused (a strip's bounce is R-PERF-3). | R-EDIT-1 |
| `clip slice <ac> [--every <beats>] [--transients] [--sensitivity <0-1>] [--kit]` | Cut an audio clip into clips (R-EDIT-1): --every <beats> from its start, or --transients where a sound begins (the DSP library's onset detector, REQ-onset-1; --sensitivity 0 … 1, 0.5) — ONE edit, the clips printed. --every with --kit makes a new instrument track instead: a Sampler of the clip's file in N equal slices (mode=slices) and a note clip in the clip's place playing them in order (root + k for slice k), so it plays what the clip did; the clip stays. A looping clip is not sliced. | R-EDIT-1 |
| `transport punch <from\|off> [to]` | Punch in and out (R-REC-1): a recording keeps only what the song plays from <from> to <to> beats, and its clip starts at <from>; `off` keeps it all. The session's, as the loop. | R-REC-1 |
| `transport loop <from\|off\|mk> [to]` | Loop between two beats while playing — or a marker's section (R-ARR-1: its length, else to the next marker); `off` ends it. | R-TIME-4 |
| `audition <file\|stop>` | Hear a sample now, outside the song (the browser's preview): through the output device at the audition level, to its end; `stop` ends it. Not an edit, never in a render. | R-EDM-9 |
| `wait <seconds>` | Let time pass (playback goes on, the model's transport and meters update) — for scripts that listen. | R-PLAY-3 |

## Addresses (`set` / `get`)

| address | value |
|---|---|
| `project.name` · `project.bpm` · `project.sig` · `project.masterGain` · `project.sampleRate` | text · 20…999 · n/d · dB · Hz |
| `<strip>.name` · `.gain` · `.pan` · `.mute` · `.solo` · `.colour` | text · dB (−120…12) · −1…1 · bool · bool · −1…15 |
| `<clip>.name` · `.at` · `.length` · `.fadeIn` · `.fadeOut` · `.gain` · `.loop` · `.in` · `.out` · `.reverse` · `.warp` · `.bpm` · `.pitch` | text · beats · beats · beats · beats · dB · bool · s · s · bool (audio, R-EDIT-1) · bool · bpm 20 … 999 · semitones −24 … +24 (audio, R-WARP-1) |
| `<lane>.name` · `.colour` · `.strip` (`<ch>` an instrument: the lane is its track; `none`: plain — R-LANE-3) · `<mixer>.name` · `<send>.gain` · `.pre` · `.sidechain` (a key, R-MIX-15) · `<sampler>.sample` (its sound, a file; R-EDM-8) · `<pattern>.name` · `.length` · `<port>.name` · `.channels` | |
| `<lane group>.name` · `.folded` · `<lane>.group` (R-ARR-3) | text · `on` (one row) / `off` · `<lg>` joins it (gathered with it) / `none` leaves it (placed after it) |
| `<device>.bypass` · `<device>.<param>` | bool · any parameter of its type below, in its unit; a choice by name |
| `<plugin device>.p<id>` (type `vst3`, R-VST-6) | 0 … 1 — another maker's plugin's parameter by its id (its title is `devices[].params[].label`); read: also `.plugin` (its class id) |
| `<automation>.name` · `.unit` · `.min` · `.max` · `.loop` · `.at` · `.length` (read: also `.from` · `.points`) | text · text · number · number · beats (0: no loop) · beats · beats (0: to the end) — R-AUTO-12 |
| any NUMBER above (a strip's gain/pan, a send's gain, `project.masterGain`, a numeric device parameter) `=<formula>` | binds it (R-AUTO-1): numbers, `+ - * / ^ ( )`, `sin cos tan abs sign min max clamp lerp pow exp log sqrt floor ceil round frac`, `pi`, `beat bar bpm t`, an automation id (`au_1`), another numeric address (a link). `get` prints the formula; a plain number clears it |

## Notation (R-SVC-8)

| what | written | e.g. |
|---|---|---|
| a pitch (`--pitch`, `--to-pitch`, a note) | a number 0–127, a name — a letter, `#`/`b`, an octave; C4 = 60, c-1 = 0 — or a kit's pad (below, case and `-` ignored) | `60` · `C4` · `F#3` · `Bb2` · `kick` · `closed-hat` |
| a note (`notes add`) | `<pitch>@<beat>[:<length>[:<vel>[:<chance>]]]`; an empty field keeps the default; the chance 0 … 1 (R-ROLL-11) | `C4@0:0.5:90` · `kick@1` · `E4@2::70` · `hat@1.5:::0.5` |
| a step row (`pattern steps`) | `x` a note, `X` an accent (127), `.` or `-` a rest; spaces and `\|` ignored | `x...x...x...X...` |
| a chord (`note add --chord`) | a root (`C`, `F#`, `Bb`) then a quality below; the root sits in `--octave` (4) | `Cm7` · `F#dim` · `Bbmaj7` |

| chord quality | semitones | |
|---|---|---|
| `C` · `Cmaj` · `CM` | 0 4 7 | major |
| `Cm` · `Cmin` · `C-` | 0 3 7 | minor |
| `C5` | 0 7 | power (root and fifth) |
| `Cdim` | 0 3 6 | diminished |
| `Caug` · `C+` | 0 4 8 | augmented |
| `Csus2` | 0 2 7 | suspended second |
| `Csus4` · `Csus` | 0 5 7 | suspended fourth |
| `C6` | 0 4 7 9 | major sixth |
| `Cm6` | 0 3 7 9 | minor sixth |
| `C7` | 0 4 7 10 | dominant seventh |
| `Cmaj7` · `CM7` | 0 4 7 11 | major seventh |
| `Cm7` · `Cmin7` · `C-7` | 0 3 7 10 | minor seventh |
| `Cmmaj7` | 0 3 7 11 | minor-major seventh |
| `Cdim7` | 0 3 6 9 | diminished seventh |
| `Cm7b5` | 0 3 6 10 | half-diminished |
| `C7sus4` | 0 5 7 10 | dominant seventh, suspended fourth |
| `Cadd9` | 0 4 7 14 | major, added ninth |
| `Cmadd9` | 0 3 7 14 | minor, added ninth |
| `C9` | 0 4 7 10 14 | dominant ninth |
| `Cmaj9` | 0 4 7 11 14 | major ninth |
| `Cm9` | 0 3 7 10 14 | minor ninth |

| a scale (`pattern scale`, R-ROLL-11) | its pitch classes above the key | |
|---|---|---|
| `major` · `ionian` | 0 2 4 5 7 9 11 | major |
| `minor` · `aeolian` · `natural-minor` | 0 2 3 5 7 8 10 | natural minor |
| `harmonic-minor` | 0 2 3 5 7 8 11 | harmonic minor |
| `melodic-minor` | 0 2 3 5 7 9 11 | melodic minor (rising) |
| `dorian` | 0 2 3 5 7 9 10 | dorian |
| `phrygian` | 0 1 3 5 7 8 10 | phrygian |
| `lydian` | 0 2 4 6 7 9 11 | lydian |
| `mixolydian` | 0 2 4 5 7 9 10 | mixolydian |
| `locrian` | 0 1 3 5 6 8 10 | locrian |
| `major-pentatonic` · `pentatonic` | 0 2 4 7 9 | major pentatonic |
| `minor-pentatonic` | 0 3 5 7 10 | minor pentatonic |
| `blues` | 0 3 5 6 7 10 | blues (minor pentatonic and the flat fifth) |

A groove (`pattern quantize --groove`, R-ROLL-11) is a cycle of grid steps from beat 0, each moved by its offset (a fraction of a step, + late) and its velocity scaled by its accent — empirical, shaped by ear.

| groove | step | offsets | accents | |
|---|---|---|---|---|
| `mpc-54` | 0.25 | 0.0 0.08 | 1.0 1.0 | the MPC's 54 % sixteenth swing |
| `mpc-58` | 0.25 | 0.0 0.16 | 1.0 1.0 | the MPC's 58 % sixteenth swing |
| `mpc-62` | 0.25 | 0.0 0.24 | 1.0 1.0 | the MPC's 62 % sixteenth swing |
| `mpc-66` | 0.25 | 0.0 0.32 | 1.0 1.0 | the MPC's 66 % — a sixteenth triplet |
| `shuffle` | 0.5 | 0.0 0.3333333333333333 | 1.0 0.8 | an eighth-note triplet shuffle, the off-beat softer |
| `hip-hop` | 0.25 | 0.0 0.2 0.05 0.25 | 1.0 0.7 0.85 0.65 | laid back: sixteenths late, the off-beats more, the weak ones soft |
| `house` | 0.25 | 0.0 0.02 -0.03 0.08 | 1.0 0.55 0.9 0.7 | the off-beat eighth early and loud, the last sixteenth late |
| `push` | 0.5 | 0.0 -0.12 | 1.0 0.9 | the off-beat eighths early — ahead of the beat |
| `lazy` | 0.25 | 0.0 0.1 0.06 0.14 | 1.0 0.8 0.9 0.75 | every sixteenth behind the beat, the off-beat most |

## Keyboard (R-SET-4)

Every shortcut is an action; `settings set key.<id>=<combos>` gives it this machine's keys (`none` takes them all away, `default` gives these back). A combo is `[Ctrl+][Alt+][Shift+]<key>` — the key A–Z, 0–9, F1–F12, Backspace, Tab, Enter, Escape, Space, PageUp, PageDown, End, Home, Left, Up, Right, Down, Insert, Delete, Semicolon, Equal, Comma, Minus, Period, Slash, Backquote, BracketLeft, Backslash, BracketRight, Quote — and a list is comma-separated (`"Ctrl+Shift+Z, Ctrl+Y"`, quoted for its space). Transport, Song and Lanes are one keyboard and the piano roll another: a combo means one action on each, and the roll's shadow the song's while a roll has the keys. `settings.keys` in the model is every action with its keys now.

| action | area | what | default |
|---|---|---|---|
| `transport.play` | Transport | Play / stop — back to where the time bar was put | Space |
| `transport.stopHere` | Transport | Stop here | Ctrl+Space |
| `transport.toStart` | Transport | To the start | Enter |
| `song.save` | Song | Save | Ctrl+S |
| `song.undo` | Song | Undo | Ctrl+Z |
| `song.redo` | Song | Redo | Ctrl+Shift+Z, Ctrl+Y |
| `song.settings` | Song | Settings | Ctrl+Comma |
| `lanes.delete` | Lanes | Delete the clip | Delete, Backspace |
| `lanes.duplicate` | Lanes | Duplicate the clip | Ctrl+D |
| `lanes.copy` | Lanes | Copy the clip | Ctrl+C |
| `lanes.paste` | Lanes | Paste it at the time bar | Ctrl+V |
| `lanes.split` | Lanes | Split the clip at the time bar | Ctrl+E |
| `roll.selectAll` | Piano roll | Select all | Ctrl+A |
| `roll.deselect` | Piano roll | Deselect | Escape |
| `roll.delete` | Piano roll | Delete | Delete, Backspace |
| `roll.duplicate` | Piano roll | Duplicate | Ctrl+D |
| `roll.copy` | Piano roll | Copy | Ctrl+C |
| `roll.paste` | Piano roll | Paste | Ctrl+V |
| `roll.up` | Piano roll | Up a semitone | Up |
| `roll.down` | Piano roll | Down a semitone | Down |
| `roll.octaveUp` | Piano roll | Up an octave | Shift+Up |
| `roll.octaveDown` | Piano roll | Down an octave | Shift+Down |
| `roll.left` | Piano roll | Earlier by the snap | Left |
| `roll.right` | Piano roll | Later by the snap | Right |

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
| `project.changed` | what, node | A node was added, removed, moved or re-routed. what = e.g. strip.added, clip.moved, route.set. Every node a command makes is announced, one event each (R-SVC-8). |
| `params.changed` | address, value | An address was written; value is what was STORED, canonical (a value out of range is refused, never clamped — R-SVC-8). |
| `render.finished` | out, frames, peak | A render was written; peak is the master's, dBFS. |
| `render.failed` | why | A render stopped with an error. |
| `audit.report` | findings | How many findings `audit` printed. |
| `settings.changed` | what | A machine setting changed and was saved: sampleRate, bufferSize, output, input, port.<name>, folders, key.<action> (R-SET-4) … |
| `devices.changed` | count | `devices list` found this many devices. |
| `browse.changed` | path, entries | The browser now lists this folder — or, a search (path `search:<text>` / `favourites`), what it found. |
| `library.changed` | path, tags | R-PRESET-3: a file's tags (favourite among them) changed and were saved; tags comma-separated, '' none. |
| `plugins.changed` | count, problems | R-VST-6: a scan found this many plugins; problems: bundles that could not be read. |
| `plugin.offline` | device, why | R-VST-6: a song's plugin is not playing — it never started, crashed or overran its budget; an effect passes dry, an instrument is silent (`device reload <dv>`). Said once. |
| `surface.changed` | protocol, connected, bank, why | R-CTRL-2: the control surface connected (1) or not (0, why), or moved to another bank of eight. |
| `map.learned` | cc, address | R-CTRL-1: the MIDI control moved during a `map learn` is bound to the address (on its channel: `<ch>/<n>`). |
| `plugin.editor` | device, open | R-VST-6: a plugin's own editor window opened (1) or closed (0) — by `device editor`, or its window's own close. |
| `recents.changed` | count | Home's recent songs changed. |
| `transport.changed` | playing, position, loop | Play, stop, seek or loop: playing 1/0, position in beats, loop `from-to` or `off`. |
| `audition.changed` | file, playing | A sample previewed from the browser started (playing 1) or ended — stopped, or to its end (playing 0). |
| `note.previewed` | strip, pitch | A note was heard through a strip, outside the song (`note preview`, the piano roll's sound — R-ROLL-7). |
| `project.autosaved` | path | The unsaved song was written beside it (`<song>.autosave`, R-SAFE-1). |
| `project.recovery` | path, age | The song just opened has an autosave newer than it (age in seconds) — `project recover` restores it, `project discard-recovery` drops it (R-SAFE-2). |
| `record.finished` | clip, notes, seconds, takes | A recording landed: its notes in this clip ('' — nothing was played) (R-MIDI-2); an audio take, how long it is (R-REC-1); how many takes a looped recording made (R-REC-2). |
| `strip.armed` | strip | The MIDI keyboard plays this strip now ('' — none); the session's, not the song's (R-MIDI-2). |
| `meters.cleared` |  | The clip lights and held peaks are out; the master's true peak and integrated loudness start again (R-METER-2). |

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
| `redoDepth` | int | steps `redo` can put back (down the newest branches) |
| `history` | object[] | the song's history TREE, oldest first (R-HIST-2) — what `history` prints |
| `history[].id` | int | never reused while the song is open; `history jump <id>` |
| `history[].parent` | int | -1 at the root (the song as it was opened or made) |
| `history[].label` | string | what made it: the command (`set ch_2.gain`), or New song / Opened |
| `historyCurrent` | int | the node the song is now (-1: no song) |
| `recovery` | string | the open song's path when it has an autosave newer than it — `project recover` restores it (R-SAFE-2) |
| `recoveryAge` | number | how old that autosave is, seconds (-1: none) *(not in `--stable`)* |
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
| `strips[].input` | string | R-REC-1: the input port it monitors and records; empty = none |
| `strips[].monitor` | string | R-REC-1: auto (heard while armed and stopped or recording) \| on \| off |
| `strips[].audible` | bool | false when muted or silenced by another strip's solo (R-MIX-7) |
| `strips[].colour` | int | an index into the track colours: its own, else from its id — never −1 |
| `strips[].sends` | object[] | its sends |
| `strips[].sends[].id` | string |  |
| `strips[].sends[].to` | string | a strip on a later mixer, master, or a port |
| `strips[].sends[].gain` | number | dB |
| `strips[].sends[].pre` | bool | pre-fader |
| `strips[].sends[].gainFormula` | string | the formula driving the send's gain, "" = none |
| `strips[].sends[].sidechain` | bool | a sidechain key: into the target's compressors' detectors, not its input (R-MIX-15) |
| `strips[].devices` | object[] | the rack, in order (an instrument strip's first is its instrument) |
| `strips[].devices[].id` | string |  |
| `strips[].devices[].type` | string | the DSP registry type |
| `strips[].devices[].label` | string | the registry's label |
| `strips[].devices[].instrument` | bool |  |
| `strips[].devices[].bypass` | bool |  |
| `strips[].devices[].takesSample` | bool | it plays a recorded sound (a sampler, R-EDM-8) |
| `strips[].devices[].takesDefinition` | bool | R-INST-9: … its sample an instrument's definition — an .sfz naming its sounds (a multi-sample) |
| `strips[].devices[].sample` | string | that sound, as stored (relative to the song); "" = none |
| `strips[].devices[].known` | bool | false when this build's registry lacks the type (kept, not played) |
| `strips[].devices[].lastChanged` | string | the parameter last written, by anyone (R-WIN-2) |
| `strips[].devices[].plugin` | string | R-VST-6: type vst3 — another maker's plugin, its class id (its name is `label`); "" = a registry device |
| `strips[].devices[].offline` | bool | R-VST-6: the plugin is not playing (never started, crashed, overran) — an effect passes dry, an instrument is silent |
| `strips[].devices[].why` | string | R-VST-6: why it is offline (`device reload <dv>` starts it again) |
| `strips[].devices[].editor` | bool | R-VST-6: the plugin's own editor window is open (`device editor <dv>`) |
| `strips[].devices[].groups` | object[] | R-UI-12: its parameter groups' titles, the registry's — `prefix` (before a parameter's dot, "" for none) and `label` |
| `strips[].devices[].groups[].prefix` | string | the prefix before a parameter's dot; "" = the parameters with none |
| `strips[].devices[].groups[].label` | string | its title, for a person |
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
| `strips[].rms` | number[] | RMS over the last 300 ms, L/R (R-METER-2) *(not in `--stable`)* |
| `strips[].hold` | number[] | the held peak, L/R: the most of the last 1.5 s, then falling 20 dB a second *(not in `--stable`)* |
| `strips[].clip` | bool | reached full scale since `meters clear` — a latch *(not in `--stable`)* |
| `strips[].cpu` | number | R-PERF-1: its share of the audio thread's time, of a block (0 stopped) *(not in `--stable`)* |
| `strips[].frozen` | string | R-PERF-3: the file it plays instead of its own work ('' not frozen) — `strip freeze\|unfreeze` |
| `strips[].clipCount` | int | fed by: clips playing through it (R-MIX-8) |
| `strips[].fromLanes` | string[] | fed by: the lanes those clips are drawn on |
| `strips[].fromStrips` | string[] | fed by: strips whose output or a send lands here |
| `strips[].targets` | string[] | where its output or a send may go (R-MIX-4): strips LATER in processing order (a later mixer, or after it on its own), master, output ports |
| `strips[].keyTargets` | string[] | where a sidechain key may go (R-MIX-15): the strips of targets |
| `masterDevices` | object[] | the master's rack, shaped like strips[].devices |
| `lanes` | object[] | timeline rows, in order |
| `lanes[].id` | string |  |
| `lanes[].name` | string |  |
| `lanes[].order` | int |  |
| `lanes[].colour` | int | −1 = none |
| `lanes[].strip` | string | R-LANE-3: the instrument strip it is the track of — what is on it plays through it; empty = a plain lane |
| `lanes[].ownName` | bool | false: a track showing its instrument strip's name (renaming the strip renames it) |
| `lanes[].group` | string | R-ARR-3: the lane group it is in; empty = none |
| `groups` | object[] | R-ARR-3: lane groups, in the order of their first lanes — each group's lanes side by side |
| `groups[].id` | string | lg_n |
| `groups[].name` | string |  |
| `groups[].folded` | bool | folded: its lanes are ONE row on the timeline |
| `groups[].lanes` | string[] | its lanes, in order |
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
| `clips[].takes` | string[] | R-REC-2: a loop recording's passes, the files, in the order played (1-based in the comp) |
| `clips[].comp` | object[] | R-REC-2: which take plays where — song beats [from, to); elsewhere the last take |
| `clips[].comp[].take` | int | 1-based |
| `clips[].comp[].from` | number | beats |
| `clips[].comp[].to` | number | beats |
| `clips[].plays` | object[] | R-REC-2: the comp resolved — runs covering the clip, each the take heard there (the last where the comp says nothing) |
| `clips[].plays[].take` | int |  |
| `clips[].plays[].from` | number | beats |
| `clips[].plays[].to` | number | beats |
| `clips[].reverse` | bool | R-EDIT-1: the span played backwards (audio) |
| `clips[].fadeInHeard` | number | beats — R-EDIT-1: the fade in as heard: grown to cover an earlier clip overlapping it on its lane (a crossfade) |
| `clips[].fadeOutHeard` | number | beats — R-EDIT-1: the fade out as heard: grown to cover a later clip overlapping it on its lane |
| `clips[].warp` | bool | R-WARP-1: it follows the song's tempo — it lasts its span's own beats, stretched |
| `clips[].bpm` | number | R-WARP-1: its own tempo (0: not known) |
| `clips[].pitch` | number | semitones — R-WARP-1: its span moved, its length kept |
| `patterns` | object[] | every pattern |
| `patterns[].id` | string |  |
| `patterns[].name` | string |  |
| `patterns[].length` | number | beats |
| `patterns[].clips` | int | clips playing it |
| `patterns[].strip` | string | the strip its first clip plays through |
| `patterns[].instrument` | string | that strip's instrument type (what names its keys) |
| `patterns[].strips` | string[] | R-BROWSE-4: every strip a clip of it plays through, in clip order |
| `patterns[].lastStrip` | string | the strip its newest clip plays through — where the MIDI list drops it off a track |
| `patterns[].notes` | object[] | sorted by (at, pitch) |
| `patterns[].notes[].pitch` | int | 0…127 |
| `patterns[].notes[].at` | number | beats from the pattern's start |
| `patterns[].notes[].length` | number | beats |
| `patterns[].notes[].vel` | int | 1…127 |
| `patterns[].notes[].chance` | number | R-ROLL-11: the odds it sounds in a repetition, 0 … 1 — rolled per clip and repetition by a hash |
| `patterns[].scale` | string | R-ROLL-11: "D dorian" — placed and moved notes land on it; "" none |
| `patterns[].scaleMask` | int | its pitch classes: bit k for pitch class k (C = 0); 0 none |
| `patterns[].expr` | object[] | R-MIDI-3: expression lanes' points, sorted by (lane, at) — linear between, held at the ends |
| `patterns[].expr[].lane` | string | bend (−1 … +1) \| cc0 … cc127 (0 … 1; cc1 the mod wheel) |
| `patterns[].expr[].at` | number | beats from the pattern's start |
| `patterns[].expr[].value` | number | bend −1 … +1, a controller 0 … 1 |
| `markers` | object[] | R-ARR-1: named places in the song, by time |
| `markers[].id` | string | mk_n |
| `markers[].name` | string |  |
| `markers[].note` | string | free text (Interstellar's marker note) |
| `markers[].at` | number | beats |
| `markers[].length` | number | beats — a section; 0 a point |
| `markers[].end` | number | where its section ends: at + length, else the next marker, else the song's end |
| `tempos` | object[] | R-TEMPO-1: the tempo map's changes, by time — before the first, `bpm` |
| `tempos[].id` | string | tp_n |
| `tempos[].at` | number | beats |
| `tempos[].bpm` | number | beats a minute from there |
| `tempos[].ramp` | bool | reached gliding (linearly in beats) from the tempo before |
| `tempos[].seconds` | number | where it is heard (computed through the map) |
| `presets` | object[] | R-PRESET-1: every device preset — yours, then the factory's of a name you have not — by type, then name |
| `presets[].type` | string | the registry type it is for |
| `presets[].name` | string |  |
| `presets[].factory` | bool | shipped with the app (never written or deleted) |
| `templates` | object[] | R-PRESET-2: every song template — yours, then the factory's of a name you have not — by name |
| `templates[].name` | string | what `project new --template` takes |
| `templates[].factory` | bool | shipped with the app (never written or deleted) |
| `automations` | object[] | every automation (R-AUTO-4) |
| `automations[].id` | string | au_n — what a formula names |
| `automations[].name` | string | "<owner> · <parameter>" when made from one |
| `automations[].unit` | string |  |
| `automations[].from` | string | the address it was made from (a hint, not a link) |
| `automations[].min` | number |  |
| `automations[].max` | number |  |
| `automations[].loop` | number | beats (R-AUTO-12): its first `loop` beats repeat — 0: it does not loop |
| `automations[].at` | number | beats: where the repeating starts |
| `automations[].length` | number | beats it repeats for — 0: to the song's end; outside, where a cycle ends |
| `automations[].points` | object[] | sorted by at |
| `automations[].points[].at` | number | beats |
| `automations[].points[].value` | number | in the automation's unit |
| `automations[].points[].shape` | string | linear \| hold \| smooth \| bezier — the segment after it (bezier: both its sides) |
| `automations[].points[].speedIn` | number | bezier (R-AUTO-10): the slope arriving, in the automation's unit per beat |
| `automations[].points[].inflIn` | number | bezier: how far into the segment before it that slope pulls, % (0 < x ≤ 100) |
| `automations[].points[].speedOut` | number | bezier: the slope leaving, unit per beat |
| `automations[].points[].inflOut` | number | bezier: how far into the segment after it, % |
| `automations[].usedBy` | string[] | addresses whose formula reads it |
| `automations[].now` | number | its value at the playhead (`transport.position`) by the engine's own evaluator; follows the transport while playing (R-AUTO-11) *(not in `--stable`)* |
| `bindings` | object[] | every formula (R-AUTO-1/9) |
| `bindings[].address` | string | what it drives |
| `bindings[].formula` | string | as typed, with its leading = |
| `bindings[].reads` | string[] | the automations and addresses it reads |
| `bindings[].ok` | bool | false = INERT: the address's own value plays |
| `bindings[].problem` | string | why it is inert |
| `bindings[].live` | number | its value at the HEARD position (R-MIX-16): the engine's while playing, eval's at the transport when stopped *(not in `--stable`)* |
| `ports` | object[] | logical ports (R-DEV-3) |
| `ports[].id` | string |  |
| `ports[].name` | string |  |
| `ports[].dir` | string | in \| out |
| `ports[].channels` | int |  |
| `matrix` | object | the routing matrix (R-MIX-9), computed once for every face |
| `matrix.columns` | string[] | its columns: every strip some strip may or does reach (processing order), "master", the output ports — every route and key has a cell |
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
| `recents[].recovery` | bool | an autosave newer than it: unsaved edits to recover (R-SAFE-2) |
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
| `settings.auditionLevel` | number | dB: a sample previewed from the browser (R-EDM-9) |
| `settings.newBpm` | number | a new song's tempo without --bpm |
| `settings.newSig` | string | a new song's meter without --sig |
| `settings.reducedMotion` | bool | the UI's tweens collapse (with the OS's own setting) |
| `settings.showIds` | bool | every object's id drawn beside its name (R-UI-11) |
| `settings.autosave` | int | seconds between autosaves of an unsaved song; 0 = off (R-SAFE-1) |
| `settings.driver` | string | pulse \| pipewire: how the clock device is reached (R-DEV-9) |
| `settings.recordQuantize` | string | off \| 1/4 \| 1/8 \| 1/16 \| 1/32: where a recorded note's start lands (R-MIDI-2) |
| `settings.countIn` | int | bars of click before a recording, 0 … 4 (R-MIDI-2) |
| `settings.midiInputs` | string | all \| none \| MIDI port names, comma-separated: the keyboards Solaris listens to (R-MIDI-2) |
| `armed` | string | the strip the MIDI keyboard plays ('' none) — the session's, never saved (R-MIDI-2) |
| `mapLearning` | string | R-CTRL-1: the address a `map learn` waits to bind ('' none) — the session's |
| `surface` | object | R-CTRL-2: the control surface as it is now — the session's |
| `surface.protocol` | string | none \| mcu \| hui |
| `surface.connected` | bool | its port is open, in and out |
| `surface.why` | string | why it is not connected ('' when it is, or none is set) |
| `surface.bank` | int | the first of its eight strips, 0-based, in processing order |
| `midiDevices` | object[] | this machine's MIDI inputs, as `midi devices` last listed them (R-MIDI-2) |
| `midiDevices[].id` | string | what `settings set midiInputs=` names |
| `midiDevices[].name` | string |  |
| `midiDevices[].listening` | bool | Solaris is subscribed to it |
| `midiMessages` | int | messages the keyboard sent since start — an activity light *(not in `--stable`)* |
| `settings.keys` | object[] | every keyboard action (R-SET-4), in the sheet's order — a pressed combo is looked up here |
| `settings.keys[].id` | string | transport.play … — set with `settings set key.<id>=<combos>` |
| `settings.keys[].area` | string | Transport \| Song \| Lanes \| Piano roll (the first three are one keyboard) |
| `settings.keys[].label` | string |  |
| `settings.keys[].keys` | string[] | its combos on this machine, canonical (Ctrl+Alt+Shift+<key>); empty = no key |
| `settings.keys[].defaults` | string[] | what `default` gives back |
| `settings.mappings` | object[] | R-CTRL-1: the MIDI controls and what they move (`map add\|remove\|learn`) |
| `settings.mappings[].cc` | string | `<n>` (any channel) or `<ch>/<n>` |
| `settings.mappings[].address` | string | the number it moves |
| `settings.mappings[].inert` | bool | the open song has no such address |
| `settings.surface` | string | R-CTRL-2: none \| mcu (Mackie Control) \| hui — `settings set surface=…` |
| `settings.surfacePort` | string | … the MIDI port it is on, in and out (the keyboard never listens there) |
| `settings.threads` | string | R-PERF-2: auto (the cores but one, at most 8) \| 1 … 16 — the engine's threads, live and in a render |
| `devices` | object[] | from the last `devices list` |
| `devices[].id` | string | what `settings set output=` takes |
| `devices[].name` | string |  |
| `devices[].dir` | string | out \| in |
| `devices[].channels` | int |  |
| `devices[].rate` | int |  |
| `transport` | object | play, position, loop (R-TIME-4) |
| `transport.playing` | bool |  |
| `transport.position` | number | beats — what is heard *(not in `--stable`)* |
| `transport.tempo` | number | R-TEMPO-1: the tempo where the time bar is (bpm) *(not in `--stable`)* |
| `transport.tempoId` | string | R-TEMPO-1: the change in effect there — tp_n, or '' before the first (the song's bpm) *(not in `--stable`)* |
| `transport.loopFrom` | number | beats |
| `transport.loopTo` | number | beats; equal to loopFrom = no loop |
| `transport.punchFrom` | number | R-REC-1: beats — a recording keeps only from here … (the session's) |
| `transport.punchTo` | number | … to here; equal to punchFrom = all of it |
| `transport.cue` | number | beats: where the time bar was last put (a seek, or a play's --from / --clip; never a plain play) — `transport stop --return` goes back there (R-TIME-7) |
| `transport.loopClip` | string | the clip the loop follows (`transport play --clip`, the piano roll's play); '' = the song's own loop |
| `transport.latencyMs` | number | the clock device's *(not in `--stable`)* |
| `transport.device` | string | the clock device ('' = the system default) |
| `transport.masterPeak` | number[] | L/R peaks of the last played block *(not in `--stable`)* |
| `transport.masterRms` | number[] | the master's RMS over 300 ms, L/R (R-METER-2) *(not in `--stable`)* |
| `transport.masterHold` | number[] | the master's held peak, L/R *(not in `--stable`)* |
| `transport.masterClip` | bool | the master reached full scale since `meters clear` *(not in `--stable`)* |
| `transport.driver` | string | what the clock device plays through now: pulse \| pipewire ('' stopped) — R-DEV-9 *(not in `--stable`)* |
| `transport.recording` | bool | `transport record` is keeping the armed strip's keyboard (R-MIDI-2) |
| `transport.countingIn` | bool | … and its count-in still clicks *(not in `--stable`)* |
| `transport.recorded` | int | notes struck that will land (a count-in's are not) *(not in `--stable`)* |
| `transport.xruns` | int | underruns since play (−1: this driver cannot count them) — R-DEV-5 *(not in `--stable`)* |
| `transport.cpu` | number | R-PERF-1: the audio thread's load — a block's work over its duration, smoothed (0 stopped) *(not in `--stable`)* |
| `transport.cpuPeak` | number | … its worst single block in the last second, unsmoothed (over 1: a block that missed its deadline) *(not in `--stable`)* |
| `transport.devices` | object[] | R-DEV-5: while playing, every device the song plays through — the clock first, then each FOLLOWER (a device a port is mapped to, held to the clock, R-DEV-1/2), and an armed input on another device; none stopped *(not in `--stable`)* |
| `transport.devices[].id` | string | the device as the settings name it ('' = the system default) *(not in `--stable`)* |
| `transport.devices[].role` | string | clock \| follower \| input (an input on another device, held to the clock — R-DEV-1/2) *(not in `--stable`)* |
| `transport.devices[].latencyMs` | number | from a frame rendered to its being heard — the same on every device that plays (R-DEV-4) *(not in `--stable`)* |
| `transport.devices[].driftPpm` | number | a follower's or an input's crystal against the clock's, ppm: + it runs fast (0 the clock) *(not in `--stable`)* |
| `transport.devices[].xruns` | int | its underruns (−1: the driver cannot count them) *(not in `--stable`)* |
| `transport.devices[].starved` | int | a follower's blocks with too little to play (the clock stalled; it primes again) *(not in `--stable`)* |
| `transport.devices[].dropped` | int | the clock's blocks a follower had no room for (its own device stalled) *(not in `--stable`)* |
| `transport.devices[].problem` | string | why it is silent ('' it plays) — a follower that would not open *(not in `--stable`)* |
| `loudness` | object | the master as measured while playing, kept after a stop (R-METER-1, ITU-R BS.1770-4) *(not in `--stable`)* |
| `loudness.momentary` | number | LUFS over the last 400 ms; −144 = nothing measured *(not in `--stable`)* |
| `loudness.shortTerm` | number | LUFS over the last 3 s *(not in `--stable`)* |
| `loudness.integrated` | number | LUFS, gated (−70 absolute, −10 LU relative), since play or `meters clear` *(not in `--stable`)* |
| `loudness.truePeak` | number | dBTP (4× oversampled), the most since play or `meters clear` *(not in `--stable`)* |
| `loudness.correlation` | number | stereo correlation over 300 ms, −1 … +1 (0: nothing to compare) *(not in `--stable`)* |
| `loudness.spectrum` | number[] | 61 bands of 1/6 octave, dB re a full-scale sine: band b's centre is 1000·2^((b−34)/6) Hz *(not in `--stable`)* |
| `audition` | object | a sample heard from the browser, outside the song (R-EDM-9) |
| `audition.file` | string | the file previewed ('' = none) |
| `audition.playing` | bool |  |
| `audition.progress` | number | 0 … 1 of the file handed to the device *(not in `--stable`)* |
| `deviceTypes` | object[] | the DSP registry: every instrument and effect a strip can host (R-BROWSE-1) |
| `deviceTypes[].name` | string | the registry type, what `device add --type` and `--instrument` take |
| `deviceTypes[].label` | string |  |
| `deviceTypes[].kind` | string | instrument \| effect |
| `deviceTypes[].noteNames` | object[] | a kit's keys, named (REQ-device-6) |
| `deviceTypes[].takesSample` | bool | it plays a recorded sound: give it one with --sample / <dv>.sample (R-EDM-8) |
| `deviceTypes[].takesDefinition` | bool | R-INST-9: … an .sfz naming its sounds, given the same way |
| `deviceTypes[].noteNames[].note` | int |  |
| `deviceTypes[].noteNames[].name` | string |  |
| `chords` | object[] | R-ROLL-11: the qualities `note add --chord <root><name>` takes |
| `chords[].name` | string | "" (major), m, 7, maj7, m7 … |
| `chords[].label` | string |  |
| `scales` | object[] | R-ROLL-11: the scales `pattern scale <pt> <key> <name>` takes |
| `scales[].name` | string |  |
| `scales[].label` | string |  |
| `grooves` | object[] | R-ROLL-11: the templates `pattern quantize --groove <name>` takes |
| `grooves[].name` | string |  |
| `grooves[].label` | string |  |
| `browser` | object | the folder last browsed |
| `browser.path` | string |  |
| `browser.entries` | object[] | folders first, then by name |
| `browser.entries[].name` | string |  |
| `browser.entries[].path` | string |  |
| `browser.entries[].kind` | string | dir \| audio \| song \| other |
| `browser.search` | string | R-PRESET-3: what the entries were searched for ('' a folder listed; `favourites`) |
| `library` | object[] | R-PRESET-3: every tagged sound in the machine's library, by path |
| `library[].path` | string | absolute |
| `library[].tags` | string[] | lower case, sorted — `favourite` not among them |
| `library[].favourite` | bool |  |
| `plugins` | object[] | R-VST-6: the VST3 plugins the last scan found (the machine's) |
| `plugins[].id` | string | the class's UID, 32 hex digits — what `device add --plugin` takes |
| `plugins[].name` | string |  |
| `plugins[].vendor` | string |  |
| `plugins[].kind` | string | instrument \| effect |
| `plugins[].path` | string | its bundle |
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
| `osc1.width` | 0.0 … 50.0 ct | 0.0 ct |
| `osc2.wave` | sine · saw · square · triangle | square |
| `osc2.octave` | -3.0 … 3.0 oct | -1.0 oct |
| `osc2.semi` | -12.0 … 12.0 st | 0.0 st |
| `osc2.fine` | -100.0 … 100.0 ct | 7.0 ct |
| `osc2.level` | 0.0 … 1.0 | 0.5 |
| `osc2.voices` | 1.0 … 5.0 | 1.0 |
| `osc2.detune` | 0.0 … 100.0 ct | 15.0 ct |
| `osc2.width` | 0.0 … 50.0 ct | 0.0 ct |
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
| `voice.mode` | poly · mono · legato | poly |
| `voice.glide` | 0.0 … 2000.0 ms | 0.0 ms |
| `voice.bend` | 0.0 … 24.0 st | 2.0 st |
| `vibrato.depth` | 0.0 … 2.0 st | 0.5 st |
| `vibrato.rate` | 0.1 … 20.0 Hz | 5.5 Hz |

### `wavetable` — Wavetable (instrument)

Two wavetable oscillators — six built-in tables, or a sound of 2048-point cycles — moved through by a position and the filter envelope; band-limited at every pitch; a resonant filter, two envelopes; 16 voices.

| parameter | range | default |
|---|---|---|
| `osc1.table` | basic · pulse · harmonics · formant · sync · fold · sample | basic |
| `osc1.position` | 0.0 … 1.0 | 0.66 |
| `osc1.posenv` | -1.0 … 1.0 | 0.0 |
| `osc1.octave` | -3.0 … 3.0 oct | 0.0 oct |
| `osc1.semi` | -12.0 … 12.0 st | 0.0 st |
| `osc1.fine` | -100.0 … 100.0 ct | 0.0 ct |
| `osc1.level` | 0.0 … 1.0 | 0.8 |
| `osc1.voices` | 1.0 … 5.0 | 3.0 |
| `osc1.detune` | 0.0 … 100.0 ct | 12.0 ct |
| `osc1.width` | 0.0 … 50.0 ct | 10.0 ct |
| `osc2.table` | basic · pulse · harmonics · formant · sync · fold · sample | harmonics |
| `osc2.position` | 0.0 … 1.0 | 0.3 |
| `osc2.posenv` | -1.0 … 1.0 | 0.0 |
| `osc2.octave` | -3.0 … 3.0 oct | -1.0 oct |
| `osc2.semi` | -12.0 … 12.0 st | 0.0 st |
| `osc2.fine` | -100.0 … 100.0 ct | 0.0 ct |
| `osc2.level` | 0.0 … 1.0 | 0.0 |
| `osc2.voices` | 1.0 … 5.0 | 1.0 |
| `osc2.detune` | 0.0 … 100.0 ct | 15.0 ct |
| `osc2.width` | 0.0 … 50.0 ct | 0.0 ct |
| `noise` | 0.0 … 1.0 | 0.0 |
| `filter.mode` | lowpass · bandpass · highpass · notch | lowpass |
| `filter.cutoff` | 20.0 … 20000.0 Hz | 6000.0 Hz |
| `filter.res` | 0.0 … 1.0 | 0.25 |
| `filter.env` | -8.0 … 8.0 oct | 1.0 oct |
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
| `voice.mode` | poly · mono · legato | poly |
| `voice.glide` | 0.0 … 2000.0 ms | 0.0 ms |
| `voice.bend` | 0.0 … 24.0 st | 2.0 st |
| `vibrato.depth` | 0.0 … 2.0 st | 0.5 st |
| `vibrato.rate` | 0.1 … 20.0 Hz | 5.5 Hz |

### `fm` — FM Synth (instrument)

Four sine operators by phase modulation — eight algorithms, operator 4 fed back, an envelope each; 16 voices.

| parameter | range | default |
|---|---|---|
| `algorithm` | 1 · 2 · 3 · 4 · 5 · 6 · 7 · 8 | 1 |
| `feedback` | 0.0 … 1.0 | 0.0 |
| `op1.ratio` | 0.5 … 16.0 x | 1.0 x |
| `op1.fine` | -100.0 … 100.0 ct | 0.0 ct |
| `op1.level` | 0.0 … 1.0 | 1.0 |
| `op1.attack` | 0.0 … 10000.0 ms | 2.0 ms |
| `op1.decay` | 0.0 … 10000.0 ms | 400.0 ms |
| `op1.sustain` | 0.0 … 1.0 | 0.6 |
| `op1.release` | 0.0 … 10000.0 ms | 300.0 ms |
| `op1.velocity` | 0.0 … 1.0 | 0.7 |
| `op2.ratio` | 0.5 … 16.0 x | 1.0 x |
| `op2.fine` | -100.0 … 100.0 ct | 0.0 ct |
| `op2.level` | 0.0 … 1.0 | 0.6 |
| `op2.attack` | 0.0 … 10000.0 ms | 2.0 ms |
| `op2.decay` | 0.0 … 10000.0 ms | 600.0 ms |
| `op2.sustain` | 0.0 … 1.0 | 0.3 |
| `op2.release` | 0.0 … 10000.0 ms | 300.0 ms |
| `op2.velocity` | 0.0 … 1.0 | 0.7 |
| `op3.ratio` | 0.5 … 16.0 x | 2.0 x |
| `op3.fine` | -100.0 … 100.0 ct | 0.0 ct |
| `op3.level` | 0.0 … 1.0 | 0.3 |
| `op3.attack` | 0.0 … 10000.0 ms | 2.0 ms |
| `op3.decay` | 0.0 … 10000.0 ms | 600.0 ms |
| `op3.sustain` | 0.0 … 1.0 | 0.3 |
| `op3.release` | 0.0 … 10000.0 ms | 300.0 ms |
| `op3.velocity` | 0.0 … 1.0 | 0.7 |
| `op4.ratio` | 0.5 … 16.0 x | 3.5 x |
| `op4.fine` | -100.0 … 100.0 ct | 0.0 ct |
| `op4.level` | 0.0 … 1.0 | 0.2 |
| `op4.attack` | 0.0 … 10000.0 ms | 2.0 ms |
| `op4.decay` | 0.0 … 10000.0 ms | 600.0 ms |
| `op4.sustain` | 0.0 … 1.0 | 0.3 |
| `op4.release` | 0.0 … 10000.0 ms | 300.0 ms |
| `op4.velocity` | 0.0 … 1.0 | 0.7 |
| `volume` | -60.0 … 6.0 dB | -12.0 dB |
| `voice.bend` | 0.0 … 24.0 st | 2.0 st |
| `vibrato.depth` | 0.0 … 2.0 st | 0.5 st |
| `vibrato.rate` | 0.1 … 20.0 Hz | 5.5 Hz |

### `piano` — Piano (instrument)

A physical piano: felt hammers on stiff, inharmonic strings through a shared bridge and soundboard; sustain and soft pedals; 16 voices, a held key never stolen while a released one rings.

| parameter | range | default |
|---|---|---|
| `hammer.hardness` | 0.5 … 1.6 x | 1.0 x |
| `hammer.curve` | 1.5 … 3.5 | 2.5 |
| `hammer.hysteresis` | 0.0 … 0.6 | 0.2 |
| `hammer.relax` | 0.0 … 2.0 x | 1.0 x |
| `strings.decay` | 0.3 … 2.5 x | 1.0 x |
| `strings.brightness` | 0.25 … 4.0 x | 1.0 x |
| `strings.inharmonicity` | 0.0 … 3.0 x | 1.0 x |
| `strings.detune` | 0.0 … 3.0 cents | 0.6 cents |
| `strings.growl` | 0.0 … 0.03 | 0.008 |
| `strings.glide` | 0.0 … 4.0 x | 1.0 x |
| `strings.shimmer` | 0.0 … 300.0 | 90.0 |
| `strings.sing` | 0.0 … 2.5 x | 1.0 x |
| `body.mix` | 0.0 … 1.0 | 0.5 |
| `body.resonance` | 0.0 … 2.0 | 0.5 |
| `pedal.sustain` | off · on | off |
| `pedal.soft` | off · on | off |
| `drive` | 0.05 … 0.5 | 0.22 |
| `volume` | -60.0 … 6.0 dB | 0.0 dB |

### `drums` — Drum Machine (instrument)

Twelve synthesized pads on the GM drum notes (36 kick … 56 cowbell, a crash 49 and a ride 51); the closed hat chokes the open hat.

Pads — a pitch by name: `kick` 36 · `rim` 37 · `snare` 38 · `clap` 39 · `low-tom` 41 · `closed-hat` 42 · `mid-tom` 45 · `open-hat` 46 · `high-tom` 48 · `crash` 49 · `ride` 51 · `cowbell` 56

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
| `crash.tune` | -12.0 … 12.0 st | 0.0 st |
| `crash.decay` | 10.0 … 4000.0 ms | 1800.0 ms |
| `crash.tone` | 0.0 … 1.0 | 0.5 |
| `crash.level` | -60.0 … 6.0 dB | 0.0 dB |
| `crash.pan` | -1.0 … 1.0 | 0.0 |
| `ride.tune` | -12.0 … 12.0 st | 0.0 st |
| `ride.decay` | 10.0 … 4000.0 ms | 1200.0 ms |
| `ride.tone` | 0.0 … 1.0 | 0.5 |
| `ride.level` | -60.0 … 6.0 dB | 0.0 dB |
| `ride.pan` | -1.0 … 1.0 | 0.0 |
| `volume` | -60.0 … 6.0 dB | -6.0 dB |

### `808` — 808 (instrument)

A tuned bass drum: the note's pitch punched down to at each attack, gliding between held notes; a long decay, a click, drive and a tone; one voice.

| parameter | range | default |
|---|---|---|
| `punch` | 0.0 … 36.0 st | 12.0 st |
| `punch.time` | 1.0 … 200.0 ms | 40.0 ms |
| `glide` | 0.0 … 1000.0 ms | 60.0 ms |
| `amp.attack` | 0.0 … 50.0 ms | 1.0 ms |
| `amp.decay` | 20.0 … 8000.0 ms | 900.0 ms |
| `amp.release` | 1.0 … 2000.0 ms | 60.0 ms |
| `click` | 0.0 … 1.0 | 0.2 |
| `drive` | 0.0 … 1.0 | 0.25 |
| `tone` | 200.0 … 20000.0 Hz | 4000.0 Hz |
| `volume` | -60.0 … 12.0 dB | -6.0 dB |
| `velocity` | 0.0 … 1.0 | 0.4 |
| `voice.bend` | 0.0 … 24.0 st | 2.0 st |

### `granular` — Granular (instrument)

A recorded sound as a cloud of short grains — their size, density and place (scanned, sprayed), pitched by the note, sprayed in pitch and pan; an envelope; 16 voices.

| parameter | range | default |
|---|---|---|
| `grain.size` | 5.0 … 500.0 ms | 80.0 ms |
| `grain.density` | 1.0 … 200.0 /s | 30.0 /s |
| `grain.position` | 0.0 … 1.0 | 0.3 |
| `grain.scan` | -1.0 … 1.0 /s | 0.0 /s |
| `grain.spray` | 0.0 … 1.0 | 0.05 |
| `grain.pitch` | -24.0 … 24.0 st | 0.0 st |
| `grain.pspray` | 0.0 … 100.0 ct | 5.0 ct |
| `grain.spread` | 0.0 … 1.0 | 0.5 |
| `root` | 0.0 … 127.0 | 60.0 |
| `amp.attack` | 0.0 … 10000.0 ms | 50.0 ms |
| `amp.decay` | 0.0 … 10000.0 ms | 0.0 ms |
| `amp.sustain` | 0.0 … 1.0 | 1.0 |
| `amp.release` | 0.0 … 10000.0 ms | 300.0 ms |
| `volume` | -60.0 … 12.0 dB | -6.0 dB |
| `velocity` | 0.0 … 1.0 | 0.5 |

### `multisample` — Multi-sample (instrument)

A recorded instrument read from an SFZ file: its sounds by key and velocity, in turn (round robin) or by chance, looped or one-shot, choking each other; an envelope each; 32 voices.

| parameter | range | default |
|---|---|---|
| `volume` | -60.0 … 12.0 dB | 0.0 dB |
| `velocity` | 0.0 … 1.0 | 1.0 |
| `voice.bend` | 0.0 … 24.0 st | 2.0 st |

### `sampler` — Sampler (instrument)

Plays a recorded sound: pitched by the note (chromatic), as recorded (one-shot), or as a kit of its span's equal slices (slices); a span, reverse, an ADSR.

| parameter | range | default |
|---|---|---|
| `mode` | chromatic · one-shot · slices | chromatic |
| `root` | 0.0 … 127.0 | 60.0 |
| `start` | 0.0 … 1.0 | 0.0 |
| `end` | 0.0 … 1.0 | 1.0 |
| `reverse` | off · on | off |
| `attack` | 0.0 … 2000.0 ms | 0.0 ms |
| `decay` | 0.0 … 2000.0 ms | 0.0 ms |
| `sustain` | 0.0 … 1.0 | 1.0 |
| `release` | 1.0 … 5000.0 ms | 30.0 ms |
| `level` | -40.0 … 12.0 dB | 0.0 dB |
| `velocity` | 0.0 … 1.0 | 1.0 |
| `bend` | 0.0 … 24.0 st | 2.0 st |
| `slices` | 1.0 … 64.0 | 8.0 |

### `compressor` — Compressor (effect)

Feed-forward peak compressor; with Sidechain on it listens to another track (its key).

| parameter | range | default |
|---|---|---|
| `threshold` | -60.0 … 0.0 dB | -18.0 dB |
| `ratio` | 1.0 … 20.0 :1 | 4.0 :1 |
| `attack` | 0.1 … 200.0 ms | 5.0 ms |
| `release` | 5.0 … 2000.0 ms | 100.0 ms |
| `makeup` | 0.0 … 24.0 dB | 0.0 dB |
| `sidechain` | off · on | off |

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

### `gate` — Gate (effect)

A gate and a downward expander: below its threshold turned down by the ratio, held open before it closes; keyed by another track if asked.

| parameter | range | default |
|---|---|---|
| `threshold` | -80.0 … 0.0 dB | -40.0 dB |
| `ratio` | 1.0 … 100.0 :1 | 100.0 :1 |
| `range` | 0.0 … 100.0 dB | 80.0 dB |
| `attack` | 0.01 … 100.0 ms | 1.0 ms |
| `hold` | 0.0 … 2000.0 ms | 20.0 ms |
| `release` | 1.0 … 5000.0 ms | 100.0 ms |
| `sidechain` | off · on | off |

### `phaser` — Phaser (effect)

Notches swept by an LFO: first-order all-passes fed back, mixed with the dry sound.

| parameter | range | default |
|---|---|---|
| `rate` | 0.01 … 10.0 Hz | 0.5 Hz |
| `center` | 100.0 … 8000.0 Hz | 800.0 Hz |
| `depth` | 0.0 … 4.0 oct | 2.0 oct |
| `stages` | 2.0 … 12.0 | 4.0 |
| `feedback` | -0.9 … 0.9 | 0.3 |
| `stereo` | 0.0 … 180.0 deg | 90.0 deg |
| `mix` | 0.0 … 1.0 | 0.5 |

### `saturator` — Saturator (effect)

A curve driven — soft, hard, tube or fold — four times oversampled, its latency reported.

| parameter | range | default |
|---|---|---|
| `drive` | 0.0 … 36.0 dB | 6.0 dB |
| `curve` | soft · hard · tube · fold | soft |
| `mix` | 0.0 … 1.0 | 1.0 |
| `output` | -24.0 … 12.0 dB | 0.0 dB |

### `bitcrusher` — Bitcrusher (effect)

Fewer bits and a lower rate: each sample rounded to a coarser grid, held for longer.

| parameter | range | default |
|---|---|---|
| `bits` | 1.0 … 24.0 | 8.0 |
| `rate` | 100.0 … 96000.0 Hz | 96000.0 Hz |
| `mix` | 0.0 … 1.0 | 1.0 |
| `output` | -24.0 … 12.0 dB | 0.0 dB |

### `deesser` — De-esser (effect)

Sibilance held down: a high-pass hears it, its level held at the threshold — the band alone turned down (split) or the whole sound (wide).

| parameter | range | default |
|---|---|---|
| `frequency` | 2000.0 … 16000.0 Hz | 6000.0 Hz |
| `threshold` | -60.0 … 0.0 dB | -30.0 dB |
| `range` | 0.0 … 24.0 dB | 12.0 dB |
| `mode` | split · wide | split |

### `transient` — Transient Shaper (effect)

More or less attack, more or less sustain, whatever the level: a fast envelope against two slow ones.

| parameter | range | default |
|---|---|---|
| `attack` | -100.0 … 100.0 % | 0.0 % |
| `sustain` | -100.0 … 100.0 % | 0.0 % |
| `output` | -24.0 … 24.0 dB | 0.0 dB |

### `multiband` — Multiband (effect)

Three bands split by Linkwitz-Riley crossovers, each compressed alone; flat when nothing is.

| parameter | range | default |
|---|---|---|
| `low.split` | 20.0 … 1000.0 Hz | 200.0 Hz |
| `high.split` | 500.0 … 12000.0 Hz | 2500.0 Hz |
| `low.threshold` | -60.0 … 0.0 dB | -20.0 dB |
| `low.ratio` | 1.0 … 20.0 :1 | 3.0 :1 |
| `low.gain` | -24.0 … 24.0 dB | 0.0 dB |
| `mid.threshold` | -60.0 … 0.0 dB | -20.0 dB |
| `mid.ratio` | 1.0 … 20.0 :1 | 3.0 :1 |
| `mid.gain` | -24.0 … 24.0 dB | 0.0 dB |
| `high.threshold` | -60.0 … 0.0 dB | -20.0 dB |
| `high.ratio` | 1.0 … 20.0 :1 | 3.0 :1 |
| `high.gain` | -24.0 … 24.0 dB | 0.0 dB |
| `attack` | 0.1 … 200.0 ms | 10.0 ms |
| `release` | 5.0 … 2000.0 ms | 150.0 ms |
| `output` | -24.0 … 24.0 dB | 0.0 dB |

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

### `limiter` — Limiter (effect)

Brickwall lookahead limiter: the output never exceeds the ceiling.

| parameter | range | default |
|---|---|---|
| `gain` | 0.0 … 24.0 dB | 0.0 dB |
| `ceiling` | -24.0 … 0.0 dB | -0.3 dB |
| `release` | 1.0 … 1000.0 ms | 60.0 ms |
| `lookahead` | 0.0 … 10.0 ms | 2.0 ms |
| `truePeak` | off · on | off |
