# Solaris — Requirements

Derived from the answered [questionnaire](questionnaire.md). Requirements are numbered
`SR-<area>-<n>`. Each carries a **target phase** `[P1]…[P10]` (see [plan.md](plan.md)); the phasing
follows the guiding principle:

> **Adapt the basic workflow first, features later.** The offline arrange→render→play loop comes
> first (P1–P5); recording, automation, versioning, embedding, and advanced editing layer on after.

Full target scope is captured here even where a capability is scheduled late or "reserved"; the
plan sequences it. Relationship to the other docs: [prerequisites.md](prerequisites.md) (what must
exist first), [../../docs/shared-core.md](../../docs/shared-core.md) (the shared **Nebula** core),
[../README.md](../README.md) (design brief).

---

## 1. Scope & product definition

- **SR-SCOPE-1** `[P1–P5]` Solaris v1 is a multi-track DAW that arranges **audio + MIDI** on tracks,
  drives a **per-track effect rack** and **synth instrument**, mixes through a flexible routing
  graph, plays back in **real time**, **records** audio, and **renders offline** to WAV (master +
  stems). It is a full [Nebula](../../docs/shared-core.md) project with version control, merge, and
  cross-project embedding.
- **SR-SCOPE-2** `[P5]` **Real-time playback is a v1 capability**, not deferred. To keep it testable
  it is built as a driver over an **offline-first, deterministic engine** (SR-NFR-1): the same graph
  renders offline (for tests/export) and runs on the audio device callback (for live play).
- **SR-SCOPE-3** `[P6]` **Audio recording** (capture from inputs) is in v1, including a
  **routing matrix** to pick and wire **multiple inputs** to record-armed tracks (SR-ROUTE-4).
- **SR-SCOPE-4** Target users: both **scoring/beds for the Interstellar MV workflow** and
  **general music production**.
- **SR-SCOPE-5** `[P1]` `solaris_core` is **UI-free** (like `cosmo_core`); the CLI and a later
  Artboard UI both drive it. v1 platform is **Linux CLI**.

## 2. Non-functional requirements

- **SR-NFR-1 Determinism.** An offline render is a **pure function of (project, config)**. Output is
  **per-machine reproducible** (same machine + build → byte-identical), enabling golden-render tests
  and content-hashed commits. Fixed processing **block size**; no wall-clock; any stochastic
  processor is **seeded** from project data. `[P2]`
- **SR-NFR-2 Per-render config.** `AudioConfig` is **not a global singleton** for the engine; sample
  rate / channel count / block size are supplied **per render/stream**, so two projects or tests
  never collide. `[P2]`
- **SR-NFR-3 Real-time safety.** The audio callback path allocates no memory, takes no locks, and
  does no I/O; parameter and event delivery to it use **lock-free queues** (`DSP::LockFreeQueue`).
  An xrun counter is exposed. `[P5]`
- **SR-NFR-4 Performance.** Offline render is **faster than real time** for a ~64-track project on a
  laptop. Multicore render (parallelize independent graph branches) is a **nice-to-have**, not a v1
  gate. No hard song-length cap. `[P4, P7]`
- **SR-NFR-5 File legibility.** The project file is **plain text, hand-editable and commentable**
  (a design goal), one fact per line, canonical formatting. `[P1]`
- **SR-NFR-6 Build & test.** A `solaris` project in `build.sh` + top-level CMake; unit + golden
  tests registered in `ctest`, mirroring cosmo. `[P1]`

## 3. Project model & file format

- **SR-FMT-1** `[P1]` A project is a **Nebula** text document with extension **`.slp`** and header
  `app=solaris`, sharing the Nebula grammar with `.cmp`/`.isp` (stable-id nodes, `key=value` fields,
  explicit order keys, canonical serialization — [shared-core.md §2](../../docs/shared-core.md#2-the-text-project-format)).
- **SR-FMT-2** `[P1]` One file holds the whole project: transport/tempo, tracks, clips, notes,
  racks, automation, mixer/routing, and markers.
- **SR-FMT-3** `[P1]` Heavy media (audio samples) is **never** inline; it is referenced from the
  **resource pool** as `res:<hash>` + a path hint, relinkable, offline-flagged if missing.
- **SR-FMT-4** `[P1]` Serialization round-trips byte-identically (parse→serialize is a fixed point).

## 4. Transport & time

- **SR-TIME-1** `[P3]` The authoritative time unit is **beats**, at **960 PPQ**. Seconds are derived
  from tempo. Clip/note/automation times are stored in beats (or ticks).
- **SR-TIME-2** `[P3]` v1 has a **single global tempo** and a **single global meter**. The format
  **reserves** a tempo map / meter map for later (SR-TIME-5) — schema is designed so adding them is
  non-breaking.
- **SR-TIME-3** `[P3]` Scheduling is **sample-accurate**: note and automation events fire at their
  exact sample offset within a processing block, not quantized to block boundaries.
- **SR-TIME-4** `[P3]` Transport supports play / stop / seek / loop (loop region in beats).
- **SR-TIME-5** `[P10, reserved]` Tempo map (tempo changes over time) and meter map.

## 5. Tracks & routing graph

- **SR-TRACK-1** `[P3/P4]` Track kinds: **`audio`**, **`instrument`**, **`bus`**, plus grouping/
  folder tracks. Soft target of **64** tracks; no hard cap.
- **SR-TRACK-2** `[P3]` Per-track controls: **gain, pan, mute, solo**.
- **SR-ROUTE-1** `[P4]` The engine is a **directed audio graph**, not a fixed track→master chain. A
  node's output can be **freely routed**: an instrument's `SignalGenerator` output routes anywhere,
  **defaulting to master**.
- **SR-ROUTE-2** `[P4]` Signal flow: sources (instruments, clip players, inputs) → track → optional
  **bus(es)** → master, with **sends**; but any node may also route directly to an output
  (SR-OUT-2).
- **SR-ROUTE-3** `[P3]` **MIDI is routable too** — a MIDI/note source can be routed to an instrument
  on another track (MIDI routing, not only audio).
- **SR-ROUTE-4** `[P6]` An **input routing matrix** lets the user pick and wire **multiple hardware
  inputs** to record-armed tracks / monitor paths.
- **SR-ROUTE-5** `[P1]` The routing graph must be acyclic for real-time (feedback only via explicit
  delay-compensated sends); cycles are rejected with a clear error.

## 6. Clips

- **SR-CLIP-1** `[P3/P4]` Clip types: **audio clips** (sample refs) and **note/MIDI clips**.
- **SR-CLIP-2** `[P4]` Audio clip edits: trim in/out, timeline start, gain, **fade in/out**, **loop**.
- **SR-CLIP-3** `[P10]` **Time-stretch** and **pitch-shift** on audio clips (deferred; needs a
  stretch/pitch DSP module).
- **SR-CLIP-4** `[P4]` Clips may **freely overlap** on a track; overlapping audio sums (with a defined
  mix policy), overlapping note clips layer.
- **SR-CLIP-5** `[P4]` Effects live on the **track rack** (SR-RACK); clips carry only gain/fade
  (no per-clip racks in v1).

## 7. Notes & MIDI

- **SR-MIDI-1** `[P3]` A note stores **pitch (0–127), start, length, velocity**, and **per-note
  expression** (per-note pitch-bend / expression / **microtuning**) `[P10 for expression]`.
- **SR-MIDI-2** `[P7]` **Continuous controllers** — pitch-bend, mod-wheel, CC lanes — are supported
  as time-based lanes feeding the instrument.
- **SR-MIDI-3** `[P10]` **Standard MIDI File** import **and** export (`.mid`).
- **SR-MIDI-4** `[P8]` Note merge policy: two branches editing the same note clip **union by
  (start, pitch)**; an identical note = no conflict; the same note edited differently = a
  field-scoped conflict.

## 8. Instruments & the processor rack

- **SR-INST-1** `[P3]` v1 instrument: the existing DSP **`SynthEngine`** (Pulsar's synth).
- **SR-INST-2** `[P10]` A **sampler** instrument (play audio samples chromatically) — deferred (new
  DSP module).
- **SR-RACK-1** `[P4]` A track/bus hosts an **ordered processor rack**: add / remove / **reorder**,
  with **per-node bypass**.
- **SR-RACK-2** `[P2/P4]` v1 effects = whatever the promoted DSP **registry** exposes (reverb, EQ,
  the ESP32-synth FX chain, envelope, spatial…).
- **SR-RACK-3** `[P10, reserved]` **VST3** hosting (later); no third-party plugins in v1.

## 9. Parameters

- **SR-PARAM-1** `[P2]` Every adjustable parameter has a stable, serializable **string path**
  `<nodeId>/<paramName>` (e.g. `fx_syn/cutoff`), with names from the DSP registry descriptors.
- **SR-PARAM-2** `[P2]` Parameter values are stored **normalized 0..1**; the registry maps to real
  units (Hz/dB/ms) on apply.
- **SR-PARAM-3** `[P2]` A **`paramVersion`** in the file + a migration hook handle registry
  evolution.

## 10. Automation

- **SR-AUTO-1** `[P7]` Automation curves animate parameters over time; sample-accurate application.
- **SR-AUTO-2** `[P7]` Curves use **bezier handles** (like Cosmo's mixer curve) between breakpoints.
- **SR-AUTO-3** `[P7]` Automatable targets: **any registry parameter** plus track **gain/pan**.

## 11. Mixer & buses

- **SR-MIX-1** `[P4]` Per-track/bus: gain, pan, mute, solo. Solo respects a solo-bus/AFL policy
  (defined in design).
- **SR-MIX-2** `[P4]` **Buses** receive track outputs and sends; a **master** bus sums to the
  default output.
- **SR-MIX-3** `[P4]` **Sends** support both **pre-fader and post-fader** taps.
- **SR-MIX-4** `[P7]` **Multichannel**: channel count is **configured per project** (default **2**);
  tracks/buses carry ≥1 channel; a master rack is supported.

## 12. Outputs & devices

- **SR-OUT-1** `[P7]` The master defaults to a **2-channel** hardware output, but output routing is
  **flexible**: the master line is **not** the only path to hardware.
- **SR-OUT-2** `[P7]` **Any bus or channel can route directly to a hardware output.** Configurations
  include a 4-channel device, or **multiple sound devices where each device outputs a different
  channel/set**.
- **SR-OUT-3** `[P7]` The output routing is part of the routing matrix (SR-ROUTE) and is persisted in
  the project.

## 13. Real-time playback

- **SR-RT-1** `[P5]` A Linux **audio device backend** streams the engine graph on the audio
  callback (device/backend choice is a P5 decision — see [plan.md](plan.md)).
- **SR-RT-2** `[P5]` Real-time play/stop/seek/loop; live parameter/note edits reach the audio thread
  via lock-free queues (SR-NFR-3).
- **SR-RT-3** `[P5]` **Offline↔RT parity**: rendering a fixed project offline and capturing the RT
  output of the same project produce matching audio (within a defined tolerance).
- **SR-RT-4** `[P7]` Multi-device / multi-output streaming per SR-OUT-2.

## 14. Recording

- **SR-REC-1** `[P6]` Record audio from hardware inputs into **new audio clips** on record-armed
  tracks; captured audio lands in the resource pool.
- **SR-REC-2** `[P6]` The **input matrix** (SR-ROUTE-4) selects which input(s) feed which armed
  track; input monitoring is supported.
- **SR-REC-3** `[P6]` Recording is sample-accurately aligned to the transport position.

## 15. Rendering, export & bounce

- **SR-RENDER-1** `[P3/P4]` Offline render to **WAV**, producing the **master mixdown** and
  selectable **stems** (per track/bus).
- **SR-RENDER-2** `[P4]` Export format: **WAV, 24-bit and 32-bit float**, at the project sample rate.
- **SR-RENDER-3** `[P4]` Render an arbitrary **[start,end] range** or the whole song.
- **SR-RENDER-4** `[P4]` **Tail handling**: render continues past the last event until output falls
  below a silence threshold or a max-tail cap (so reverb/delay tails aren't cut).
- **SR-RENDER-5** `[P9]` **Bounce-in-place / freeze**: a track or the master can be bounced to a
  cached mixdown clip; playback then uses the **bounce** (to save CPU) with a clear **indicator**
  that audio is bounced (not live). Best case remains **live** playback from the project.

## 16. Audio file I/O

- **SR-IO-1** `[P2]` A **library-level** audio-file reader/writer using **vendored `dr_wav`** (no
  system dependency — matches the vendored-LibRaw precedent). WAV in and out.
- **SR-IO-2** `[P2]` v1 decodes **WAV** source clips (FLAC/others later).
- **SR-IO-3** `[P2]` One project **sample rate**; source clips at a different rate are **resampled**
  (a resampler is provided). On tempo-driven stretch, an option **preserves pitch** (SR-MERGE-4).

## 17. Versioning (Nebula)

- **SR-VCS-1** `[P8]` Every project is a small repository with **branch + auto-rebase** ("living
  branches"): a feature branch declares a base; when the base advances, the branch **auto-rebases**
  onto the new tip (clean → folds in; conflict → flags only the clashing nodes).
- **SR-VCS-2** `[P8]` Backing store is a **self-contained text store** (own commit/branch objects),
  not an external git.
- **SR-VCS-3** `[P8]` A commit is an **explicit user commit** plus **coalesced auto-snapshots** (like
  Cosmo's history coalescing). History is a **branching tree** (reuse the Nebula/Cosmo model) with
  undo/redo across it.

## 18. Merge

- **SR-MERGE-1** `[P8]` **Semantic 3-way merge** on the node graph: field-scoped conflicts;
  note lists and automation curves **merge by time**; additions union.
- **SR-MERGE-2** `[P8]` **Project merge** (combine two songs) mirrors embedding: a **main project**
  and an **imported project**; policy **concatenate** (imported after the main's last item) or
  **overlay**.
- **SR-MERGE-3** `[P8]` Id/name collisions on merge: **id-namespace** both sides; **dedupe** identical
  source assets by content hash.
- **SR-MERGE-4** `[P8]` On tempo mismatch (A=120, B=140), **time-convert B's beats** to A's tempo;
  offer an option to **pitch/stretch B's samples to preserve pitch** under the tempo change
  (stretch itself is SR-CLIP-3 `[P10]`; the option is recorded earlier).

## 19. Embedding & nested projects

- **SR-EMBED-1** `[P9]` A Solaris project can be **embedded into another Solaris project**: the
  embedded project is **imported onto the timeline after all items of the host** (append), and its
  **mixer tracks stay separate** with **their own master**, which is **routed into the host's
  master**.
- **SR-EMBED-2** `[P9]` An embed follows a **branch** of the source; when that branch advances, the
  host **auto-rebases** and the embedded content updates (no conflict) or flags (conflict) —
  cross-project propagation ([shared-core.md §5](../../docs/shared-core.md#5-cross-app-embedding--propagation)).
- **SR-EMBED-3** `[P9]` For playback/CPU, an embedded (or any) project can present its **bounced
  master mixdown in place** (SR-RENDER-5) with the live/bounced indicator; live playback from the
  full project is the best case.
- **SR-EMBED-4** `[P9]` When Solaris is embedded **into Interstellar** (`as=audio`), it exposes the
  **master mixdown and named stems**, following a branch (drives the MV workflow).

## 20. CLI

- **SR-CLI-1** `[P1]` A **headless CLI** drives `solaris_core` via **subcommands over a project
  file** (`solaris <verb> <proj> …`): create/open/save, add/edit/remove any node (tracks, clips,
  notes, rack, params, automation, routing), transport/play, record, render, branch/rebase/merge,
  embed. Full headless authoring (a song can be built entirely from the CLI).
- **SR-CLI-2** `[P1]` Output is **human text + a `--json` mode**; **non-zero exit** on error; merge/
  rebase conflicts are **machine-readable**.

---

## Appendix — target-phase index

| Phase | Requirements delivered |
|-------|------------------------|
| **P1** Model + CLI skeleton | SR-SCOPE-5, SR-FMT-*, SR-NFR-5/6, SR-CLI-*, SR-ROUTE-5 |
| **P2** DSP enablement | SR-NFR-1/2, SR-PARAM-*, SR-RACK-2, SR-IO-* |
| **P3** Offline MIDI→synth→WAV | SR-TIME-1..4, SR-TRACK-2, SR-ROUTE-3, SR-CLIP-1(note), SR-MIDI-1(core), SR-INST-1, SR-RENDER-1(master) |
| **P4** Audio clips + rack + mixer | SR-TRACK-1, SR-ROUTE-1/2, SR-CLIP-1/2/4/5, SR-RACK-1, SR-MIX-1/2/3, SR-RENDER-1/2/3/4 |
| **P5** Real-time playback | SR-SCOPE-2, SR-RT-1/2/3, SR-NFR-3 |
| **P6** Recording + input matrix | SR-SCOPE-3, SR-ROUTE-4, SR-REC-* |
| **P7** Automation + multichannel/output routing | SR-AUTO-*, SR-MIX-4, SR-OUT-*, SR-RT-4, SR-MIDI-2 |
| **P8** Nebula VCS + merge | SR-VCS-*, SR-MERGE-*, SR-MIDI-4 |
| **P9** Embedding + bounce/freeze | SR-EMBED-*, SR-RENDER-5 |
| **P10** Advanced (later) | SR-CLIP-3, SR-INST-2, SR-MIDI-1(expr)/3, SR-RACK-3, SR-TIME-5 |
