# Solaris — Requirements (intent tier)

**What Solaris is asked to do, why, and what happened to each request.** `R-<AREA>-<n>`; status in
the section heading. Read before any code; conflict-check before accepting a new one
(`arstro.rule` §2). As-built is [`docs/requirements.md`](docs/requirements.md) (`DR-`).

> **This is the SECOND specification.** The first (`SR-<AREA>-<n>`, phases P1–P10) is archived
> whole in [`docs/history/requirements-v1.md`](docs/history/requirements-v1.md). Its ids are retired,
> not reused; each R- line below that carries one forward says `(was SR-…)`. It was replaced for
> three reasons, settled in [`docs/discussion.md`](docs/discussion.md) on 2026-10-08:
>
> 1. **Its mixer was Cubase's** (a track is a lane and a strip at once), which fights loop-based
>    music. The user's model: *every sample and instrument has its own strip; mixers are pages the
>    user adds; the first holds the sources, the second receives them and feeds master; a matrix
>    shows every route; any strip can reach any of several audio devices.*
> 2. **Its versioning was project-level Nebula branching**, the model Interstellar already withdrew
>    (Interstellar R-VER-6). Solaris versions follow Interstellar's R-VER instead, so the suite has
>    one versioning model.
> 3. **Its UI was a touch-first tablet brief** with its own blue accent. The user asked for *"the
>    whole app to refer the design of cosmo and interstellar"*.
>
> And one rule was added by the same request, and is the reason `R-DSP` comes first: *"make sure
> all the core of those instruments and basic filters is in dsp"*.

**Status legend:** `📋 SPECIFIED` · `🚧 IN PROGRESS` · `✅ IMPLEMENTED` · `⚠️ REOPENED (D-n)` ·
`❌ WITHDRAWN`.

---

## Global rules — 📋 SPECIFIED

- **R-G-1 The design law binds this app by reference** (`.claude/skills/arstro.design.rule`). No
  value from it is restated here.
- **R-G-2 Cosmo is the reference implementation and the divergence is ONE token**, exactly as
  Interstellar R-UI-2: radii, fonts, spacing and every neutral colour are cosmo's, *aliased*; only
  the accent is Solaris's (R-UI-2).
- **R-G-3 One authority per fact.** A strip's gain exists once; a clip's position exists once; a
  pattern's notes exist once however many clips play them.
- **R-G-4 Nothing is reachable only by clicking.** Every behaviour is a `Command`, observable in the
  `AppModel` or the `Event` stream, assertable from a shell with no display. A drag-and-drop is a
  gesture over a command, never a path the CLI cannot take.

---

## R-DSP — the sound is the DSP library's — ✅ IMPLEMENTED (DR-DSP-1, DR-ENG-1: Solaris hosts the registry's devices and implements none)

The user's rule (2026-10-08): *"make sure all the core of those instruments and basic filters is in
dsp."*

- **R-DSP-1 Every instrument and every effect is a module of `core/DigitalSignalProcessing`**, built,
  documented (`## Math`) and tested there under `arstro.dsp.implement`. Solaris contains **no**
  oscillator, filter, envelope, dynamics, delay or reverb math. Its engine *hosts* devices; it does
  not *implement* them. A DSP routine found in `apps/solaris/` is a defect.
- **R-DSP-2 One device interface and one registry, in the DSP library.** Every instrument and
  effect is reachable through a uniform `Device` (process a block in place; notes for instruments;
  parameters by index) and described by a registry entry: its type name, kind
  (instrument | effect), and each parameter's name, unit, range, default and — for a choice — its
  values. Solaris's address space, its `.slp` keys, its API document and its UI's knobs are all
  **read from that registry**, never restated (was SR-RACK-2, SR-PARAM-1).
- **R-DSP-3 Parameters are stored in engineering units** (Hz, dB, ms, semitones, 0–1 for a mix) and
  validated against the registry's range. (**Changes SR-PARAM-2**, which normalised to 0..1: a
  `.slp` is read by people — R-NFR-5 — and `filter.cutoff=1200` is legible where `0.43` is not.
  Interstellar's address space stores engine units for the same reason.)
- **R-DSP-4 Rendering is deterministic.** A device's output is a pure function of its parameters,
  its note events and its input; noise is seeded from data, never from a clock (was SR-NFR-1).
- **R-DSP-5 Splitting at note events is the host's job.** A device renders whole blocks; the engine
  splits a block at every note event so a note starts on its exact sample (was SR-TIME-3).

## R-INST — instruments — ✅ IMPLEMENTED (DR-INST-1, DR-INST-2; played through Solaris's engine, DR-ENG-1) — 5 ✅ (the piano, DR-INST-5) — 1 amended ✅ (stereo unison, DR-INST-1) — 2 amended ✅ (crash and ride, DR-INST-2)

The user's request (2026-10-08): *"make me some basic instruments: Drum Machine, Basic Synth with 2
OSC."*

- **R-INST-1 Basic Synth — two oscillators, subtractive.** Polyphonic. Per oscillator: waveform
  (sine, saw, square, triangle), octave, semitone, fine tune, level, unison voices and detune. A
  noise source. One resonant filter after the oscillators — low-pass, band-pass or high-pass, cutoff,
  resonance, envelope amount, key tracking — with its own ADSR. An amplitude ADSR after the filter
  (oscillators → filter → amplifier: the classic order). Velocity scales level. Built on the DSP
  library's `Oscillator`, `ADSREnvelope` and a state-variable filter (was SR-INST-1).
  (**AMENDED (user request, 2026-10-15: *"make an edm song, edm require more advance sound"*):** unison is
  STEREO — per oscillator a width (the library oscillator's per-channel detune spread, in cents), and unison
  voices start at spread phases (deterministic, per voice and channel) instead of all at zero, so a supersaw is
  wide and its attack does not flam; one voice stays mono-identical, as before.)
- **R-INST-2 Drum Machine — synthesized kit, played by notes.** Pads on the General MIDI drum
  notes: kick 36, rim 37, snare 38, clap 39, low tom 41, closed hat 42, mid tom 45, open hat 46,
  high tom 48, cowbell 56. Each pad has tune, decay, tone, level and pan. **The closed hat chokes the
  open hat.** Every pad is synthesized from DSP primitives (oscillators, seeded noise, filters,
  exponential decays) with its model written in its README — no sample files.
  (**AMENDED (user request, 2026-10-15, the EDM song):** two cymbals on their GM notes — **crash 49**, a long
  bright wash for the downbeat of a drop, and **ride 51**, a shorter pinging bell with a wash — each with tune,
  decay, tone, level and pan like every pad, synthesized as the hats are, with their models in the README.)
- **R-INST-3 An instrument is played by note clips** (R-CLIP-2). A drum pattern is a note clip
  whose notes are pads; a step sequencer is a *view* of that clip (R-UI), not a second data model.
- **R-INST-4 Reserved:** a sampler (play a sample chromatically) (was SR-INST-2), VST3 hosting (was
  SR-RACK-3). (**AMENDED (user request, 2026-10-09):** VST3 is now R-VST — our instruments ship as
  plugins first, hosting follows; the sampler is R-EDM-8.)
- **R-INST-5 Piano — the DSP library's physical piano, played by notes** (user request, 2026-10-15: *"if
  the instrument is not enough, make that instrument"* — the song of R-SVC-10 is for the piano). The
  library's physical model (`PianoVoice`: felt hammer, stiff strings, bridge, soundboard — REQ-piano-*)
  becomes a library instrument with a registry entry, so Solaris, its plugins and the CLI have it with no
  Solaris code (R-DSP-2). Its voicing controls (hammer, strings, body, output) and the sustain pedal are
  registry parameters. A held note is never stolen while a released one is still ringing: a figure over a
  held bass (an Alberti bass, an arpeggio) keeps its bass. Fast enough for one track with headroom: the
  registry's real-time budget holds for a piano chord.

## R-FX — effects — ✅ IMPLEMENTED (1–4 DR-FX-1; 5 the rack: `device add/remove/move`, DR-SVC-1)

The user's request (2026-10-08): *"and some basic filters: Compressor, EQ, Reverb, …"*

- **R-FX-1 The basic set:** Compressor, EQ, Reverb, Delay, Chorus, Drive, Filter. Each is a DSP
  library device (R-DSP-1).
- **R-FX-2 EQ is parametric:** a low cut, a low shelf, three peaking bands, a high shelf and a high
  cut, each with its own frequency, gain and Q where it has them, each switchable. Built from one
  biquad primitive (RBJ cookbook), not seven hand-written filters.
- **R-FX-3 Filter** is the synth's state-variable filter as an effect (mode, cutoff, resonance, mix).
- **R-FX-4** Compressor (threshold, ratio, attack, release, makeup), Reverb (pre-delay, decay, low
  cut, high cut, width, mix), Delay (time, feedback, tone, mix), Chorus (rate, depth, delay, mix) and
  Drive (drive, tone, level) wrap the DSP library's existing `Compressor`, `Reverb`, `Repeater`,
  `Chorus` and `Overdrive` — reused, not rewritten. (**Honest status (audit, 2026-10-09):** the Reverb
  is built with `size` (the network's delay spread), decay, low cut, high cut, width and mix — its
  PRE-DELAY is not built; it stays wanted, a DSP-library task first (R-DSP-1).)
- **R-FX-5 A strip hosts an ordered rack** of devices: add, remove, reorder, bypass per device
  (was SR-RACK-1). An instrument strip's first device is its instrument.

---

## R-MIX — the mixer: every source has its own strip — ✅ IMPLEMENTED (1–11 headless: DR-ENG-1, DR-MIX-2/4/7/8/9/10/11; drawn and played with: DR-UI-8; 12 the fold: DR-UI-8; 13/14 the line added, sources relinked: DR-MIX-13; 15 the sidechain: DR-MIX-15; 8's linked selection with U4) — 16 ✅ (the mixer's numbers bound, DR-MIX-16) — 17 ✅ (latency compensated, DR-MIX-17); 4 amended ✅ (forward = later in processing order, DR-MIX-4)

The user's model (2026-10-08): *"sample got its own line by default; we can add multiple mixers as
the user wants their workspace organised; the 1st mixer stores the samples, the 2nd receives from
the 1st; the 2nd goes to master by default; a matrix view shows the whole send picture."*

- **R-MIX-1 A channel IS a strip.** There is no insert number between a sound and its strip (the
  hidden indirection that makes FL Studio's mixer unreadable). Kinds: `audio` (plays sample clips),
  `instrument` (hosts an instrument, played by note clips), `bus` (receives). A strip has gain (dB),
  pan, mute, solo, a rack (R-FX-5), one main output and any number of sends.
- **R-MIX-2 Every sample FILE gets its own strip by default.** Dropping a file the project has not
  used creates an audio strip named after it; placing the same file again reuses that strip — one
  strip per source, not per clip. A recording lands in the armed strip. A clip can be moved to any
  other strip of its kind.
- **R-MIX-3 Mixers are pages the user adds, renames and orders.** Every strip lives on exactly one
  mixer. A new project has **Mixer 1 "Sources"** (empty) and **Mixer 2 "Buses"** holding one bus
  **"Main"**; every new source strip is created on the first mixer and routed to "Main"; "Main"
  feeds master.
- **R-MIX-4 Routing only goes forward.** A strip's output and its sends may target a strip on a
  LATER mixer, the master, or an output port — never a strip on its own or an earlier mixer. A
  feedback loop therefore cannot be built at all, the processing order is the mixer order, and the
  flow always reads left to right. A backward route is refused naming both ends (was SR-ROUTE-5,
  which allowed any acyclic graph). (**AMENDED (R-MIX-15, sidechain, 2026-10-09):** a SIDECHAIN
  key — a signal a strip's detector listens to, not something it plays — may come from any strip
  EARLIER in processing order, the same mixer included: the key is computed before the strip that
  reads it, so no loop can form. Main outputs and audible sends stay forward-only.) (**AMENDED (audit,
  2026-10-09):** "forward" is LATER IN PROCESSING ORDER — mixer order, then strip order within a mixer
  — for outputs and sends too, as it already was for keys. Found: a bus made by `strip add --kind bus`
  (and the GUI's Add Bus) lands on Buses with Main and could not feed Main, so every reverb return or
  group skipped Main. The order is still total, so no loop can form; `feedsForward` stays the ONE copy
  of the rule; a reorder (`strip move`, `mixer move`) that would point any route backward is refused.)
- **R-MIX-5 A send** has its own level and is pre- or post-fader (was SR-MIX-3). The main output is
  where the fader goes; a send is an extra copy.
- **R-MIX-6 The master** sums every strip routed to it, has a gain and a rack, and feeds one or
  more output ports (R-DEV-3).
- **R-MIX-7 Solo** silences every strip that is not soloed and does not feed or is not fed by a
  soloed strip (solo-in-place, the signal path kept alive).
- **R-MIX-8 "Fed by" is on every strip**, computed never stored: how many clips and from which
  lanes, and which strips route or send to it. Selecting a strip lights its clips on the timeline;
  selecting a clip lights its strip and draws its path to the outputs.
- **R-MIX-9 The matrix** shows every route at once: rows = every strip (grouped by mixer) and every
  input port; columns = every later strip, the master, and every output port grouped by device.
  A cell shows the main output (●), a send's level in dB and pre/post. Editable from the cell.
  `matrix print [--json]` prints the same thing. (**Added, U3:** which cells are open is the
  service's answer — `strips[].targets` — so the view hatches what routing refuses rather than
  re-deriving the rule.)
- **R-MIX-10 Mix audit** — a command and a panel: strips with no clips, clips on muted strips,
  strips that reach no output, buses with a single input, peaks over 0 dBFS in the last render or
  playback. Assertable from the CLI like Interstellar's `lint`.
- **R-MIX-11 Mix laws are the suite's**: constant-power balance pan with unity at centre and
  linear-amplitude fades — Interstellar's `AudioMix` — so one project sounds the same in both apps.
- **R-MIX-13 A line is added from the mixer** (user request, 2026-10-09): every mixer page ends with
  "+ Line" — an audio line, a bus, or any instrument of the registry — one command,
  `strip add --kind … --mixer <mx>`; the new card grows in.
- **R-MIX-14 A source can be relinked to another line** (user request, 2026-10-09): a clip plays
  through any strip of its kind — its menu on the lanes offers "Play through ▸" (`clip move --strip`);
  a strip's menu offers "Move its clips to ▸", which moves EVERY clip of it in one command
  (`strip relink <from> --to <to>`, refused across kinds).
- **R-MIX-16 The mixer's numbers take formulas and automation too** (user request, 2026-10-09: *"even
  mixer can have formula and automation"*): a right-click on a fader, a pan, a send's level or the
  master fader offers the device parameter's menu (R-WIN-3) — Create Automation, Formula…, Clear
  Binding, Reset, and Copy Address / Value / as Formula (R-UI-11) — each one command over the
  addresses that already bind (`<ch>.gain`, `.pan`, `<sd>.gain`, `project.masterGain`, R-AUTO-1). A
  bound control says what drives it, and while the song plays it MOVES with the value the engine
  evaluates — a value the model publishes (`bindings[].live`), never one the widget re-derives.
- **R-MIX-17 Latency is compensated** (audit, 2026-10-09: the master limiter's lookahead put the whole
  render 96 samples late, and on a strip it would put that strip out of time with the rest): a device
  reports its latency (DSP `Device::latency()`); the engine delays every other path so all signals meet
  in time at each strip, bus and the master; the offline render starts at beat 0 exactly (the master's
  latency trimmed) and live playback reports the heard position minus it. Measured: a limiter on one
  strip keeps that strip sample-aligned with an unprocessed twin.
- **R-MIX-15 Sidechain** (R-EDM-3): a send may feed a strip's SIDECHAIN input instead of its audio
  (`send add <ch> --to <ch> --sidechain`); the strip's compressor detects on it (its `sidechain`
  switch); the key strip must come earlier in processing order (R-MIX-4, amended). The classic
  pump — the kick ducking the bass — is measured: the bass dips by the compressor's gain reduction
  within its attack after each kick, and not without the send.
- **R-MIX-12 Strips feeding a bus fold under it** in the mixer view (expand to see them), so thirty
  one-shots are not thirty visible strips. (**AMENDED (U3, 2026-10-08):** the strips on a page that
  feed the same bus (two or more) stand together under a header naming it, and a click FOLDS them
  into it — but they start OPEN. A new song's three strips all feed Main; folded by default, the
  only faders a new song has would hide behind a click. The fold is the view's, not the song's: a
  fold is not saved and no command sets it, like zoom.)

## R-LANE — the timeline: lanes hold time — 🚧 IN PROGRESS (R-LANE-1, R-CLIP-1…4 ✅: DR-CLIP-2/3 headless, DR-UI-3 drawn and dragged, DR-LANE-1 `lane move`; R-LANE-2's view with U4) — R-LANE-3 ✅ (instrument tracks, DR-LANE-3) — R-CLIP-6…8 ✅ (MIDI clips made, looped and moved, DR-CLIP-6) — R-CLIP-9, 10 ✅ (copy by Shift-drag and Ctrl+C/V, DR-CLIP-9)

- **R-LANE-1 A lane is a timeline row for organisation only** (FL Studio's playlist): it holds any
  clips, from any strips. Lanes are added, named, coloured, reordered and deleted (deleting a lane
  with clips is refused unless asked to take them with it). (**AMENDED (user request, 2026-10-09,
  third brief):** a lane may instead be an instrument's TRACK (R-LANE-3) — then what it holds plays
  through that instrument. A lane that names no instrument is organisation only, exactly as written.)
- **R-LANE-2 Arrange by Lane | Channel.** One toggle regroups the same clips as one row per strip —
  each row is exactly what one strip plays. In that view, dragging a clip to another row moves it to
  that strip (re-routes it).
- **R-LANE-3 An instrument track** (user request, 2026-10-09, third brief: *"user can create midi track
  and put in an instrument track, so they can loop that midi in the track or move to another"*; the user
  chose "a track owns its instrument"): a lane may name ONE instrument strip as its own
  (`<lane>.strip`); it is then that instrument's track and a note clip on it plays through it. Placed
  there, a clip plays through it; moved there (`clip move --lane`), it is re-routed in the same command,
  and the output says so; told to play through another instrument (`clip move --strip`), it moves to
  that instrument's track (its newest, else a new one) in the same command. An audio clip is refused
  on an instrument's track, naming it — a track is exactly what its instrument plays. A lane made for
  an instrument (`clip add --instrument`, an instrument's first clip, `lane add --strip <ch>`) is its
  track; one instrument may have several. A track's header names its instrument and, while the lane
  has no name of its own, shows the strip's name — renaming the instrument renames it (D-2's lane
  half). `set <ln>.strip=<ch>` makes a lane a track (refused while it holds a clip of another strip,
  naming it); `set <ln>.strip=none` makes it plain. Deleting the instrument leaves its tracks plain
  lanes. A song saved before tracks existed (no `tracks` line in its header) opens with every lane
  whose clips all play through one instrument strip made that strip's track — the lanes R-SVC-8's lane
  reuse built; one still carrying the name it was made with (its strip's, or its instrument's) shows the
  strip's name — and is saved with the line from then on (detailed building E3).
- **R-CLIP-1 An audio clip** plays a span of a file: `at` (beats), source `in`/`out` (seconds — a
  file has no tempo), gain, fade in/out, loop to fill a length (was SR-CLIP-1/2).
- **R-CLIP-2 A note clip** plays a **pattern** — a named list of notes (pitch, start, length,
  velocity) with its own length; a clip longer than its pattern loops it (the EDM case). It sounds
  through exactly one instrument strip.
- **R-CLIP-3 Duplicates are linked.** Duplicating a note clip makes a second clip of the SAME
  pattern: editing the notes edits every copy. "Make unique" gives a clip its own copy.
- **R-CLIP-4 Clips may overlap** on a lane; overlapping audio sums (was SR-CLIP-4).
- **R-CLIP-5 Reserved:** time-stretch and pitch-shift (was SR-CLIP-3).
- **R-CLIP-6 A MIDI clip is made on its track** (user request, 2026-10-09, third brief; the user chose to
  keep the piano roll as the clip's editor): a double-click on an instrument track's empty space makes a
  one-bar note clip there on the snap step — a new empty pattern, one command (`clip add --lane <ln> --at
  <b> --length 4`, the track's instrument implied); a right-click there offers New MIDI Clip. The UI
  says **MIDI** for what the grammar calls a note clip and a pattern (the user's word); the grammar keeps
  its names. Double-clicking the clip opens its piano roll, as before (R-ROLL-1).
- **R-CLIP-7 A clip's end is dragged** (*"so they can loop that midi in the track"*): its right edge sets
  its length on the snap step — one `set <ac>.length=<beats>` on release, the edge the pointer's while
  held. A note clip longer than its pattern repeats it (R-CLIP-2), and every repeat's start is marked on
  the clip so the loop can be seen.
- **R-CLIP-8 A clip dragged onto another track plays through that track's instrument** (R-LANE-3): one
  `clip move <ac> --lane <ln> [--at <b>]`; an audio clip dragged onto an instrument's track is refused
  (it eases home), a note clip onto a plain lane keeps its instrument (organisation, R-LANE-1).
- **R-CLIP-9 Shift-drag copies a clip** (user request, 2026-10-16: *"when hold shift drag and drop, duplicate the
  sample/midi clip"*). A clip — audio or MIDI — dragged with Shift held when it is let go is DUPLICATED where it is
  dropped and the original stays: ONE `clip duplicate <ac> --at <b> [--lane <ln>]`. While Shift is held the
  original shows at full strength and the dragged copy carries a "+", both eased in and out as Shift is pressed and
  let go (a drag without it still moves). A MIDI clip's copy plays the same pattern — linked (R-CLIP-3); dropped on
  an instrument's track it plays through that instrument (R-CLIP-8), and an audio copy is refused there as a move
  is. The copy is selected, where the pointer left it.
- **R-CLIP-10 Ctrl+C / Ctrl+V copy and paste a clip** (same request: *"same with ctrl C and V"*). Ctrl+C copies the
  selected clip (the window's clipboard — nothing is sent); Ctrl+V pastes a duplicate of it on its own lane AT THE
  TIME BAR — ONE `clip duplicate <ac> --at <b>` — and selects it; Ctrl+V again, the time bar not moved, lands right
  after the last copy, so pastes run end to end. Both are keymap actions (`lanes.copy`, `lanes.paste`, R-SET-4) and
  Edit menu items; while a piano roll has the keys its own Ctrl+C / Ctrl+V are the notes' (R-ROLL-8).

## R-TIME — transport and time — ✅ BUILT (1–4: DR-PLAY-1, the metronome DR-EDM-2, the loop region on the ruler DR-EDM-7) — 5 ✅ (the ruler on the grid, DR-UI-10) — 6 ✅ (the ruler dragged, DR-TIME-6) — 7, 8 ✅ (stop back to the cue, or here: DR-TIME-7)

- **R-TIME-1 Beats are authoritative**, 960 PPQ; seconds are derived from the tempo (was SR-TIME-1).
- **R-TIME-2 One tempo and one meter** per project; a tempo/meter map is reserved (was SR-TIME-2/5).
- **R-TIME-3 Sample-accurate:** notes start on their exact sample (R-DSP-5) (was SR-TIME-3).
- **R-TIME-5 A click on the ruler goes there, on the grid you see** (user request, 2026-10-09: *"when click
  on beat indicator on timeline, move to that position with snap to beat at that zoom, if zoom max, no
  snap"*): the seek lands on the nearest line of the zoom's grid (R-UI-10); at the deepest zoom it does
  not snap at all — the exact tick under the pointer.
- **R-TIME-6 The ruler takes drags** (user request, 2026-10-09, third brief: *"time bar interact with drag
  and drop too, not just click"*): pressed and dragged, the playhead follows the pointer on the grid you
  see (unsnapped at the deepest zoom, R-TIME-5) — a `transport seek` each time it reaches a new grid
  line, as a fader sends each step, so a playing song is heard from there, and once more where it is
  let go. The loop region's brace is dragged by its body (it moves, its length kept) or by an end (it
  resizes): one `transport loop` on release. Shift-drag still draws a new loop (R-EDM-7); a click still
  seeks (R-TIME-5); a click inside the brace still clears it. A seek is not an edit: no undo step, the
  song stays saved.
- **R-TIME-4 Transport:** play, stop, seek, loop region, metronome (was SR-TIME-4). (**Detailed
  2026-10-09, R-EDM-2/7:** the metronome is the machine's setting (`settings set metronome=on`), clicks
  on every beat while playing with the bar's first accented, its sound a pad of the DSP library's
  Drum Machine, and is NEVER in a render; the loop region is drawn on the ruler and dragged there.)
- **R-TIME-7 Stop returns the time bar to where it was put** (user request, 2026-10-16: *"remember where user
  put the time bar, when stop using space return to that time bar"*). The transport keeps a CUE — where the
  user last PUT the time bar: a seek (a ruler click or drag, the start key), or a play that says where it
  begins (`--from`, a roll's clip). A plain `transport play` starts wherever the time bar stands and leaves the
  cue alone. `transport stop --return` stops and puts the time bar back at the cue; the play/stop key (Space
  by default) and the song bar's Stop are that. The cue is published (`transport.cue`) and marked on the
  ruler while playing — where the time bar will go back — fading in and out with the play.
  (**AMENDED (user follow-up, 2026-10-16:** *"Ctrl space only temp remember that position, when use space to
  stop again back to original position that user set"*). The first cut also moved the cue to wherever ANY
  play began, so after a Ctrl+Space stop the next Space stop came back to the Ctrl+Space position, not to the
  one the user had set. A plain play no longer moves the cue.)
- **R-TIME-8 Or stop where it is** (same request: *"when ctrl space stop at the time user stop"*): the
  stop-here key (Ctrl+Space by default) stops and leaves the time bar where it was heard (`transport stop`, as
  before); the next play starts from there. That place is TEMPORARY: it is not the cue, so a play/stop key
  stop after it goes back to where the user put the time bar (R-TIME-7). Pressed while stopped, either key
  plays. (**AMENDED (same follow-up, 2026-10-16):** it said "and there is the new cue".)

---

## R-DEV — audio devices: use every device you have — 🚧 IN PROGRESS (listing and the port map ✅ DR-SET-1; output followers held to the clock and aligned ✅ DR-DEV-1; an input on another device ✅ DR-DEV-1a; Settings › Audio — N32 part 4)

The user (2026-10-08): *"we can send to multiple output devices; I want solaris to handle multiple
audio devices so the user can take advantage of their devices."*

- **R-DEV-1 Several devices at once,** inputs and outputs. The user enables devices in Settings and
  marks one as the **clock**.
- **R-DEV-2 Followers are drift-corrected.** Every device has its own crystal, and 48 000 vs 48 003 Hz
  becomes a click within minutes. The engine runs on the clock device; every other device is fed
  through a ring buffer read by an **adaptive resampler** that holds the buffer's fill steady. The
  resampler is a DSP library module (R-DSP-1).
- **R-DEV-3 Logical ports, not device names, in the project.** A project names output and input
  ports ("Main 1–2", "Phones", "Click"); Settings maps each port to a device's channels, per machine.
  A port whose device this machine lacks is **offline** — shown, silent, never fatal.
  (**Changes SR-OUT-3**, which persisted hardware routing in the project: a project moves between
  machines and device names do not.)
- **R-DEV-4 Latency compensation across devices:** outputs on faster devices are delayed so every
  port is heard at the same moment; a recording is aligned per input device.
- **R-DEV-5 Each device's state is observable:** latency, measured drift (ppm), xruns.
- **R-DEV-6 Any strip, send or the master can reach any output port** (was SR-OUT-1/2). Use cases:
  master → speakers on device A; a headphone cue (sends) → device B; the click → device B only.
- **R-DEV-7 The project rate is the project's.** The clock device opens at it when it can; otherwise
  the driver resamples and the transport says so.
- **R-DEV-8 Linux first**, over the PulseAudio API (which PipeWire also serves); Windows later. (**AMENDED
  (R-DEV-9, 2026-10-16):** a native PipeWire driver beside it, for low latency; PulseAudio stays the fallback.)

## R-SET — settings — ✅ IMPLEMENTED (DR-SET-1, DR-SET-2, DR-SET-3, DR-SET-4, DR-SET-5)

- **R-SET-1 A settings sheet in Cosmo's modal style** — the same scrim, card, chips, fade and Done
  button — with Solaris's rows: output and input devices (the clock is the output), sample rate
  (the default for new projects and the preferred device rate), buffer size with the latency it
  costs in ms, the port map, and the **sample folders** (add, remove, reorder) that become the
  browser's quick-access list.
  (**AMENDED 2026-10-08, building it:** this said "Cosmo's Settings dialog, reused" with Solaris rows
  added. Cosmo's dialog is an IMAGE engine's — preview quality, GPU, threads, CPU share — none of
  which Solaris has, and its extension point is rows of chips, which cannot hold a folder list with
  add and remove. Showing a DAW's user five rows about a photo engine is worse than a second dialog
  that looks the same; so the reuse is the look, not the class. A test tone waits for P2.)
- **R-SET-3 The full option set** (user request, 2026-10-09: *"full option like cosmo and
  interstellar"*): the sheet is in sections as their Engine Settings are — **Audio** (output, input,
  sample rate, buffer), **Playback** (metronome on/off and level), **New songs** (tempo and time
  signature a new song starts with), **Sample folders**, **Interface** (reduced motion) — every
  option a `settings set <key>=…` line. (**AMENDED (R-SET-5, 2026-10-16):** the sections are PAGES now, not
  titles down one long sheet.)
- **R-SET-2 Settings are the machine's**, persisted beside the app's other settings, never in a
  project. Each change is a `settings set` line the service validates, applies and persists.
- **R-SET-5 Settings in sections, a page each** (user request, 2026-10-16: *"setting need to have section,
  don't put all in 1 page"*). The sheet is a rail of sections — Audio, Playback, New songs, Sample folders,
  Interface, Keyboard — beside ONE page, the chosen section's; a section clicked cross-fades its page in (never
  a cut) and the rail's highlight slides to it. A page taller than the sheet scrolls on its own. The sheet opens
  on the section it was last left on while the app runs (the view's — nothing a script needs). Every control
  is still one `settings set …` line, and a change from a shell shows on its page.
- **R-SET-6 A choice is a combo box** (same request: *"for option need to have combo box"*). A setting that is
  one value of a list — Output, Input, Sample rate, Buffer, Click level, Preview level, a new song's Tempo and
  Meter — is a combo box: the value it has, a caret; clicked, its list drops (eased, scrolling when long), a
  row picked is ONE `settings set` line. An on/off setting (Metronome, Reduced motion) is a toggle switch,
  its thumb sliding. The folder list and the keymap keep their own rows (they are lists, not a choice). The
  combo box is Artboard's (`ComboBox`), dressed in cosmo's tokens as Genesis dresses it — never a fork. A value
  the list does not have (a device not connected now, a tempo set from a shell) is still shown, as an entry of
  its own, so the setting is never invisible. (**AMENDS R-SET-1's "chips"**: the look stays cosmo's — scrim,
  card, fade, Done — the controls are combo boxes and toggles.)
- **R-SET-4 Hotkeys are settings** (user request, 2026-10-16: *"need to have setting for hot key"*). Every
  keyboard shortcut is an ACTION with its keys — in areas: Transport (play/stop back to the cue, stop here,
  to the start), Song (save, undo, redo, settings), Lanes (delete, duplicate and copy the selected clip, paste it — R-CLIP-10), Piano roll
  (select all, deselect, delete, duplicate, copy, paste, up, down, octave up and down, left, right). The
  sheet's **Keyboard** section lists them, each with its keys: a key clicked listens for the next ("Press a
  key…" — Escape cancels, Backspace removes it), "+" adds one, Reset gives the defaults back. Each change is
  `settings set key.<action>=<keys>` — a comma list of combos (`Ctrl+Shift+Z, Ctrl+Y`), `none`, or `default`;
  a key not known, or one another action of the same area uses (the song's three areas count as one; the
  roll's keys shadow the song's while the roll has the keyboard) is refused naming it. Machine settings
  (R-SET-2), persisted; the model publishes every action with its label, keys and defaults, so any front end
  resolves a key the same way.

## R-HOME — Home — ✅ IMPLEMENTED (DR-HOME-1, DR-UI-1)

- **R-HOME-1 Like Interstellar's and Cosmo's.** Recent projects as cards, newest first: name,
  `bpm · length`, version count. New (name, tempo, meter, sample rate defaulting from Settings),
  Open, Settings.

## R-BROWSE — the left panel: samples and presets — 🚧 IN PROGRESS (1 ✅ DR-BROWSE-1/2, presets wait for a preset store; 2 ✅ audition, DR-EDM-9 (R-EDM-9); 3 ✅ onto the lanes, DR-BROWSE-2 — onto a strip or a rack with U3/U4) — 4 ✅ (the song's MIDI list, DR-BROWSE-4)

- **R-BROWSE-1 Three sections:** Folders (the sample folders from Settings), Presets (every
  instrument and effect in the registry with its factory and user presets), Project (the sounds the
  project already uses). (**AMENDED (U2, 2026-10-08):** the tabs read **Samples · Instruments ·
  Song** — a tab is named for what is in it: "Folders" named the mechanism; "Presets" promised a
  preset store that does not exist yet, so the tab lists the registry's instruments and effects and
  a type's factory and user presets will join it under its row; "Project" is "the song" everywhere
  else in this app.)
- **R-BROWSE-4 The song's MIDI list** (user request, 2026-10-09, third brief: *"there should be a midi
  list of project to drag and drop into project"*): the Song tab lists the song's MIDI (its patterns)
  above its samples — each with its name, length, note count and the instruments that play it; keyed
  rows that grow in and shrink out. Dragged onto an instrument's track → a clip of it there, on the snap
  step (`clip add --pattern <pt> --lane <ln> --at <b>`, the track's instrument implied); onto a plain
  lane or below the last → it plays through the instrument its newest clip plays through (on that
  lane, or a new track for it); a pattern no clip plays, dropped off a track → a notice naming what to
  do, nothing sent. **New MIDI** → `pattern new`; a double-click → its piano roll; its menu: Rename,
  Duplicate (`pattern duplicate`), Delete (refused while a clip plays it, naming them), Copy ID. Every
  drop is one command (R-BROWSE-3).
- **R-BROWSE-2 Click auditions** a sample through the master's port without touching the project.
- **R-BROWSE-3 Drag and drop.** A sample onto a lane → a clip at the drop point on a new or reused
  strip (R-MIX-2); into empty space → a new lane too; onto a strip → load it there. An instrument
  preset into empty space → a new instrument strip + lane + an empty clip; onto a strip's rack → an
  effect inserted at that slot. **Every drop is one command** (R-G-4). (**Added 2026-10-08,
  building U2:** an instrument dropped is `clip add --instrument <type>` — the new strip and its
  empty clip in one command, so a refusal of either leaves nothing half made; and a refused command
  emits no `project.changed` for the work it undid.) (**AMENDED (R-SVC-8, 2026-10-09):** a `clip
  add` with no `--lane` now goes on the lane its strip's clips are on — a new lane only for a strip
  with none — so the drop into empty space asks for its new lane with `--lane new`; the gesture and
  what it makes are unchanged.)

---

## R-HIST — the history is a tree, as cosmo's (user request, 2026-10-16: *"need to have tree history like cosmo"*) — ✅ IMPLEMENTED (1, 2, 4 DR-HIST-1; 3 DR-HIST-2)

Cosmo keeps an image's edits as a branching tree (cosmo DR-HIST-1…3): undo walks to the parent, an edit made
there starts a NEW branch and leaves the old future as it was, and History ▸ Show History Tree… draws it
git-style, a node clicked jumping there. Solaris's undo was a stack whose redo half a new edit erased. One
suite (law 16): the same model, the same view.

- **R-HIST-1 Branching.** Each landed edit is a node — the song as it was made, its label (what made it:
  `notes add pt_1`, `set ch_2.gain`), its parent. `undo` moves to the parent; `redo` to the NEWEST child (the
  branch made last); an edit made with children below starts another child — a branch — and every older
  branch stays. Consecutive `set`s of the same addresses stay ONE node (R-EDM-1). The tree keeps at most 200
  nodes: past that the oldest nodes off the path to the current one go first. A new, opened or closed song
  starts its own tree at one root ("Opened" / "New song").
- **R-HIST-2 Any node, by one line.** `history` prints the tree (one node a line: its number, its parent, its
  label, `*` at the current one); `history jump <n>` makes node `n` the song — an edit, so the song is
  unsaved and the player hears it, but NOT a node itself (it moves through the tree, as undo does). The model
  publishes the tree (`history[]`: id, parent, label; `historyCurrent`), so any front end draws the same tree.
- **R-HIST-3 The tree, seen.** Edit ▸ Show History Tree… opens cosmo's own history view (reused from
  `apps/cosmo`, never copied — R-UI-4): root at the top, branches fanning into lanes, the current node lit;
  a node clicked is ONE `history jump <n>`, the view staying open on the new current; drag pans, Escape or the
  scrim closes. Its open and close fade, its highlight eases (design rule §1).
- **R-HIST-4 The tree lives while the song is open** (the user's answer, 2026-10-16: *"only while the song is
  open"*). It is never written — not into the `.slp`, not beside it; a song saved and opened again starts a new
  tree at one root, as the undo stack did. Cosmo saves an image's tree with its project (cosmo R-PERSIST-1); for a
  song that would be up to 200 copies of it, and the user chose not to. This is a deliberate difference from
  cosmo, not a gap.

## R-VER — versions of a song — 📋 SPECIFIED

The user (2026-10-08): *"should have branching too for multi version of a song."* Interstellar's
R-VER, applied to an arrangement:

- **R-VER-1 A project holds many arrangements;** `main` is the root; any other declares a base and
  stores **deltas**, never a copy.
- **R-VER-2 The sound is shared** — strips, instruments, racks, mixers — as Interstellar's rack is.
  A version may override any field of any node (a strip's mute, a device's parameter, a clip's
  position) and may add or drop nodes.
- **R-VER-3 Inherit live, override to diverge, pin/freeze to stop, rebase to reconcile** — the same
  four verbs and meanings as Interstellar R-VER-2..4. Examples: *Instrumental* = base + the vocal
  strip muted; *Extended* = base + a longer intro; *Radio edit, delivered* = frozen.
- **R-VER-4 Every version renders by name.** (**Replaces SR-VCS-1..3**, the project-level Nebula
  branching with its own commit store, for Interstellar's reason: it puts the version boundary
  around the thing versions share. Merging two songs — SR-MERGE-2 — survives as an import, R-VER-5.)
- **R-VER-5 Reserved:** import another song (concatenate or overlay), embed a song in a song or in
  Interstellar (was SR-MERGE-2, SR-EMBED-1..4).

## R-RENDER — offline render — ✅ IMPLEMENTED (DR-RENDER-1)

- **R-RENDER-1 Render is offline and deterministic:** the same project, version and range give the
  same bytes (was SR-NFR-1, SR-RENDER-1).
- **R-RENDER-2 What it writes:** the master mixdown; selected strips as stems; or one file per
  output port. WAV, 24-bit or 32-bit float, at the project rate (was SR-RENDER-1/2).
- **R-RENDER-3 A range or the whole song**, with the tail rendered until the output falls below
  −90 dBFS or a 10 s cap (was SR-RENDER-3/4).

## R-PLAY — real-time playback — ✅ IMPLEMENTED on one device (1–2: DR-PLAY-1/2) — 3 ✅ (peaks DR-PLAY-3; RMS, held peak, clip latch DR-METER-2) — several devices: P2, R-DEV-2/4

- **R-PLAY-1 Playback runs the same graph the render runs** on the clock device's callback;
  offline and live produce the same samples (was SR-RT-1..3, SR-SCOPE-2).
- **R-PLAY-2 The audio thread never allocates, locks or does I/O;** edits reach it through lock-free
  queues; xruns are counted (was SR-NFR-3).
- **R-PLAY-3 Meters** on every strip and the master: peak and RMS per channel, a held peak, a clip
  latch — the numbers in the model, not only drawn.

## R-AUTO — automation and parameter formulas — ✅ IMPLEMENTED (user request, 2026-10-09; DR-AUTO-1…5, made from a window: DR-UI-5) — 10 ✅ (bezier, DR-AUTO-6), 11 ✅ (its window, DR-AUTO-7) — 12, 13 ✅ (a looping automation, a pump: DR-AUTO-8)

The user: *"all number param can be link, apply formula, create automation, apply formula with
automation like FL Studio. Automation is created separately, make the formula core of the binding.
When creating automation from a property, solaris creates the automation named with the property and
the object it belongs to, shows it on the timeline, and that property will use a formula like
=automation1."*

- **R-AUTO-1 Every numeric address can be BOUND** — a strip's gain and pan, the master gain, a send's
  gain, every numeric parameter of every device. A binding is a formula: `set <address>="=<expr>"`;
  `set <address>=<number>` clears it. One binding per address (R-G-3); a choice or a switch is not
  bindable.
- **R-AUTO-2 The formula language:** numbers; `+ − * / ^`, parentheses, unary minus; `sin cos tan abs
  sign min max clamp lerp pow exp log sqrt floor ceil round frac`; `pi`; the song's `beat`, `bar`,
  `bpm` and `t` (seconds); an automation's id (`au_1` — its curve's value at the position); and any
  numeric address — a LINK (`ch_2.gain`, `dv_1.filter.cutoff`) reading that address's EVALUATED value.
  The result is clamped to the target's range (rounded when it is an integer). A syntax error or an
  unknown name is refused, naming it (R-SVC-3).
- **R-AUTO-3 Bindings are acyclic.** A binding that would make an address depend on itself, directly
  or through links, is refused naming the loop.
- **R-AUTO-4 An automation is its own object** (FL Studio's automation clip): an id (`au_n`), a name, a
  unit and range, and points — beat, value, shape `linear | hold | smooth`. It moves nothing until a
  formula reads it. (**AMENDED (R-AUTO-10, 2026-10-09):** and `bezier`, shaped by handles.) Stored as `#aauto` with `#point` children (amends the suite schema's sketch,
  `docs/audio-format.md` §2.5, which nothing wrote yet).
- **R-AUTO-5 Automation is created from a property in ONE command** (`auto create <address>`): a new
  `au_n` named *"<owner> · <parameter label>"* (*"Bass · Cutoff"*), ranged as the property, with points
  at beat 0 and at the song's end holding the property's current value; it shows on the timeline; and
  the property is bound to `=au_n`. (The user wrote `=automation1`; ids follow the suite's scheme.)
  `auto add --name … [--min --max --unit]` makes one from nothing.
- **R-AUTO-6 Automations are drawn on the timeline** — an AUTOMATION section under the lanes, a row per
  automation showing its name and curve. A click adds a point, a drag moves it (time snapped to a
  sixteenth), a double-click removes it, a right-click sets its shape or deletes the automation; each
  gesture is one command (`auto point add|move|delete|shape`, `auto delete`). (**AMENDED (R-UI-10,
  R-AUTO-11, 2026-10-09):** a point's time snaps to the zoom's grid, not a fixed sixteenth; a double-click
  ON A POINT still removes it, a double-click on the row's header or on the curve away from a point
  opens the automation's window.)
- **R-AUTO-7 Evaluated by the engine, at control rate.** The service compiles the bindings and the
  curves into the graph; the engine evaluates every binding every 64 samples at ABSOLUTE positions,
  so playback and render agree whatever the chunking (R-RENDER-1); gain and pan ramp across each
  control period so nothing zippers. Measured: an automated gain renders the curve's level at known
  beats within 0.1 dB.
- **R-AUTO-8 Explainable:** `eval <address> [--at <beat>] [--explain]` prints the value there and, with
  `--explain`, the formula and every name it reads with its value.
- **R-AUTO-9 Observable:** the model publishes `bindings[]` (address, formula, what it reads) and
  `automations[]` (id, name, unit and range, points, which addresses use it).

- **R-AUTO-10 Bezier automation, like cosmo's curve** (user request, 2026-10-09: *"automation need bezier
  curve like cosmo curve"*): a point's sides may be bezier. The MODEL is Interstellar's (R-ANIM-2,
  `apps/interstellar/model/Anim.h` — After Effects' keyframe: each side linear | bezier | hold with a
  speed, in the automation's unit per beat, and an influence, % of the neighbouring segment) so the
  suite has ONE curve model (law 16); the GESTURE is cosmo's (`CurvePanel`): Alt-drag a point pulls
  out symmetric handles, a handle drags (the opposite one mirrors), Alt breaks the symmetry. One command
  per gesture (`auto point shape <au> --at <b> --shape bezier [--speed-in --influence-in --speed-out
  --influence-out]`); stored on `#point`; evaluated by the engine at control rate from compiled data;
  `eval` agrees with what renders, measured.
- **R-AUTO-11 Double-click an automation to see it** (user request, 2026-10-09: *"double click to see
  information of automation"*): its window (R-WIN) shows its name (renamable), id, unit and range,
  the address it was made from, its points (beat, value, shape), every formula that reads it (address
  and formula), and its value at the playhead — all from the model, nothing re-derived.
- **R-AUTO-12 An automation LOOPS, like FL Studio's automation clip** (user request, 2026-10-16: *"there should be
  option to loop the automation clip like FL studio, so that we can manipulate property in a loop"*). An
  automation has a loop length (`<au>.loop`, beats; 0 = off, the curve as drawn): its first `loop` beats are one
  CYCLE, repeated through its SPAN — from `<au>.at` for `<au>.length` beats (0 = to the end of the song); outside
  the span it rests at the value a cycle ends on. One `set` line each; stored on `#aauto`. Evaluated by the engine
  from compiled data as every curve is (R-AUTO-7), so `eval`, playback and a render agree, measured. On the
  timeline the automation's row shows the span — the first cycle as drawn (edited as ever), each repeat after it
  drawn dimmer with a seam at its start — and the span's two ends are grips (the resize cursor, R-UI-14): dragged,
  ONE `set <au>.at=` / `set <au>.length=` line. Its window offers the loop as a combo box (Off, 1/4, 1/2, 1, 2
  beats, 1, 2, 4 bars) — one line.
- **R-AUTO-13 A pump in one gesture** (the same request: *"right now setup a side chain is hard"*): the side-chain
  FEEL without a side-chain — `auto create <address> --pump` makes the property's automation already looping every
  beat: down on the beat, back to the property's value by the middle of the beat (smooth), then held — and binds
  it, as `auto create` does. The dip is 18 dB for a level in dB, otherwise most of the way to the bottom of the
  range (to 20 % of the way up from it). Right-click a number (a fader, a knob, a parameter row — ParamMenu) offers
  "Create Pump" — the same one line. (A real side-chain — a compressor keyed by the kick, R-MIX-15 — stays; making
  it one gesture is in the analysis, M3.)

## R-WIN — windows inside the song view — ✅ IMPLEMENTED (user request, 2026-10-09; DR-WIN-1, DR-UI-5) — 5 ✅ (resized by the corner, DR-WIN-5)

- **R-WIN-1 A device opens in a WINDOW** (FL Studio's channel window), *"when open a synth setting,
  need to show a window of that synth"*: a rack chip clicked, or an instrument strip's name
  double-clicked, opens a floating window over the song view — titled by the device and its strip,
  closed by ×, dragged by its title bar, several open at once, the last touched on top, at most one
  per device. Opening and closing ease.
- **R-WIN-2 The window lists every parameter the device supports** — label, value in its unit, and
  whether it is a number, a link, a formula or an automation — and **HIGHLIGHTS THE LAST CHANGED**
  (eased). Which parameter changed last is the model's (`devices[].lastChanged`), written by any `set`
  from anywhere — a shell's edit lights it too. (**AMENDED (R-UI-12, user request 2026-10-15):** an
  instrument's window shows its analog face instead of the list; every knob and switch carries the same
  facts — its value in its unit or its binding, the light of the last change, its menu.)
- **R-WIN-3 A parameter's menu** (right-click): Create Automation (R-AUTO-5), Formula… (type one — a
  link is a formula naming an address), Clear Binding, Reset to Default — each one command.
- **R-WIN-4 Window placement and stacking are the view's** — not saved, not commands, like zoom.
- **R-WIN-5 A window is resized by its corner** (the UX audit's U-5, R-SVC-11): dragging a window's
  bottom-right corner sizes it — the pointer's while held, never smaller than its content's floor nor
  out of the layer — and its content lays itself out at the new size (a roll shows more rows, a face
  reflows its modules). The view's, like its place (R-WIN-4): not saved, not a command.

## R-ROLL — the piano roll — ✅ BUILT (user request, 2026-10-09; DR-ROLL-1) — 6 ✅ (play from the roll, DR-ROLL-6) — 7 ✅ (a note heard when made, DR-ROLL-7) — 8 ✅ (the selection, DR-ROLL-8) — 9 ✅ (the pattern grows, DR-ROLL-9) — 10 ✅ (the length chosen, DR-ROLL-10) — 11 ✅ (the note tools, DR-ROLL-11)

- **R-ROLL-1 A piano-roll WINDOW per pattern** (R-WIN's frame), opened by double-clicking a note clip
  or from its instrument's window: keys on the left (C named with its octave; a drum machine's keys
  named by its pads — the registry's note names), a beat grid with bars, the pattern's notes in the
  strip's colour, a velocity lane below, the pattern's end marked.
- **R-ROLL-2 Editing, one command per gesture:** a click on empty grid adds a note of the last-used
  length at the snapped beat (`note add`); a drag moves a note in time and pitch, a drag on its right
  edge resizes it (`note move`); a double-click or right-click deletes it; a drag in the velocity lane
  sets its velocity. Snap 1/4, 1/8, 1/16 (default), 1/32 — NOTE values, a beat being 1/4 — or off; Ctrl+wheel zooms, the wheel
  scrolls, eased.
- **R-ROLL-3 Step mode** — the step sequencer, a VIEW of the same pattern (R-INST-3): a row per pad
  (or per key), sixteen steps a bar; a click toggles a note there. (Detailed while building it, B6: for
  a melodic instrument a row per pitch the pattern uses, and middle C; Steps zooms to fit the pattern.)
- **R-ROLL-4 Quantize and swing:** `pattern quantize <pt> [--grid <beats>] [--swing <0…0.75>]` moves
  every note start onto the grid, delaying every second grid step by the swing.
- **R-ROLL-5 The pattern's length** is set from the roll (`set <pt>.length=`), its end dragged.
- **R-ROLL-6 Play from the roll** (user request, 2026-10-15: *"when open a midi roll, user can press play
  directly in that"*). A ▶ at the start of the roll's toolbar plays the clip the roll belongs to — the one it
  was opened from, else the pattern's first clip in time — from its start, LOOPING its span, the whole song
  sounding there, until stopped (■, the same button); the song's own loop region comes back when it stops.
  One command, `transport play --clip <ac>`; the transport publishes which clip it loops so every front end
  can show it. Space stops whatever plays; stopped, with a roll window in front, it is that ▶ (else the
  song's play). The
  roll draws the playhead where its clip is heard and keeps it in view while playing, eased. A pattern on no
  track has nothing to sound through: its ▶ is dimmed and says so.
- **R-ROLL-7 A note is heard when it is made** (user request, 2026-10-15: *"when create a note, play
  sound"*). A click that adds a note, a drag that takes a note into a new pitch row (each row it enters) and
  a press on the roll's keys play that pitch through the clip's track — its instrument and its rack, at its
  fader and pan, as it will sound in the song — on a stream of its own, so it is heard whether the song plays
  or not. Never in a render, never an edit, never in undo. One command, `note preview <ch> <pitch> [--vel]
  [--length]`. A preview started while another rings takes over with a short fade, never a click.

- **R-ROLL-8 A selection, and what is done to it** (U-2, R-SVC-11): a click on a note selects it (Shift+click
  adds or removes one; a click on empty grid still adds a note and clears the selection); Ctrl+drag on the
  grid draws a box selecting every note it touches — wherever it starts, even on a note: Ctrl is selecting and
  never moves a note (**AMENDED (the UX audit's U-10, 2026-10-15):** a box started on a note used to grab the
  note — and, the note being selected, carried the whole selection away); Ctrl+A selects all. The selection is lit. Done to it, each
  ONE edit: a selected note dragged moves the whole selection (`notes move <pt> "<pitch@beat> …" --by <beats>
  --semi <n>`); ↑/↓ transpose it a semitone (Shift: an octave), ←/→ move it a snap step; Delete removes it
  (`notes delete <pt> "<pitch@beat> …"`); Ctrl+D duplicates it right after itself — by its span, the earliest
  start to the latest end, rounded up to the snap — and the copy becomes the selection (`notes add`, the copy's
  tokens); Ctrl+C copies it to the app's clipboard and Ctrl+V pastes it into this roll or another at the same
  beats, selected, ready to be dragged. A bulk edit that would land a note on an unselected one, below beat 0 or
  outside 0–127 is refused whole, naming the first.
- **R-ROLL-9 The pattern grows to hold what is written in it** (U-3, R-SVC-11): a note added, moved, resized or
  pasted past the pattern's end lengthens the pattern to the end of the bar that holds the note's end — one
  edit with the note's — and a clip that played its pattern whole (its length the pattern's) grows with it; a
  clip cut shorter or looping on stays as it is. (**AMENDS R-SVC-8's "warns of notes past the pattern's end":**
  `notes add` grows the pattern instead of warning — a note no one hears is never what was asked for; the
  audit's warning stays for a song saved before.)
- **R-ROLL-10 The length a click makes is shown and chosen** (U-4, R-SVC-11): the toolbar names it ("Length ¼"
  — the last length, as now), and its menu sets it — the snap values, ½, 1, 2 and 4 beats, or "Last" (the
  length of the note last made or resized, the default). The view's, not a command.

## R-VST — VST3 — ✅ BUILT 1–5 (user request, 2026-10-09; replaces R-INST-4's VST3 half; DR-VST-1; 6 reserved) — 7, 8 ✅ (the plugins' own editor — the shared panel, Drum Machine's pads — DR-VST-7)

The user: *"need to sync with current workflow of other apps that use VST3 (because this will use
VST3 later) — install the VST3 SDK and adapt Basic Synth and Drum Machine to VST3."* No app in the
workspace hosts VST3; the workflow is the industry's: an instrument is a plugin with a processor, a
controller, normalised parameters and saved state.

- **R-VST-1 The SDK is installed once per machine** (`~/sdk/vst3sdk`, Steinberg's `vst3sdk`, its
  version and licence recorded in DR-VST-1) and found through `VST3_SDK_ROOT`; with no SDK the build
  skips the plugins and says so.
- **R-VST-2 Basic Synth and Drum Machine build as VST3 instruments FROM THE DSP LIBRARY'S OWN
  CLASSES** (`core/DigitalSignalProcessing/apps/vst3`) — no copy of their sound (R-DSP-1).
- **R-VST-3 Their parameters ARE the registry's:** one VST parameter per registry parameter, its id
  the parameter's index in its type (frozen — a shipped id never moves), title, unit, default and step
  count from the spec; the normalised 0…1 is ONE shared mapping in the DSP library
  (`normalizedFromValue` / `valueFromNormalized`, honouring `logScale`, `integer`, choices).
- **R-VST-4 State** saves and restores every parameter as the registry's text — the values a `.slp`
  stores.
- **R-VST-5 Verified:** the SDK's validator passes both plugins; an offline host renders each and
  equals the DSP device's own render sample for sample at the same parameters, notes and block size.
- **R-VST-6 Reserved:** hosting other makers' VST3 in Solaris (scan `~/.vst3`, load, process, their
  editor window) — host work (R-SVC-4).
- **R-VST-7 Their own editor, in the Arstro look** (user request, 2026-10-09, third brief: *"make the
  Basic Synth a vst3 app and make the UI follow Arstro theme like Cosmo/Interstellar and Solaris, drum
  machine also"*; the user chose "plugins with an Arstro editor", not standalone apps): each plugin opens
  its own editor (`IPlugView` — on Linux an X11 window embedded in the host's, driven by the host's
  `IRunLoop`) instead of the host's generic sliders. It is drawn by Artboard with cosmo's tokens and
  embedded fonts and Solaris's teal (they are Solaris's instruments) and obeys `arstro.design.rule` like
  any Arstro window — nothing snaps; a value the HOST changes (automation, a preset, undo) eases in.
  Its controls are GENERATED from the registry (law 2): a section per parameter group, a slider per
  number with its unit and taper, a segmented choice per choice, double-click → the default; every
  gesture is a host-automatable edit (`beginEdit` / `performEdit` / `endEdit`) through the ONE shared
  normalisation (R-VST-3). Drum Machine's editor adds its pads: a grid named by the registry's note
  names, a click plays the pad through the plugin's own processor, the pad picked shows its controls.
  The editor is the umbrella's (it needs Artboard and cosmo, which the DSP repo does not carry): the DSP
  repo alone still builds both plugins with the host's generic view, through one seam. (**AMENDED (R-UI-12,
  user request 2026-10-15):** the editor's content is the instrument's analog face — sections, knobs,
  switches, displays — not the parameter list, and it opens at the face's own size.)
- **R-VST-8 Verified:** the validator still passes both plugins with their editors; each editor is
  rendered headless at its size and looked at; a slider dragged makes `performEdit` with the shared
  mapping's value; a host change is caught mid-tween; a pad click reaches the processor as a note; and
  the editor runs embedded in a real VST3 host window on X11 (the SDK's editor host, L5).

## R-EDM — what a professional EDM DAW needs (user request, 2026-10-09: *"suggest all necessary features"*) — 🚧 IN PROGRESS (1 ✅ DR-EDM-1; 2 ✅ DR-EDM-2; 3 ✅ with R-MIX-15, DR-MIX-15; 4 ✅ DR-EDM-4; 5 ✅ with R-AUTO, DR-AUTO-2; 6 ✅ with R-ROLL, DR-ROLL-1; 7 ✅ DR-EDM-7; 8 ✅ DR-EDM-8; 9 ✅ DR-EDM-9)

**Built with this brief:**
- **R-EDM-1 Undo and redo** of every edit — `undo`, `redo`; Ctrl+Z, Ctrl+Shift+Z / Ctrl+Y; the model
  names what each would undo. A machine setting or the transport is not an edit. Consecutive `set`s of
  the same addresses — a dragged fader — are ONE step (added while building it, B2).
  (**AMENDED (R-HIST, user request 2026-10-16):** the history is a TREE — an edit after an undo no longer
  throws the undone steps away; they stay a branch. `redo` takes the newest branch.)
- **R-EDM-2 Metronome** (R-TIME-4, detailed).
- **R-EDM-3 Sidechain compression** (R-MIX-15): the DSP Compressor gains a sidechain detector input.
- **R-EDM-4 A limiter** — a DSP brickwall limiter (lookahead, ceiling, release) for the master: the
  output never exceeds its ceiling, measured.
- **R-EDM-5 Tempo-synced values** by formula: `bpm` in a formula (R-AUTO-2) makes a delay time or an
  LFO rate follow the song (`=60000/bpm*0.75` is a dotted eighth).
- **R-EDM-6 Step sequencer, quantize, swing** (R-ROLL-3/4).
- **R-EDM-7 The loop region on the ruler** (R-TIME-4, detailed): Shift-drag on the ruler sets it
  (`transport loop`), its brace drawn there; a click inside it clears it.

**Built after** (each its own ledger task): R-EDM-8 a sampler (one-shots, chromatic) — DR-EDM-8 ·
R-EDM-9 audition in the browser (R-BROWSE-2) — DR-EDM-9.

**Specified, scheduled after** (each its own ledger task): R-EDM-10 a MIDI keyboard (play, record into a pattern)
· R-EDM-11 audio recording (R-REC) · R-EDM-12 clip fades and gain on the lanes, slice, reverse ·
R-EDM-13 warp / time-stretch (R-CLIP-5) · R-EDM-14 a tempo map and tempo automation · R-EDM-15 a
spectrum analyser and a LUFS meter on the master · R-EDM-16 export MP3 / FLAC beside WAV · R-EDM-17
hosting other makers' VST3 (R-VST-6) · R-EDM-18 bounce / freeze a strip to audio · R-EDM-19 several
audio devices (R-DEV, P2) · R-EDM-20 song templates and device presets.

## R-PRO — a professional DAW (user request, 2026-10-16: *"implement all"* of `docs/pro-daw-analysis.md`) — 🚧 IN PROGRESS (R-SAFE ✅ DR-SAFE-1; R-MIDI-1 ✅ DR-MIDI-1; R-METER ✅ DR-METER-1/2; R-EDM-4a ✅ DR-EDM-4a; R-EXPORT-1 ✅ DR-EXPORT-1; R-MIX-18 ✅ DR-MIX-18; R-DEV-9 output ✅ DR-DEV-9; R-MIDI-2 ✅ DR-MIDI-2; R-INST-6 ✅ DR-INST-6; R-MIDI-3 ✅ DR-MIDI-3; R-ROLL-11 ✅ DR-ROLL-11; R-ARR-1 ✅ DR-ARR-1; R-ARR-2 ✅ DR-ARR-2; R-ARR-3 ✅ DR-ARR-3; R-REC-1 ✅ DR-REC-1 — loop takes are R-REC-2's; R-REC-2 ✅ DR-REC-2; R-EDIT-1 ✅ DR-EDIT-1; R-WARP-1 ✅ DR-WARP-1; R-TEMPO-1 ✅ DR-TEMPO-1; R-PRESET-1 ✅ DR-PRESET-1; R-PRESET-2 ✅ DR-PRESET-2; R-PRESET-3 ✅ DR-PRESET-3; R-VST-6 ✅ DR-VST-6 — the wheels to a plugin wait; R-CTRL-1 ✅ DR-CTRL-1; R-CTRL-2 ✅ DR-CTRL-2 — not yet against a real surface; R-PERF-1 ✅ DR-PERF-1; R-PERF-2 ✅ DR-PERF-2; R-PERF-3 ✅ DR-PERF-3)

Each line is one ledger task (or two, a DSP half first); built in the analysis's phase order. Lines that an earlier
area already reserved keep that id and are detailed here. Every feature is a command line first (law 8), observable
in the model, and drawn eased (law 14); new sound is the DSP library's (law 1); a device, a file format, a driver or
a process is the host's (law 12).

**Phase 1 — finish a track safely and deliver it**
- **R-SAFE-1 Autosave.** While a song has unsaved edits, the service writes it beside the song every 60 s and after
  any edit that removes something (`<song>.slp.autosave`), atomically (a temporary file, then renamed); a save or a
  close removes it. `settings set autosave=<seconds|off>`.
- **R-SAFE-2 Recovery.** Opening a song whose autosave is NEWER than it offers it: `project open <path>` says so
  (`recovery: <path>.autosave from <time>`); `project recover <path>` opens the autosave as the song, unsaved;
  `project discard-recovery <path>` deletes it. Home marks the card; the window asks Recover / Open the saved one.
- **R-MIDI-1 MIDI files.** `pattern import <file.mid> [--track <n>] [--clip --at <b>]` makes a pattern (or a clip of
  one) from a Standard MIDI File (type 0 or 1; notes and velocities; the file's tempo map read for the beats);
  `pattern export <pt> <file.mid>` and `project export-midi <file.mid>` write one (a track per instrument, the
  song's tempo). Dropping a `.mid` from the browser onto a track is the import, one line. Round trip exact to the tick.
- **R-METER-1 Mastering meters** (R-EDM-15): the master's integrated, short-term (3 s) and momentary (400 ms)
  loudness in LUFS (ITU-R BS.1770-4, gated), true peak in dBTP (4× oversampled), stereo correlation, and a spectrum
  (1/6-octave, 20 Hz–20 kHz) — computed by the DSP library's meters on the audio thread without allocating,
  published in the model while playing and printed by `render --measure` for a render. The master's meter shows
  them (a Loudness panel opened from it); its numbers ease. (Built 2026-10-16: kept after a stop, so a played
  song's integrated loudness can be read; the panel opens from the master's LUFS chip — DR-METER-1.)
- **R-METER-2 Strip meters** (R-PLAY-3, completed): RMS beside peak, a held peak (1.5 s, then falling), a clip
  latch cleared by a click — on every strip and the master. (Built 2026-10-16: measured by the player, in samples, and
  published — a window cannot compute them from a peak per frame; falling 20 dB a second; the click is `meters
  clear` — DR-METER-2.)
- **R-EDM-4a A true-peak limiter**: the DSP Limiter gains `truePeak` (on: inter-sample peaks detected 4×
  oversampled), so a render with it on never exceeds its ceiling in dBTP, measured. (**AMENDED (a 4× detector read
  0.19 dB over on another 4× grid, 2026-10-16):** detected at 8× — 4× meters disagree on where their points fall, on
  the quarters or between them, by up to 0.19 dB on a 12 kHz sine 45° out; every ⅛ sample covers both. Built:
  measured under 10⁻⁵ dB over by the library's meter, −1.001 dBTP for −1 by an independent one — DR-EDM-4a.)
- **R-EXPORT-1 Delivery formats** (R-EDM-16): `render --out <file>` writes WAV, FLAC, MP3, OGG Vorbis or Opus by the
  file's extension (`--format` to override), at `--rate` and `--bits` (16, 24, 32 float), with TPDF dither when
  bits drop, and `--normalize <LUFS>` (measured integrated loudness to a target, the gain applied, the true peak
  kept under `--ceiling`); encoders are FFmpeg's, in the host. File › Export… is a sheet for the same line. (Built
  2026-10-16 — DR-EXPORT-1: `--rate` renders the song AT the rate, so no resampler is needed; `--kbps` sets a lossy
  file's rate; Opus is 48 kHz and MP3 goes to 48 kHz; an exact zero is not dithered, so silence stays silence.)
- **R-MIX-18 A side-chain in one gesture**: `strip duck <ch> --under <key-strip> [--amount <dB>]` inserts a
  Compressor (fast attack, release from the tempo, ratio for the amount) on `<ch>` keyed by a sidechain send from
  the key strip — ONE command (R-BROWSE-3's rule); right-click a strip › Duck Under ▸ <strip>. (Built 2026-10-16:
  the amount is the cut a FULL-SCALE key makes — a real kick cuts less; the key send is pre-fader — DR-MIX-18.)

**Phase 2 — play it in**
- **R-DEV-9 Low latency on PipeWire**: a native PipeWire output and input (beside PulseAudio, which stays the
  fallback): buffers down to 64 frames; the driver chosen in Settings › Audio (`settings set driver=pipewire|pulse`);
  every xrun counted and published (R-DEV-5). (Built 2026-10-16, the OUTPUT — DR-DEV-9; the input lands with the
  recording task N16; a failed PipeWire is said once and not retried until the driver is set again. Owed: a live
  check on a PipeWire server.)
- **R-MIDI-2 A MIDI keyboard** (R-EDM-10): MIDI input devices listed (`midi devices`) and enabled in Settings
  (ALSA sequencer, in the host); notes played sound through the ARMED instrument strip at once (`strip arm <ch>`),
  sustain pedal honoured; `transport record` with a strip armed records them into a new note clip at the play
  position (or into the clip under the playhead, `--overdub`), with an optional count-in and input quantize
  (`settings set recordQuantize=<grid|off>`). The notes land exactly where they were played (the audio
  callback's sample time, minus the output latency). (Built 2026-10-16 — DR-MIDI-2: the count-in clicks with the song
  waiting; a take lands at the stop as one undo; inputs chosen by `settings set midiInputs=`; arming is the session's.)
- **R-MIDI-3 Expression lanes**: pitch bend and any CC on a note clip as a lane in the piano roll (points, like
  automation), played into the instrument (the DSP instruments take pitch bend ±2 semitones and mod wheel →
  their filter or vibrato); recorded from the keyboard. (The DSP half built 2026-10-16 —
  DR-INST-6: the Basic Synth's bend ± its range and its wheel's vibrato, the Sampler's bend. The lanes built the same
  day — DR-MIDI-3: `bend` and `cc0`…`cc127` (`mod` = `cc1`), linear between points, at rest after their clip; the
  instruments play bend and cc1; every lane goes through MIDI files.)
- **R-ROLL-11 Note tools**: velocity humanise and timing humanise (seeded, so a render is pure), note
  probability (seeded per note), groove templates beside swing, scale lock (a key and a scale snap new and moved
  notes), chord stamps (a chord placed in one click), legato (notes stretched to the next), and
  `pattern arpeggiate <pt> --rate --mode up|down|updown|random --octaves` (a MIDI effect written as notes).
  (**Detailed while building it, N12 — 2026-10-16.** Each tool is ONE edit on the pattern, on its notes or on the
  ones named by `--notes "<pitch@beat> …"` (the roll's selection), and a seed makes it reproducible: the same line on
  the same notes writes the same notes. *Probability* is the one decided while playing: a note's `chance` (0 … 1,
  1 unless set) is rolled per clip and per repetition from a hash of the clip, the repetition, the note's pitch and
  tick — no clock — so the render stays pure, two clips of one pattern vary apart, and `project export-midi` writes
  what the render plays. *Scale lock* is the pattern's (`pattern scale <pt> <key> <scale>` | `off`): a note PLACED
  (`note add`, its `--chord`, `notes add`) lands on the nearest pitch of the scale — a tie goes down, so a major
  chord stamped on any degree becomes that degree's own triad — and a note MOVED in pitch (`note move --to-pitch`,
  `notes move --semi`) goes on in the direction it was moved to the next pitch of the scale; a move in time only, an
  import, a step row, a recording and `pattern transpose` keep their pitches. *Grooves* sit beside swing on
  `pattern quantize --groove <name> [--strength]`: a named template of per-step offsets and accents, empirical and
  listed in the API document. *Humanise* moves each start within ± `--timing` beats and each velocity within
  ± `--vel`. *Legato* ends every note where the next start begins (a chord's notes together), the last at the
  pattern's end. *Arpeggiate* plays, on every `--rate` step of the beat grid, the next pitch of what is held there
  (its pitches over `--octaves`, up, down, up-down without repeating the turns, or a seeded random), each step
  `--gate` of a step long at the held note's velocity, replacing what it arpeggiated.)
- **R-INST-6 Glide**: the Basic Synth gains a mono / legato mode with portamento time (DSP). (Built 2026-10-16 —
  DR-INST-6: constant-time glide, linear in semitones; legato does not strike again.)
- **R-ARR-1 Markers and sections**: `marker add <name> --at <b> [--length <b>]`, rename, move, delete; drawn on the
  ruler, clicked to jump, double-clicked to loop the section; the model lists them. (**Detailed while building it, N13 —
  2026-10-16.** One suite (law 16): Interstellar's `#marker` — `mk_` ids, `name`, `at`, `note`, `marker add <name> --at`
  — in the song's beats and the audio schema's prefix, `#amarker`, with `length` added: a marker with a length is a
  SECTION; one without is a point whose section runs to the next marker, else to the song's end, published as
  `markers[].end` so no front end re-derives it. `transport seek <mk>` and `transport loop <mk>` reach them — the
  ruler's click and double-click, one line each.)
- **R-ARR-2 Time edits across the song**: `time insert --at <b> --length <b>` and `time delete --from <b> --to <b>`
  move every clip, automation point, marker and loop after it — ONE edit. (**Detailed while building it, N14 — 2026-10-16.**
  What CROSSES an edit point is decided before anything moves. Inserting, a clip that starts before the point and
  runs over it stays where it is and is named (there is no `clip split` yet — R-EDIT-1); a section, a looping
  automation's span or the loop that holds the point grows. Deleting, what lies wholly inside the span goes (clips,
  automation points, markers); a clip that repeats its material — a note clip, or an audio clip set to loop — and
  starts before the span is shortened by the overlap, since what repeats keeps repeating; any other clip across an
  edge is REFUSED, naming it, and nothing changes. The loop is the session's, not the song's: a time edit moves it,
  and undo leaves it where it is. The ruler's right-click offers a bar opened or taken out there, and the loop's span
  taken out.)
- **R-ARR-3 Lane groups**: lanes folded into a named group (`lane group <ln>… --name`), the group collapsed or
  opened (eased); its clips moved together. (**Detailed while building it, N15 — 2026-10-16.** A group is organisation
  only, as a lane is (R-LANE-1): `#alanegroup id=lg_1 name=Drums [folded=on]`, each of its lanes naming it
  (`#alane … group=lg_1`), its lanes side by side in the lane order — `lane group` gathers them at the first one's
  place, `lane move <lg> --to <i>` moves the group whole, a lane moved out of its group's run leaves it and one moved
  into another's run joins it, a group left with no lane goes. `lane ungroup <lg>` lets its lanes go. Its name and
  whether it is folded are the song's (`set lg_1.name=`, `set lg_1.folded=on|off`). Folded, its lanes are ONE row
  showing all their clips; dragged there, they move together — `clips move "<ac> …" --by <beats>`, one edit, a
  grammar any front end can use for a selection of clips.)

**Phase 3 — work with audio**
- **R-REC-1 Record audio** (R-EDM-11): an input port monitored through an armed audio strip (`strip arm <ch>
  --input <port>`, `set <ch>.monitor=on|off|auto`); `transport record` writes the input to a new WAV beside the song
  and makes a clip of it where recording began, sample-aligned (the input latency compensated); punch in/out
  (`transport punch <from> <to>`) and loop recording making TAKES (R-REC-2). (**Detailed while building it, N16 —
  2026-10-16.** A strip's INPUT is the song's — `set <ch>.input=<port>|none`, an input port (`#aport … dir=in`) the
  machine's settings map to a capture device (`port.<name>=<device>`, R-DEV-3); `strip arm <ch> --input <port>` sets it
  and arms in one line. Arming stays the session's and one strip at a time (as R-MIDI-2). MONITORING is the song's —
  `set <ch>.monitor=auto|on|off` (`auto` unless set): `on` the input is heard through the strip — its rack, its fader,
  its meter — whenever it is armed; `off` never; `auto` while armed and the transport is stopped or recording, not
  while it plays the song back. RECORDING (`transport record [--from] [--count-in]` with an audio strip armed) keeps
  the input from the moment recording begins to the stop; at the stop it is written beside the song —
  `<song folder>/Recordings/<strip> <n>.wav`, 32-bit float, the next free number — and a clip of it is put on the
  strip where recording began, all ONE undo ("Record"); a song never saved is refused (there is nowhere beside it
  yet). ALIGNED: the take is moved earlier by the output's latency plus the input's (what was played was heard Lo
  after it was rendered; what was sung arrived Li after it was sung), both as the devices report them — a loop of the
  output back into the input lands on the beat it left. PUNCH: `transport punch <from> <to>` | `off` (the session's,
  as the loop): a recording keeps only `[from, to)` of the song and its clip starts at `from`. Loop takes are R-REC-2's
  (N17). The host's capture is a seam beside the output's (PulseAudio and PipeWire); without one, recording audio is
  refused by name.)
- **R-REC-2 Takes and comping**: each pass over a loop is a take on the clip; the comp is chosen by ranges
  (`clip comp <ac> --take <n> --from --to`), the takes shown under the clip. (**Detailed while building it, N17 — 2026-10-16.**
  Recording an audio line while the song loops makes ONE clip spanning the loop and a TAKE of each pass over it — what
  was heard there that pass (the latency taken out as R-REC-1 says, so the input heard just after the loop turned is
  the end of the pass before, not the start of the next) — each written beside the song as its own file; a pass that
  covers less than half the loop is dropped. The takes are the clip's, in the order they were played
  (`#aclip … ` then `#take src=…` lines), numbered from 1. The COMP is ranges of the song's beats, each naming the take
  that plays there (`#comp take=2 from=4.0 to=5.5`); where no range says, the LAST take plays. `clip comp <ac> --take
  <n> [--from <b> --to <b>]` (the whole clip without a range) is ONE edit — the range overwrites what it covers. A
  comped clip plays each range from its take, crossfaded 5 ms at each seam so a cut never clicks; a render of it is as
  pure as any. A recording with no loop is one take, as before.)
- **R-EDIT-1 Audio editing on the lanes** (R-EDM-12): `clip split <ac> --at <b>`; a clip's fades and gain dragged
  on the clip (its corners and a gain line; `set <ac>.fadeIn/.fadeOut/.gain` exist); overlapping clips on a lane
  crossfade by their fades; `set <ac>.reverse=on`; `clip consolidate <ac>…` (bounce in place to one audio clip);
  `clip slice <ac> --every <b>|--transients` making a Sampler kit or clips. (**Detailed while building it, N18 —
  2026-10-16.** SPLIT: `clip split <ac> --at <b>` cuts an AUDIO clip in two where `b` falls inside it — the first
  half keeps the clip's id and its fade in, the second is a new clip (its id said) from `b` with the fade out, each
  fade no longer than its half; the two play what the one did, to the sample (a fade across the cut aside). A
  reversed clip's first half is the END of its span; a clip with takes keeps every take in both halves and its comp
  divided at the cut. A looping clip is cut only where a repetition begins (the refusal names the nearest two); a
  note clip is refused — its notes are its pattern's, shared (R-CLIP-3). REVERSE: `set <ac>.reverse=true|false` (a
  boolean like every other) plays an audio clip's span backwards, `out` to `in`, its takes too (`#aclip … reverse=true`). CROSSFADES: where two audio
  clips on one lane, playing through one strip, overlap — the later beginning inside the earlier and ending after
  it — the earlier's fade out and the later's fade in each last at least the overlap (linear in amplitude,
  R-MIX-11), so one passes into the other without a click; a clip wholly inside another is mixed, not faded. What is
  heard is published (`clips[].fadeInHeard`, `.fadeOutHeard`), the clip's own fades left as set. CONSOLIDATE:
  `clip consolidate <ac> <ac>…` joins audio clips of ONE strip into one: what they play — gains, fades, crossfades,
  reverse, comps — rendered raw (before the strip's devices, fader and pan), from the first one's start to the last
  one's end, written beside the song (`Bounces/<strip> <n>.wav`, 32-bit float) and put where they were, on the
  first one's lane — one edit. A note clip is refused (its sound is its instrument's: a strip's bounce is
  R-PERF-3). SLICE: `clip slice <ac> --every <b>` cuts an audio clip into clips every `b` beats from its start;
  `--transients [--sensitivity 0…1]` where the DSP library's onset detector (REQ-onset-1) finds a sound begins in
  what the clip plays — one edit, the new clips said. `--every <b> --kit` instead makes a new instrument track: a
  Sampler of the clip's file playing its span in N equal slices (`mode=slices`, `slices`, `start`/`end` the span; N
  the whole number of slices nearest the clip's length over `b`, 1 … 64) and a note clip where the clip was, whose
  notes — root + k at the k-th slice's place — play the slices in order, so the kit plays what the clip did; the
  clip stays (mute or delete it). A reversed clip's kit plays its slices reversed, in its order. A looping clip is
  not sliced; a comped one only `--every` (consolidate it first for its transients).)
- **R-WARP-1 Time-stretch** (R-EDM-13, R-CLIP-5): an audio clip follows the song's tempo (`set <ac>.warp=on`, its
  own BPM detected on import or set, `.bpm`), pitch kept; `set <ac>.pitch=<semitones>` shifts pitch, tempo kept —
  a DSP time-stretcher (phase vocoder with transient handling), rendered deterministically. (**Detailed while building
  it, N20 — 2026-10-16.** An audio clip carries `warp` (on/off), `bpm` (its own tempo; 0 unknown) and `pitch`
  (semitones), written on its `#aclip` only when set. WARPED, a clip lasts its span's own beats — (out − in)·bpm/60 —
  whatever the song's tempo, its span stretched by bpm / the song's (pitch kept, REQ-stretch-1, attacks kept,
  REQ-stretch-3); PITCHED, its span is moved `pitch` semitones, its length kept (REQ-stretch-2); both may hold. Turning
  `warp` on with no `bpm` detects it in the same edit (REQ-tempo-1 on the clip's span; nothing heard refuses, naming
  `.bpm`); `clip add --src <file> --warp` drops a file warped, detected on import. `set <ac>.bpm` (20 … 999) and
  `set <ac>.pitch` (−24 … +24) are its addresses; a note clip or one comped from takes refuses them (consolidate a comp
  first). A stretched span is made once (per file, span, ratio and pitch) and kept while it is used; a song tempo
  change makes it again for the new ratio; a render at another rate stretches at that rate. As every render, pure. A
  warped clip cut (`clip split`, `clip slice`) cuts its span at its own beats — each half stretched apart, so a window's
  worth either side of the cut may differ from the whole; `--kit` of a warped or pitched clip is refused (consolidate
  first). The model publishes `clips[].warp`, `.bpm`, `.pitch`.)
- **R-TEMPO-1 A tempo map** (R-EDM-14): `tempo add --at <b> --bpm <v> [--ramp]`, the ruler and every beat↔second
  conversion follow it; render, playback, the metronome, warp and MIDI export agree; automating `project.bpm`
  writes it. (**Detailed while building it, N21 — 2026-10-16.**
  The song's tempo is its `bpm` from beat 0 and a CHANGE at a later beat (`#atempo id=tp_1 at=16.0 bpm=140.0
  ramp=true`): from there on that tempo — or, `ramp`, reached there gliding linearly (in beats) from the tempo before.
  `tempo add --at <b> --bpm <v> [--ramp]` (prints its id), `tempo move <tp> [--at] [--bpm] [--ramp true|false]`,
  `tempo delete <tp>`; one change to a beat, after beat 0, 20 … 999 bpm. Every beat↔sample conversion goes through
  the map — clips, notes, fades, the wheels, the render's range, playback's start, seek, loop and count-in, the time
  bar heard, the metronome's beats, a recording's notes and takes, formulas' `beat` and `bpm`, a warped clip's stretch
  (its own beats heard where the song puts them — the DSP's time map, REQ-stretch-4). A curve stays in BEATS (it is
  read at the song's beat). A song of one tempo renders the bytes it did. `time insert|delete` carry the changes as
  they carry markers. The MIDI file writes each change (a glide as a step every sixteenth). The model publishes
  `tempos[]` (each with where it is heard, in seconds) and `transport.tempo`, the tempo where the time bar is.
  **AMENDED (one model of tempo — law 16; the same day):** "automating `project.bpm` writes it" reads as: the tempo map
  IS `project.bpm`'s automation — `auto create project.bpm` and a formula on it are refused, naming `tempo add`; the
  window edits the changes as a lane of points (part 3), so there is never a second, competing curve of the tempo.
  "The ruler follows it" (part 3, the same day): the ruler stays in bars and beats (the song's time is beats); each change
  is a flag on its band, a glide drawn as a slope, dragged, retyped, glided or deleted there; the song bar shows the tempo
  where the time bar is and a drag on it changes the change in effect there (`transport.tempoId`).)

**Phase 4 — open the ecosystem**
- **R-PRESET-1 Device presets** (R-EDM-20): `preset save <dv> <name>`, `preset load <dv> <name>`, a preset store
  per device type in the machine's data folder (factory presets shipped beside it); the browser's Presets tab
  lists and drops them (R-BROWSE's preset half). (**Detailed while building it, N22 — 2026-10-16.** A PRESET is a
  device's set values (the registry's names and texts, as the `.slp` keeps them, and a sampler's sound) under a name,
  in a text file `<folder>/<type>/<name>.slpreset`. Two folders: the USER's — the machine's data folder
  (`$XDG_DATA_HOME/solaris/presets`, `SOLARIS_PRESETS` overriding, as the settings) — and the FACTORY's, shipped with the
  app and never written. `preset save <dv> <name> [--replace]` writes the user's (an existing name refused without
  --replace; a name with a path in it refused); `preset load <dv> <name>` sets the device to it — every value back to
  the registry's default first, then the preset's, ONE edit, each value checked as `set` checks it (a preset of another
  type refused); the user's preset of a name wins over the factory's. `preset list [<type>]` prints them; `preset delete
  <type> <name>` deletes a user's (a factory's refused). A drop is one line: `device add <ch> --type <t> --preset <name>`
  and `clip add --instrument <t> --preset <name>`. Saving and deleting change the machine, not the song (no undo, no
  unsaved mark). The model publishes `presets[]` (type, name, factory) for the browser.)
- **R-PRESET-2 Song templates**: `project new <path> --template <name>`; File › Save as Template. (**Detailed while
  building it, N23 — 2026-10-17.** A TEMPLATE is a song kept to start others from: `<folder>/<name>.slp`, in the user's
  templates folder (`$XDG_DATA_HOME/solaris/templates`, `SOLARIS_TEMPLATES` overriding) or the factory's shipped with the
  app (never written). `template save <name> [--replace]` writes the open song there as it is — its strips, devices,
  lanes, patterns, clips, markers, tempo — every file it names (a clip's, a sampler's) made absolute, so a song started
  from it anywhere finds them; an existing name refused without --replace, a name with a path in it refused.
  `project new <path> --template <name>` starts a song from it: the template's song with the new song's name and id
  (from its path, or --name), its tempo, meter and rate unless --bpm, --sig, --rate say; the user's template of a name
  before the factory's. `template list`, `template delete <name>` (a factory's refused). Saving and deleting change the
  machine. The model publishes `templates[]` (name, factory) for the window.)
- **R-PRESET-3 A sample library**: tags and favourites on files (`sample tag <file> <tag>`), search by name and tag
  (`browse --search`), in the browser. (**Detailed while building it, N24 — 2026-10-17.** The LIBRARY is the machine's
  (never a song's): a text file in its data folder (`$XDG_DATA_HOME/solaris/library`, `SOLARIS_LIBRARY` overriding), a
  line per file that has tags — its absolute path, then its tags. A tag is a word of 1 … 32 letters, digits or dashes,
  kept in lower case; `favourite` is one like any other, with its own command. `sample tag <file> <tag>`, `sample untag
  <file> <tag>`, `sample favourite <file> [on|off]` (on when not said) — no undo, no unsaved mark. `browse --search
  <text>` finds audio files whose name holds the text (any case) or that carry a tag starting with it, in the sample
  folders (or `<folder>` when given) and their sub-folders — eight levels down, the first 200 found — and the
  library's tagged files wherever they are; `browse --favourites` lists the favourites. Found files are the browser's
  entries (`browser.search` says what was searched). The model publishes `library[]` (path, tags, favourite).)
- **R-VST-6 Hosting other makers' VST3** (R-EDM-17): `plugins scan` lists `~/.vst3` and the system folders;
  `device add <ch> --plugin <id>` loads one, each in its OWN process (`solaris-plughost`), audio exchanged through
  shared memory a block at a time; a plugin that crashes or overruns its budget goes offline (its strip passes
  dry, said in the model) and never takes the song with it; its latency compensated (R-MIX-17); its parameters
  addressable and automatable; its own editor in a window (the host's X11 embed). (**Detailed while building it, N25 —
  2026-10-17**, the design in discussion.md Round 9. Scanning: `plugins scan` reads `SOLARIS_VST3_PATH` (colon-separated)
  or else `~/.vst3`, `/usr/lib/vst3`, `/usr/local/lib/vst3`, each bundle in a helper process of its own
  (`solaris-plughost --scan`) so a module that crashes while read loses only itself, said by name; the list — each class
  with its id (UID), name, vendor, instrument or effect, bundle — is the machine's, kept in its data folder
  (`SOLARIS_PLUGINS` overriding), printed by `plugins list`, published as `plugins[]`. A plugin in a song is a device of
  type `vst3` naming its class and bundle (`device add <ch> --plugin <id>`; an instrument's as the strip's first, as
  the registry's); its parameters are the plugin's, numbers 0 … 1 by id (`p<id>`) — set, read and bound as any; its
  state kept in the song as the plugin gives it. One helper process per plugin instance plays it — audio and events
  through shared memory a block at a time, the audio thread waiting at most the block's duration; a render runs its
  own instance and waits. Its latency compensated (R-MIX-17). The helper dying or overrunning takes it offline (an
  effect passes dry, an instrument is silent, the model says why); a missing bundle too, the song still opening.
  (**Detailed further while building it, N25 part 2 — 2026-10-17:** an instrument plugin is an instrument strip's —
  `strip add --kind instrument --plugin <id|name>` and `clip add --plugin` (one drop, R-BROWSE-3); `device add --plugin`
  takes an effect and refuses an instrument naming the line that takes it. A plugin is named by its id or its name in
  any case. Live, a block may wait its own duration, an engine block's (128 frames) at least — the engine splits at
  every note, so a plugin's notes and value changes go with the next block, at its start; a render waits up to 60 s a
  block. Offline is for good — the helper ended then and there — until `device reload <dv>` (not an edit) starts a new
  one from the last state; a plugin that goes offline is said once (`plugin.offline device why`), shown in
  `devices[].offline/why` and in `audit`; a render says which were offline, and so silent or dry in its file. An effect
  gone offline passes its input still as late as its latency (the engine compensated it), never early. The song's
  set values win over its state: a plugin is given its state, then the values the song sets; its state is asked for
  whenever the song or its autosave is written. A note preview and the input monitor play their own instance (another
  stream), never the song's. A plugin's presets are its own (`preset save|load` refused). Not yet: the pitch and mod
  wheels are not sent to a plugin (VST3 maps them to parameters through the plugin's own MIDI mapping — a later task).)
  (**Detailed while building it, N26 — 2026-10-17:** a plugin's OWN editor — `device editor <dv>` opens it in a window
  of its own made by the helper (the plugin's view in a top-level X11 window titled "<plugin> — <strip>", driven by the
  helper's run loop: the plugin's own process, so an editor that crashes takes only its plugin offline); `device editor
  <dv> --close` closes it, and so does the window's own close. Not an edit. The helper plays on a thread of its own, so
  an editor never delays a block. What is turned in the editor reaches the processor at once (the helper's next block)
  and the song as `set <dv>.p<id>` — consecutive ones one undo step, as a dragged fader's; a plugin that changes its own
  values (a preset of its own) puts the song's stored ones to what they became. The model: `devices[].editor` (open),
  said by `plugin.editor device open`. The device panel (part 2): a plugin device shows its parameter list (no face),
  an Editor button, and — offline — a banner saying why with Reload (`device reload`), each eased. With no display the
  editor is refused, saying so; tests never open a window on a display.)
- **R-CTRL-1 MIDI learn**: right-click a number › Learn MIDI CC — the next CC moved binds it (`map add <cc>
  <address>`); mappings are the machine's settings; the transport keys of a keyboard (MMC) work.
  (**Detailed while building it, N27 — 2026-10-17:** a CONTROL is a controller number — `<n>` (0–127, on any channel)
  or `<ch>/<n>` (on channel 1–16 only). `map add <cc> <address>` binds it to any NUMBER a formula can drive (a strip's
  gain or pan, a send's gain, the master gain, a device's numeric parameter, a plugin's `p<id>`), one address per
  control (added again: moved); `map remove <cc>`; `map list`; `map learn <address>` waits for the next controller
  moved and binds THAT one (said, `map.learned`; its value is not taken), `map learn --cancel` stops waiting. Mappings
  are the machine's settings (`map.<cc> = <address>` in its file) — not an edit, never in the song; one naming what the
  open song does not have does nothing, and `audit` says so. A mapped control moving sets its address — 0 … 127 over the
  address's travel: a gain by the mixer's fader law (the dock's, now one copy for both), a device's parameter by the
  registry's own normalisation (its taper, as a knob), anything else linearly — as `set <address>=<value>`, landing as
  a plugin editor's edits do (consecutive ones one undo step). MIDI Machine Control (a keyboard's transport keys):
  Play and Deferred Play → `transport play`, Stop and Pause → `transport stop`, Record Strobe → `transport record`,
  Rewind / Fast Forward → a bar back / on (`transport seek`). The MIDI input opens while there are mappings or a learn
  waits, as for an armed strip. The model: `settings.mappings[]` (cc, address, inert) and `mapLearning`. In the window
  (part 2): a number's menu offers Learn MIDI CC — and Forget CC <n> when it has one; while a learn waits the song bar
  says so; the Settings sheet's MIDI page lists the mappings, each removable.)
- **R-CTRL-2 Control surfaces**: a Mackie Control / HUI surface's faders, pans, mutes, solos and transport follow
  and move the mixer.
  (**Detailed before building it, N28 — 2026-10-17**, the design in discussion.md Round 10: `settings set
  surface=mcu|hui|none` and `surfacePort=<port>` — the machine's; the surface's port is its own, in and out, and the
  keyboard never listens to it. Eight channel strips over the song's strips in processing order, from a bank the
  surface moves (bank: eight, channel: one — the session's). In: a fader → `set <ch>.gain` by the fader law, the master
  fader → `project.masterGain`, a pot → the strip's pan (0.02 a step), mute / solo → `set <ch>.mute|solo` toggled, arm
  → `strip arm` (again: disarm), Play / Stop / Record / Rewind / Fast Forward → `transport …` as MIDI Machine Control's
  are. Out — only what changed: each fader at its strip's gain (not one a hand is on), mute / solo / arm lights, each
  pot's ring at its pan, each strip's name on the text (Mackie's scribble strip; HUI's four-character names); a slot
  past the last strip at the bottom and dark. HUI's ping is answered. The model: `settings.surface`,
  `settings.surfacePort`, and `surface` (connected, its bank). Not yet: values on the text, the jog wheel, function
  keys, several surfaces.)
- **R-PERF-1 A CPU meter**: the audio thread's load (DSP time over the block's time) and per strip, published and
  drawn in the song bar. (**Detailed while building it, N29 — 2026-10-17:** the load is the time the audio thread spends
  on a block — everything but waiting for the device — over the block's duration, read on that thread from a clock
  before and after (no allocation); smoothed over about a third of a second, and its worst block since the model last
  looked kept too (over 1 is a block that missed its deadline). A strip's share is the time its own processing took
  in those blocks (the engine adds it up only when asked — a render is never timed: it has no deadline). Published while
  playing: `transport.cpu`, `transport.cpuPeak`, `strips[].cpu`; stopped, 0. Drawn in the song bar before the master
  meter: "CPU" and its percentage over a thin bar, easing as a value catching up, the destructive colour from 80 %.)
- **R-PERF-2 A multi-core engine**: strips that do not feed each other render in parallel on a worker pool,
  byte-identical to one thread (R-RENDER-1). (**Detailed while building it, N30 — 2026-10-17:** a strip NOTHING feeds — no
  route, send or key into it — does its own work (its clips or notes, its instrument, its rack) before the others, on
  a pool, in any order: nothing it reads is written by another strip. Then everything else runs as on one thread, in
  processing order — the fed strips' own work, and every strip's fader, pan, sends, keys and outputs — so each sum is
  added in the same order, and the bytes are one thread's. Workers take the audio thread's floating-point mode. The
  audio thread takes items too and never waits to START (a worker still leaving an old job: it works alone); it waits
  only for the items it handed out. `settings set threads=auto|1…16` — the machine's (auto: the cores but one, at most
  8); live playback and a render both use it. Measured: a render and live playback, one thread against several,
  byte-identical; an engine run with its free strips in reverse order, byte-identical.)
- **R-PERF-3 Freeze / bounce** (R-EDM-18): `strip freeze <ch>` renders the strip to audio and plays that instead
  (its devices idle); `strip unfreeze`; `strip bounce <ch>` makes the audio a clip on a new strip. (**Detailed while building it, N31 — 2026-10-17:** what is rendered is the
  strip's OWN sound — its clips or notes through its instrument and rack, its devices' automation with them — at unity,
  centred, before its fader: from the song's start to its end and the tail after (until it falls under −90 dB, 10 s at
  most), 32-bit float, beside the song (`Frozen/<strip> <id>.wav`; a bounce: `Bounces/<strip> <n>.wav`). Frozen, the
  strip plays that file from the start instead — its clips, notes and devices kept but idle (not built: they cost
  nothing) — and its fader, pan, mute, solo, sends and output work as before; the song sounds as it did (to float
  precision). Only a strip NOTHING feeds can be frozen (its sound is its own); the file is beside the song's (an open
  song always has one — `project new` writes it). `strip unfreeze <ch>` plays its own work again (the file stays: an undo may want it). Both are edits (one undo
  each). `strip bounce <ch>` puts the same render as an audio clip at beat 0 on a NEW strip (R-MIX-2's rule for a new
  file), the strip itself untouched. The model: `strips[].frozen` (the file, "" none); `audit` says a frozen strip's
  edits are not heard until it is unfrozen. **Detailed while building it, N31 part 2 — 2026-10-17:** in the mixer, a
  strip's menu offers "Freeze" and "Bounce to a new strip" when nothing feeds it (`strips[].fromStrips` empty — the
  service's answer, not the widget's), "Unfreeze" when it is frozen — each one line. A frozen strip's rack recedes (its
  devices idle) under a "FROZEN" pill in the accent; both ease in and out, never snap. A freeze renders before it
  answers, as an export does.)
- **R-DEV-1/2/4 Several devices** (R-EDM-19, P2): a port on a second device played clocked to the first,
  drift-corrected (R-DEV-2), latency aligned (R-DEV-4). (**Detailed before building it, N32 — 2026-10-17, discussion
  Round 11:** a FOLLOWER is every device other than the clock that an output port of the playing song is mapped to;
  its own stream and thread, fed by the audio thread through a lock-free ring; a follower that cannot open is silent
  and said once, the song plays on. The DSP library's `AdaptiveResampler` (windowed sinc, 64 taps, a ratio changeable
  every block without a click) and `FillController` (a PI loop holding the ring's fill at its target — its integral
  the drift, in ppm, clamped at ±2000) keep it on the clock: two crystals 1000 ppm apart for simulated minutes hold
  the fill within a few frames, never underrun, and read the drift to under 1 ppm. Every port is heard at the same
  moment (within a millisecond, measured): a follower's fill target is its delay; a clock path faster than a follower
  is delayed on the clock's channels, set at play. An input on another device is held to the clock the same way (the
  input seam says what it has buffered). Seen while playing: `transport.devices[]` — id, role (clock | follower |
  input), latency, drift ppm, xruns, why silent — and in Settings › Audio. Not yet: a follower at a rate unlike the
  song's; re-aligning after play.)

**Phase 5 — grow the sound and the craft**
- **R-INST-7 Wavetable synth** · **R-INST-8 FM synth (4 operators)** · **R-INST-9 Multi-sample instrument**
  (SFZ import, velocity layers, round robin) · **R-INST-10 Granular sampler** · **R-INST-11 808** (a tuned bass
  drum with glide and drive) — each a DSP instrument with a registry entry (law 1).
- **R-FX-6 Multiband compressor** · **R-FX-7 Gate / expander** · **R-FX-8 Transient shaper** · **R-FX-9 De-esser**
  · **R-FX-10 Saturator / bitcrusher** · **R-FX-11 Phaser** · **R-FX-12 Flanger** · **R-FX-13 Stereo imager**
  (width, mono below a frequency) · **R-FX-14 Convolution reverb** (an impulse-response file) · **R-FX-15 Pitch
  shifter** · **R-FX-16 Vocoder** (a sidechain carrier) · **R-FX-17 Tuner** (a meter, not a sound) — each a DSP
  effect with a registry entry.
- **R-MIX-19 Group / VCA faders**: a VCA moves the gain of the strips assigned to it, adding its dB.
- **R-MIX-20 Per-strip tools**: phase invert, width, mono check on the master; solo-safe on buses.
- **R-VER build** (R-VER-1…4, specified): versions of a song.
- **R-COLLECT-1 Collect and save**: `project collect` copies every file the song uses into its folder and rewrites
  the paths; a relink window for missing files (`clip relink <ac> <file>`, or a folder searched).
- **R-LAUNCH-1 A clip launcher**: a session grid (scenes × tracks) whose slots start in time with the song, beside
  the arrangement; launched clips recorded into the arrangement.
- **R-VIDEO-1 A video lane**: a video file shown in a window, synced to the transport (decoded by the host's
  FFmpeg), with an offset; a render can carry it into the export.
- **R-COLLAB-1 Sharing a song through a folder**: no server — a song saved into a shared (or Git) folder; when the
  file changed under an open song, its edits and the other's are merged node by node (by id), a conflict on one
  node asked about.
- **R-ACCESS-1 Accessibility**: a UI scale setting (eased), a high-contrast theme, every control's name and value
  published for assistive technology (AT-SPI through the host), all of it reachable from the keyboard.
- **R-ACCESS-2 Localisation**: every UI string from a table; a language setting.
- **R-ASSIST-1 Analysis tools (signal processing, no AI model)**: harmonic / percussive separation of an audio
  clip into two clips; monophonic audio-to-MIDI (pitch tracking); chord detection of a clip or the song
  (chroma), shown on the ruler.

## Reserved areas — 📋 SPECIFIED, not scheduled

- **R-REC** recording from input ports into the armed strip, sample-aligned (was SR-REC-1..3,
  SR-ROUTE-4). (**Specified 2026-10-16 — R-REC-1/2 in R-PRO.**)
- ~~**R-AUTO** automation curves on any registry parameter and strip gain/pan (was SR-AUTO-1..3).~~
  Specified 2026-10-09 — see R-AUTO above.
- **R-MIDI** CC lanes, per-note expression, SMF import/export (was SR-MIDI-1..3). (**Specified 2026-10-16 —
  R-MIDI-1/2/3 in R-PRO.**)

---

## R-FMT — the `.slp` project — ✅ IMPLEMENTED (DR-FMT-1; R-FMT-3's offline flag: `clips[].offline` and `audit`, DR-MIX-8/9/10)

- **R-FMT-1 A `.slp` is a text document in the suite grammar** (`arstro-project = 1`, `app = solaris`,
  `#type id=… key=value`), normative in [`docs/project-format.md`](docs/project-format.md), and its
  audio nodes are the suite schema's ([`../../docs/audio-format.md`](../../docs/audio-format.md)),
  extended there — additively, so an Interstellar `.isp`'s audio means what it meant (was SR-FMT-1/2).
- **R-FMT-2 Parse → serialize is a byte-exact fixed point;** unknown keys and nodes survive (was
  SR-FMT-4).
- **R-FMT-3 Media is referenced, never embedded:** a path relative to the project, offline-flagged
  if missing (was SR-FMT-3; its content hash is deferred to R-VER-5, where merging needs it).
- **R-FMT-4 A structural error refuses, a numeric corruption repairs** (Interstellar's rule).

## R-SVC / R-API — the service and its document — ✅ IMPLEMENTED (DR-SVC-1…3, DR-API-1) — rung 5 — 5–9 ✅ (control channel + attach, equivalence, web, composition, the song: DR-SVC-5/7/8/9) — 10, 11 ✅ (the Prelude made in the window, 549 of 549 notes; its UX audit, U-1…U-10: DR-SVC-10/11) — 12 ✅ (Perihelion, an original EDM track measured as a master: DR-SVC-12)

- **R-SVC-1** `SolarisService`: `dispatch(Command)` / `pump` / `model()` / an `Event` sink. The GUI,
  `solaris-cc`, a script and an agent send the same text lines (was SR-CLI-1).
- **R-SVC-2 One way in, one way out, one codec.** The grammar is a table; `formatEvent()` output is
  the log line.
- **R-SVC-3 An unknown command, flag, address or parameter is REJECTED, naming it** with the nearest
  candidates, and lands in `lastError` (was SR-CLI-2).
- **R-SVC-4 The core carries no codec, no device API and no OS path.** Decoders, writers, devices,
  directory listing and the settings path are injected by the host.
- **R-API-1 The API document is generated, committed and drift-tested** — rung 4 — and includes the
  device registry (every instrument and effect parameter with unit and range).

- **R-SVC-5 An agent can drive the running window** (user request, 2026-10-09: *"Agent friendly, make it
  like cosmo, core backend and front end can be cli (for Agent), GUI for user and web"*): `solaris
  --control <socket>` opens a line channel on the live window (cosmo's `ControlChannel`: it moves lines,
  never parses them); `solaris-cc attach <socket> [--script f]` sends command lines and prints the
  event lines — the user watches the song being made.
- **R-SVC-6 One script, two faces, one answer** (arstro.rule §1's equivalence test): a committed script
  run headless (`solaris-cc --script`) and through a live window (`attach`) ends in the same `state
  print --json --stable` and the same event stream.
- **R-SVC-7 A web face**: Solaris is an Arstro Remote app over NTWB (`solaris-cc ntwb serve | install |
  uninstall | api`, the `core/Ntwb` library, cosmo's worked example, `arstro.ntwb.implement`); its web UI
  (`apps/solaris/web`) shows the song — transport, lanes and clips, strips and their levels — and sends
  the same command lines. The service is the model; the web draws it.
- **R-SVC-8 Composition is agent-sized**: whatever an agent needs to write a whole song in few,
  readable lines — bulk note entry with note names, the ids a command made printed back, a song
  overview — specified from an audit of an agent composing with the CLI (2026-10-09), with a guide,
  `docs/AGENTS.md`, that ends in a worked song. (**Detailed from the audit, 2026-10-09** — an agent's
  16-bar song took 110 commands, 64 of them single notes, every id guessed:)
  - **a session that lasts**: `solaris-cc shell [--song <f>]` reads lines from stdin into ONE service
    (undo kept); a one-shot call that would drop unsaved edits says so and exits 4 unless `--discard`;
  - **scripts that say where**: a refusal names its `line N`; `#` comments may end a line; a comment or
    blank line prints nothing (it reprinted the last output — ids came out twice); `--keep-going`;
  - **everything made is said**: a creating command prints its id first, then `made: strip=… device=…
    pattern=… lane=…` for whatever else it made, and each made node is announced (`project.changed`);
  - **notes in bulk, by name**: `notes add <pt> "<pitch>@<beat>[:<length>[:<vel>]] …"`; a pitch anywhere is
    a number, a name (`C4`, `F#3`, `Bb2`; C4 = 60) or a kit's pad (`kick`); `note add --chord <Cm7>`;
    `pattern steps <pt> --pitch <p> "x...x...x...x..."`; `pattern duplicate | clear | delete | transpose`;
  - **arrangement**: `clip duplicate <ac> --count N`; a `clip add` with no `--lane` goes on the lane its
    strip's clips are already on (a new lane only for a strip with none) — AMENDS ClipAdd's "a new lane";
  - **reading back**: `ls` (a tree of ids and names), `show <id>` (a node's non-default fields),
    `pattern print <pt>`, `state print --json --compact` (no registry, no defaults);
  - **honest values**: a device parameter out of its range is REFUSED as a strip's gain is (R-DSP-3), not
    clamped in silence; the audit names notes past their pattern's end, empty and unused patterns.
- **R-SVC-9 The goal, measured** (user request: *"the goal is you can make me a song that you know"*): an
  agent makes a whole song with `solaris-cc` alone; the script is committed, the render measured (not
  silent, under 0 dBFS, the kick on the beats, the tempo right). The tune is public domain, so the
  result can be shared.
- **R-SVC-10 The second song, made in the window, measured against its score** (user request, 2026-10-15:
  *"make a song yourself … that you can replicate 99% … the goal is a production ready solaris app"*):
  J. S. Bach's Prelude in C major, BWV 846 (1722, public domain), on the piano (R-INST-5), made through the
  window's gestures — the roll, the mixer, the transport — not the CLI. Measured: the pattern diffed against
  the score note by note (pitch, start, length) — 99 % or better, the goal every note; the render: each
  bar's pitch classes match that bar's chord, not silent, under 0 dBFS, the tempo right. Committed with
  its score table and its measurement, as the canon is.
- **R-SVC-11 The UX found by making it** (same request: *"during making that song, analyze the UX, if any
  point that don't make sense to user fix it"*): every place the song's making stumbles — a gesture missing,
  a result a user would not expect, a step that takes ten when it should take one — is written down as it
  happens in `docs/ux-audit.md` (U-n: what was being done, what got in the way, what a user expects, how
  bad) and fixed in its own task with its own requirement, or deferred with the reason.
- **R-SVC-12 An EDM track, original, measured as a production** (user request, 2026-10-15: *"please make an edm
  song, edm require more advance sound, so it can be a test for you"*). An original track — its progression,
  melody and arrangement written for it, no one else's song — made with `solaris-cc` alone (as R-SVC-9), its
  script committed, its sounds the DSP library's: an intro, a build with a riser and a snare roll, a drop with a
  wide supersaw, a sub and a sidechained mid bass under a four-on-the-floor kick and a crash, a breakdown, a
  second build and drop, an outro. Measured as a master is judged, not only "not silent": integrated loudness
  and the drops' short-term loudness, true peak under −1 dBTP, the drops louder than the breakdown, the low end
  near-mono and the highs wide, the sidechain's pump in dB, the kick on every beat of the drops, the notes in
  the key, every section's energy where the form puts it. A sound the library lacks for it is made there first.

## R-UI — the screens — 🚧 IN PROGRESS (1, 2, 3, 5, 6, 7 ✅ DR-UI-1/3/5/6/7/8/9 — 3 without the version chip, X1; 4 partly: the Artboard move is T1) — 10 ✅ (the grid, DR-UI-10), 11 ✅ (ids shown and copied, DR-UI-11) — 12 ✅ (the analog face, DR-UI-12) — 13 ✅ (the tempo set in the song bar, DR-UI-13) — 14 ✅ (the resize cursor, DR-UI-14)

- **R-UI-1 Two screens, as Interstellar:** Home (R-HOME) and the Project view.
- **R-UI-2 The accent is teal, `#159387`, the only forked token** — teal because orange, the
  "solar" reading, sits beside record-red and solo-amber, which a DAW cannot afford to confuse.
  (**AMENDED 2026-10-08, measuring it:** this said `#5AEDDE`, Interstellar's recipe — cosmo's blue
  rotated to H 174° at the same S 80 %, L 64 %. The recipe holds HSL LIGHTNESS, which is not
  luminance: at H 174° that lightness is a relative luminance of 0.71 against cosmo blue's 0.23, and
  white text (cosmo's `primaryForeground`, not forked) on it reads at 1.4:1. The hue stays; the
  lightness drops to cosmo blue's LUMINANCE — `#159387`, 0.228, white text at 3.8:1 against cosmo's
  3.7:1 — so every chip, button and fill that puts white on the accent stays as legible as cosmo's.)
- **R-UI-3 The Project view:** Cosmo's menu strip and wordmark with the version chip; the browser on
  the left (R-BROWSE); lanes in the centre (R-LANE); **the mixer docked under the timeline** with a
  tab per mixer and a Matrix tab (R-MIX); the transport (R-TIME-4) with the master meter and device
  status. Docked, not a separate screen, because linked selection (R-MIX-8) needs both in view.
  (**AMENDED (user request, 2026-10-09):** *"open setting need to be at top left next to home and have
  full option like cosmo and interstellar"* — the song bar's left reads: wordmark · Home · Settings ·
  cosmo's `MenuStrip` with **File** (New, Open, Save, Save As, Render, Render Stems, Home), **Edit**
  (Undo, Redo, Duplicate, Delete), **Song** (Add Mixer, Add Line, Add Lane, Quantize), **View** (the
  mixer dock, the browser, the metronome). Settings is no longer at the right. Every item is a command
  line or a host picker.) (**As built (audit, 2026-10-09):** Song reads Add Mixer, Add Bus, Add Audio
  Line, Add Lane, Quantize Clip — the selected note clip's pattern at the lanes' snap step, one
  `pattern quantize <pt> --grid <step>` line, DR-UI-9.) (**AMENDED (user request, 2026-10-16):** *"remove
  'Home', when click the logo solaris back to home just like cosmo and interstellar"* — the Home button is gone;
  the `solaris.` wordmark IS Home, as cosmo's and Interstellar's are: washed on hover, a click goes Home (with the
  unsaved-changes question, as the button asked). The left reads: wordmark · Settings · the menus. File › Home
  stays.)
- **R-UI-4 Cosmo's widgets are reused as libraries** (`SliderRow`, `SegmentedControl`, `PillButton`,
  `IconButton`, `ConfirmDialog`, `MenuStrip`, `HoverFade`); Interstellar's timeline idioms are
  followed, and its header-only helpers (`TextFit`, `EasedScroll`, `Glyphs`) are INCLUDED from
  `apps/interstellar/app/widgets/`, not copied. A copied widget is a divergence with a delay fuse.
  (**Amended 2026-10-08:** `SettingsDialog` left the list — R-SET-1; the Interstellar helpers joined
  it — their home should be Artboard, a task filed in `docs/PROGRESS.md`. **U2:** `AnimatedRows`
  joined them — the browser's list and the lanes travel through it. **U3:** cosmo's `ContextMenu`
  (routes, effects, rename) and Interstellar's `FadePage` joined; `SliderRow` gained an OPT-IN
  readout formatter and value width — engineering units are not cosmo's −100…100 — following the
  precedent of Interstellar's key gutter: unset, cosmo's rows draw exactly as before.)
- **R-UI-5 Device panels are generated from the registry** (R-DSP-2): a knob or slider per
  parameter, grouped by its name's prefix (`osc1.*`, `filter.*`). A new DSP parameter appears in
  the UI with no UI code.
  (**AMENDED (R-WIN-1, 2026-10-09):** the generated controls now open in a device WINDOW — the rule is
  unchanged, the frame moved.)
- **R-UI-6 Every state is drawn and shot,** empty and loading included, at two window sizes,
  mid-transition as well as at rest. The shots drive the REAL service through the same hooks the
  window uses (no fake): the service is headless and cheap, so the shot is also an integration test.
- **R-UI-7 A strip has one colour everywhere** — its own, else one derived from its id, so it never
  changes when other strips come and go; a clip wears its strip's colour on whatever lane it sits
  (the timeline, and the mixer with U3). The service resolves it (R-G-3: a second front end must
  not re-derive it). (**Added 2026-10-08, building U2:** the lanes first took a strip's place in
  processing order, and deleting one strip recoloured every later strip's clips in one frame.)

- **R-UI-10 The grid follows the zoom** (user request, 2026-10-09: *"when zoom, divide into smaller note"*):
  zoomed out the lanes show bars; zooming in, beats appear, then halves, quarters and on to the
  thirty-second of a beat — each level fading in as it gets room (eased: nothing snaps, §1). The finest
  level that has room is THE snap step everywhere on the lanes — a ruler click (R-TIME-5), a clip drag,
  a loop drag (R-EDM-7), an automation point (R-AUTO-6), a browser drop; at the deepest zoom nothing
  snaps. The step is the view's, published for tests and drawn on the ruler.
- **R-UI-11 IDs you can see and copy** (user request, 2026-10-09: *"highlight id, right click to an object
  property to copy it property, for 3rd vst3, right click on item in parameter list to do it"*): View ›
  Show IDs draws every object's id beside its name — strips, lanes, clips, patterns, devices, sends,
  automations — and every parameter row's full address. A right-click on any property (a device
  parameter row, a fader, a pan, a send, an automation, a clip) offers Copy Address
  (`dv_1.filter.cutoff`), Copy Value (`1200 Hz`) and Copy as Formula (`=dv_1.filter.cutoff`), through
  the host's clipboard (the core has none, R-SVC-4). A third-party VST3's parameter list (R-VST-6) gets
  the same menu when hosting lands.
- **R-UI-12 An instrument's face is an analog synth panel** (user request, 2026-10-15: *"make a full GUI
  following analog synth design, with arstro theme that mean the basic synth should have section, knob,
  good design following an analog synth"*). An instrument's window (R-WIN-1) and its plugin's editor
  (R-VST-7) show its parameters as a hardware panel in the Arstro look — still GENERATED from the registry
  (R-UI-5, law 2: a parameter the DSP library adds tomorrow appears as a knob with no UI code):
  - **sections** — a module per parameter group, titled by the registry's group label (DSP REQ-device-10),
    in the registry's order with the ungrouped last; the modules flow left to right and wrap with the
    width (design rule R4), a row's modules sharing its height and its spare width;
  - **knobs** for numbers — a 270° dial whose arc, in the accent, grows from the minimum, or from zero for a
    range that spans it; detents for a whole-number range of a few steps; its label under it and its value,
    in its unit, under that;
  - **switches** for choices — a row of keys, each drawn as the choice's shape (a wave, a filter) or named,
    the chosen one lit, the light sliding to a new choice;
  - **displays** drawn from the live values — an oscillator's wave (its unison voices spread by the detune),
    a filter's response (the DSP library's own documented prototype, measured to agree with the filter),
    an envelope's shape (linear stages, as the library's ADSR);
  - **gestures** — drag up or down (Shift: fine), double-click → the default, right-click → the parameter's
    menu (R-WIN-3, in Solaris); each step of a drag one `set`, as a slider's was;
  - **nothing snaps** — a value changed anywhere turns its knob there and redraws its display through it;
    the last change is lit (R-WIN-2); a bound parameter names its binding under the knob and follows its
    live value (R-MIX-16); Show IDs puts each address over its label (R-UI-11).
  An effect's window keeps the parameter list.

- **R-UI-13 The song's tempo is set where it is shown** (U-1, R-SVC-11): the song bar's "<bpm> bpm" is a control
  — dragged up or down, a bpm a step (Shift: a tenth), one `set project.bpm=<n>` per step that moved it (as a
  fader sends each step); a double-click types one in. Drawn eased when it changes from elsewhere (§1).
- **R-UI-14 The pointer says a drag resizes** (user request, 2026-10-16: *"when on the edge to expand/shrink item,
  cursor change to resize icon so that it more clear to do"*). Over a grip that changes a SIZE the pointer is a
  resize cursor — ↔ over a clip's end (R-CLIP-7), a note's end and the pattern's end handle in a piano roll, the
  loop brace's two ends on the ruler; ↕ over the mixer dock's top edge and a note's velocity bar; ⤡ over a
  window's corner (R-WIN-5) — exactly where a press would take that grip (the same zone, never a second guess), and
  it stays that while the resize is in hand, wherever the pointer goes; anywhere else, and over any modal (the
  settings, a menu, the history tree, a dialog), the arrow. A window covers what is beneath it. The App decides the
  shape; the host only shows it (law 12) — the OS's own cursors, which do not ease (an OS cursor is not drawn by us).

## R-NFR — non-functional — 📋 SPECIFIED

- **R-NFR-1 Performance:** a 64-strip project renders faster than real time on a laptop (was
  SR-NFR-4).
- **R-NFR-5 Legibility:** the `.slp` is plain text, hand-editable, one fact per line (was SR-NFR-5).
- **R-NFR-6 Build:** `apps/solaris` in the root CMake; every suite in `ctest` (was SR-NFR-6).
- **R-NFR-7 Known limit — `AudioConfig` is a process singleton** in the DSP library, so one process
  renders at one sample rate at a time. SR-NFR-2 asked for per-render config; that is a DSP library
  change (its REQ-base-1 says the opposite) and is deferred, written here so nobody assumes it.

## R-TEST — evidence — 📋 SPECIFIED

- **R-TEST-1** Every suite registers with the root `ctest` and undefines `NDEBUG` before `<cassert>`.
- **R-TEST-2** Lowest level that proves it; L2 (the real service, headless) is usually right.
- **R-TEST-3** A new behaviour's test fails without it — checked.
- **R-TEST-4 Audio is asserted by numbers:** a render's peak/RMS/spectral peak at a known place,
  byte-identical output across two runs, a golden hash.
