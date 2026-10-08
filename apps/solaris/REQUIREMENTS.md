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

## R-DSP — the sound is the DSP library's — 🚧 IN PROGRESS (the library side is built — DR-DSP-1; Solaris hosts it from E1)

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

## R-INST — instruments — ✅ IMPLEMENTED in the DSP library (DR-INST-1, DR-INST-2); played by Solaris from E1

The user's request (2026-10-08): *"make me some basic instruments: Drum Machine, Basic Synth with 2
OSC."*

- **R-INST-1 Basic Synth — two oscillators, subtractive.** Polyphonic. Per oscillator: waveform
  (sine, saw, square, triangle), octave, semitone, fine tune, level, unison voices and detune. A
  noise source. One resonant filter after the oscillators — low-pass, band-pass or high-pass, cutoff,
  resonance, envelope amount, key tracking — with its own ADSR. An amplitude ADSR after the filter
  (oscillators → filter → amplifier: the classic order). Velocity scales level. Built on the DSP
  library's `Oscillator`, `ADSREnvelope` and a state-variable filter (was SR-INST-1).
- **R-INST-2 Drum Machine — synthesized kit, played by notes.** Pads on the General MIDI drum
  notes: kick 36, rim 37, snare 38, clap 39, low tom 41, closed hat 42, mid tom 45, open hat 46,
  high tom 48, cowbell 56. Each pad has tune, decay, tone, level and pan. **The closed hat chokes the
  open hat.** Every pad is synthesized from DSP primitives (oscillators, seeded noise, filters,
  exponential decays) with its model written in its README — no sample files.
- **R-INST-3 An instrument is played by note clips** (R-CLIP-2). A drum pattern is a note clip
  whose notes are pads; a step sequencer is a *view* of that clip (R-UI), not a second data model.
- **R-INST-4 Reserved:** a sampler (play a sample chromatically) (was SR-INST-2), VST3 hosting (was
  SR-RACK-3).

## R-FX — effects — 🚧 IN PROGRESS (1–4 ✅ in the DSP library, DR-FX-1; 5, the strip's rack, from E1)

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
  `Chorus` and `Overdrive` — reused, not rewritten.
- **R-FX-5 A strip hosts an ordered rack** of devices: add, remove, reorder, bypass per device
  (was SR-RACK-1). An instrument strip's first device is its instrument.

---

## R-MIX — the mixer: every source has its own strip — 🚧 IN PROGRESS (4 ✅ DR-MIX-4; 11 ✅ DR-MIX-11; 1/5/6 rendered by the engine, DR-ENG-1; 3's defaults in the model)

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
  which allowed any acyclic graph).
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
  `matrix print [--json]` prints the same thing.
- **R-MIX-10 Mix audit** — a command and a panel: strips with no clips, clips on muted strips,
  strips that reach no output, buses with a single input, peaks over 0 dBFS in the last render or
  playback. Assertable from the CLI like Interstellar's `lint`.
- **R-MIX-11 Mix laws are the suite's**: constant-power balance pan with unity at centre and
  linear-amplitude fades — Interstellar's `AudioMix` — so one project sounds the same in both apps.
- **R-MIX-12 Strips feeding a bus fold under it** in the mixer view (expand to see them), so thirty
  one-shots are not thirty visible strips.

## R-LANE — the timeline: lanes hold time — 📋 SPECIFIED

- **R-LANE-1 A lane is a timeline row for organisation only** (FL Studio's playlist): it holds any
  clips, from any strips. Lanes are added, named, coloured, reordered and deleted (deleting a lane
  with clips is refused unless asked to take them with it).
- **R-LANE-2 Arrange by Lane | Channel.** One toggle regroups the same clips as one row per strip —
  each row is exactly what one strip plays. In that view, dragging a clip to another row moves it to
  that strip (re-routes it).
- **R-CLIP-1 An audio clip** plays a span of a file: `at` (beats), source `in`/`out` (seconds — a
  file has no tempo), gain, fade in/out, loop to fill a length (was SR-CLIP-1/2).
- **R-CLIP-2 A note clip** plays a **pattern** — a named list of notes (pitch, start, length,
  velocity) with its own length; a clip longer than its pattern loops it (the EDM case). It sounds
  through exactly one instrument strip.
- **R-CLIP-3 Duplicates are linked.** Duplicating a note clip makes a second clip of the SAME
  pattern: editing the notes edits every copy. "Make unique" gives a clip its own copy.
- **R-CLIP-4 Clips may overlap** on a lane; overlapping audio sums (was SR-CLIP-4).
- **R-CLIP-5 Reserved:** time-stretch and pitch-shift (was SR-CLIP-3).

## R-TIME — transport and time — 📋 SPECIFIED

- **R-TIME-1 Beats are authoritative**, 960 PPQ; seconds are derived from the tempo (was SR-TIME-1).
- **R-TIME-2 One tempo and one meter** per project; a tempo/meter map is reserved (was SR-TIME-2/5).
- **R-TIME-3 Sample-accurate:** notes start on their exact sample (R-DSP-5) (was SR-TIME-3).
- **R-TIME-4 Transport:** play, stop, seek, loop region, metronome (was SR-TIME-4).

---

## R-DEV — audio devices: use every device you have — 📋 SPECIFIED

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
- **R-DEV-8 Linux first**, over the PulseAudio API (which PipeWire also serves); Windows later.

## R-SET — settings — 📋 SPECIFIED

- **R-SET-1 Cosmo's Settings dialog, reused,** as Interstellar R-SET-1, plus Solaris's rows: output
  and input devices (enable, clock), sample rate (the default for new projects and the preferred
  device rate), buffer size with the latency it costs in ms, a test tone, the port map, and the
  **sample folders** (add, remove, reorder) that become the browser's quick-access list.
- **R-SET-2 Settings are the machine's**, persisted beside the app's other settings, never in a
  project. Each change is a `settings set` line the service validates, applies and persists.

## R-HOME — Home — 📋 SPECIFIED

- **R-HOME-1 Like Interstellar's and Cosmo's.** Recent projects as cards, newest first: name,
  `bpm · length`, version count. New (name, tempo, meter, sample rate defaulting from Settings),
  Open, Settings.

## R-BROWSE — the left panel: samples and presets — 📋 SPECIFIED

- **R-BROWSE-1 Three sections:** Folders (the sample folders from Settings), Presets (every
  instrument and effect in the registry with its factory and user presets), Project (the sounds the
  project already uses).
- **R-BROWSE-2 Click auditions** a sample through the master's port without touching the project.
- **R-BROWSE-3 Drag and drop.** A sample onto a lane → a clip at the drop point on a new or reused
  strip (R-MIX-2); into empty space → a new lane too; onto a strip → load it there. An instrument
  preset into empty space → a new instrument strip + lane + an empty clip; onto a strip's rack → an
  effect inserted at that slot. **Every drop is one command** (R-G-4).

---

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

## R-RENDER — offline render — 📋 SPECIFIED

- **R-RENDER-1 Render is offline and deterministic:** the same project, version and range give the
  same bytes (was SR-NFR-1, SR-RENDER-1).
- **R-RENDER-2 What it writes:** the master mixdown; selected strips as stems; or one file per
  output port. WAV, 24-bit or 32-bit float, at the project rate (was SR-RENDER-1/2).
- **R-RENDER-3 A range or the whole song**, with the tail rendered until the output falls below
  −90 dBFS or a 10 s cap (was SR-RENDER-3/4).

## R-PLAY — real-time playback — 📋 SPECIFIED

- **R-PLAY-1 Playback runs the same graph the render runs** on the clock device's callback;
  offline and live produce the same samples (was SR-RT-1..3, SR-SCOPE-2).
- **R-PLAY-2 The audio thread never allocates, locks or does I/O;** edits reach it through lock-free
  queues; xruns are counted (was SR-NFR-3).
- **R-PLAY-3 Meters** on every strip and the master: peak and RMS per channel, a held peak, a clip
  latch — the numbers in the model, not only drawn.

## Reserved areas — 📋 SPECIFIED, not scheduled

- **R-REC** recording from input ports into the armed strip, sample-aligned (was SR-REC-1..3,
  SR-ROUTE-4).
- **R-AUTO** automation curves on any registry parameter and strip gain/pan (was SR-AUTO-1..3).
- **R-MIDI** CC lanes, per-note expression, SMF import/export (was SR-MIDI-1..3).

---

## R-FMT — the `.slp` project — 🚧 IN PROGRESS (1, 2, 4 ✅ DR-FMT-1; 3's offline flag lands with V2)

- **R-FMT-1 A `.slp` is a text document in the suite grammar** (`arstro-project = 1`, `app = solaris`,
  `#type id=… key=value`), normative in [`docs/project-format.md`](docs/project-format.md), and its
  audio nodes are the suite schema's ([`../../docs/audio-format.md`](../../docs/audio-format.md)),
  extended there — additively, so an Interstellar `.isp`'s audio means what it meant (was SR-FMT-1/2).
- **R-FMT-2 Parse → serialize is a byte-exact fixed point;** unknown keys and nodes survive (was
  SR-FMT-4).
- **R-FMT-3 Media is referenced, never embedded:** a path relative to the project, offline-flagged
  if missing (was SR-FMT-3; its content hash is deferred to R-VER-5, where merging needs it).
- **R-FMT-4 A structural error refuses, a numeric corruption repairs** (Interstellar's rule).

## R-SVC / R-API — the service and its document — 📋 SPECIFIED

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

## R-UI — the screens — 📋 SPECIFIED

- **R-UI-1 Two screens, as Interstellar:** Home (R-HOME) and the Project view.
- **R-UI-2 The accent is teal, `#5AEDDE`, the only forked token** — cosmo's `#4F7EF7` rotated to
  H 174° at S 80 %, L 64 % (Interstellar's recipe). Chosen because orange, the "solar" reading, sits
  beside record-red and solo-amber, which a DAW cannot afford to confuse.
- **R-UI-3 The Project view:** Cosmo's menu strip and wordmark with the version chip; the browser on
  the left (R-BROWSE); lanes in the centre (R-LANE); **the mixer docked under the timeline** with a
  tab per mixer and a Matrix tab (R-MIX); the transport (R-TIME-4) with the master meter and device
  status. Docked, not a separate screen, because linked selection (R-MIX-8) needs both in view.
- **R-UI-4 Cosmo's widgets are reused as libraries** (`SliderRow`, `SegmentedControl`, `PillButton`,
  `IconButton`, `ConfirmDialog`, `MenuStrip`, `SettingsDialog`); Interstellar's timeline idioms are
  followed. A copied widget is a divergence with a delay fuse.
- **R-UI-5 Device panels are generated from the registry** (R-DSP-2): a knob or slider per
  parameter, grouped by its name's prefix (`osc1.*`, `filter.*`). A new DSP parameter appears in
  the UI with no UI code.
- **R-UI-6 Every state is drawn and shot,** empty and loading included, at two window sizes,
  mid-transition as well as at rest.

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
