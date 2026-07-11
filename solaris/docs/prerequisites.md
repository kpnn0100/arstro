# Solaris — Prerequisites (Definition of Ready)

> What must be **defined** (decisions/specs/contracts) and **implemented** (foundational code)
> *before* it makes sense to start building `solaris_core`. Solaris is a thin DAW front-end over
> two things: the existing **DigitalSignalProcessing (DSP)** engine, and the **not-yet-existing**
> shared **[Nebula](../../docs/shared-core.md)** project/VCS/resource core. This document is a
> checklist, not an implementation.

## 0. Where Solaris actually sits (grounding)

Audited against the real tree (`DigitalSignalProcessing/`):

**Already exists and is reusable:**
- Per-sample processing base `SignalProcessor::process(Sample in, int channel)` with block-smoothed
  params (`src/base/SignalProcessor.h`) and `AudioConfig` (`src/base/AudioConfig.h`).
- Instrument: `SynthEngine` / `VoiceManager` / `Voice` with `noteOn(midiNote, velocity)` /
  `noteOff(midiNote)` and per-sample `render(channel)` (`src/synth/`).
- A library of processors: `effects/`, `reverb/`, `equalizer/`, `envelope/`, `generator/`,
  `spatial/`, `simpleProcessor/`.
- A data-driven rack **catalogue** pattern — `NodeType{name, factory, ParamDesc[]}` + a reorderable
  chain — but it lives in an **app**, not the library (`apps/kitchen_sink/RackEngine.h`). Pulsar is
  effectively "one instrument track" already.

**Does NOT exist yet (Solaris depends on these):**
- **Nebula** — the entire shared project/VCS/resource/merge/embed core. Nothing is implemented.
- **Offline deterministic render** — there is per-sample `process`, but no "render this graph for N
  samples/blocks into a float buffer, deterministically" utility, and no timeline.
- **Library audio-file I/O** — WAV read/write exists only as an app demo using libsndfile
  (`apps/wav_demo/main.cpp`), not as a library facility. `output/OutputBlock` only quantizes to
  ints for a UART transport.
- **Sequencer / timeline** — no scheduling of note/param events over time.
- **Multi-track / mixer / bus / sends / automation.**
- **Serializable param addressing** — `ParamId` (`src/synth/ParamId.h`) is a synth-specific
  `uint16` map (group high-byte + param low-byte), not a stable, self-describing **string path**
  that a text project can reference.
- **Determinism guarantees** — `AudioConfig` is a **singleton** (global sample rate); effects have
  not been audited for RNG/wall-clock/denormal nondeterminism. Content-hashed commits and
  reproducible renders need this pinned.

So the prerequisite work is roughly: **(1) a minimal Nebula**, **(2) DAW-enabling additions to
DSP**, then **(3) `solaris_core` on top**. Parts A–E below split that into DEFINE vs IMPLEMENT.

---

## A. What to DEFINE (decisions & contracts — before any code)

Freeze these as written specs. Many for Nebula are already sketched in
[shared-core.md](../../docs/shared-core.md) and need only be promoted to a frozen contract.

### A1. Nebula contracts (shared with Interstellar — define once)
- **Project model interface** — the node/field/child/order abstraction (`shared-core.md §1`) as a
  concrete API surface + validation rules.
- **Text grammar (formal)** — the exact tokenizer/writer rules (`shared-core.md §2`): header line,
  `#type id=…` blocks, `key = value` scalars/lists, canonical number formatting, comment/escape
  rules, unknown-key preservation. Nail this because *everything* (diff, merge, hash) depends on
  byte-canonical output.
- **Versioning semantics** — commit identity (content hash), branch pointers, `base=` for living
  branches, and the exact **auto-rebase** trigger + outcome states (clean / conflict / pinned)
  (`shared-core.md §3`).
- **Semantic-merge resolver interface** — the ancestor/A/B node-and-field rules (`shared-core.md
  §4`) and the **type-resolver plug-in contract** apps register (e.g. "merge note lists by time").
- **Resource pool addressing** — `res:<hash>` scheme, relink/offline behavior (`shared-core.md §7`).
- **Embed resolution contract** — `target=<app>:<projid>`, `branch=`/pin, `as=<role>`; how a host
  requests a render/artifact from an embedded project (`shared-core.md §5`).
- **Decision:** implement Nebula on **real git** (custom merge/rebase drivers) vs a **self-contained
  store**. This changes a lot downstream; decide first.

### A2. Solaris domain model / schema (the app-specific part)
- **Node types + fields + units + ranges** for every type: `project` (bpm, meter, sampleRate),
  `track` (`kind=audio|instrument|bus`), `clip` (audio: src/in/out/start/gain/fade; note:
  start/length), `note` (pitch, start, length, velocity), `rack` (ordered processor chain),
  `effect`/instrument entry (type + param paths), `automation` (node/param/points/easing),
  `bus`/send (routing + level).
- **Time model** — is the authoritative unit **beats** (bpm + PPQ) or **seconds**? How are clip/
  note times stored so tempo changes and merges behave? Is scheduling **sample-accurate**?
- **Mixer/routing model** — track → bus → master graph; sends; gain/pan staging; where the master
  sums.
- **Stem / mixdown definition** — what "render `--stems trk_a,trk_b`" produces, and what a
  Solaris→Interstellar embed (`as=audio`) hands over (full mix vs named stems).
- **Instrument-per-track** — one `SynthEngine` per instrument track; polyphony/voice limits.

### A3. DSP integration contract (the seam Solaris drives)
- **Param path scheme (critical)** — a stable, serializable **string path** (e.g.
  `fx_syn/cutoff`, `osc1/detune`) for every adjustable parameter, and its mapping to DSP's
  `ParamId` / `RackEngine::ParamDesc.apply`. The text project references params by path, so this
  must be defined and versioned (like `PARAM_VERSION`) before serialization exists.
- **Processor registry contract** — promote `RackEngine`'s `NodeType{name, factory, ParamDesc[]}`
  catalogue into a **shared, serialization-friendly registry** (type name → factory + param
  descriptors with name/min/max/default/unit). Solaris instantiates racks from this by name.
- **Offline render contract** — the pure function
  `render(project, branch, range, sampleRate, blockSize) → float buffer` (or stems): deterministic,
  no wall-clock, events scheduled per block. Define block size, how note/automation events are
  quantized to block boundaries or sub-block, and how a per-render `AudioConfig` is supplied
  (see A4).
- **Note scheduling** — mapping note clips → sample-accurate `noteOn/noteOff` on the track's
  `SynthEngine`; voice-steal policy; note-off at range end.

### A4. Determinism & identity (required by Nebula)
- **Fixed processing constants** — a defined block size and channel layout for renders.
- **Config ownership** — decide whether `AudioConfig` stays a singleton (one global rate per
  process — simplest, acceptable for a single-project CLI) or becomes **passable per render** (so
  two projects/tests at different rates don't collide). Content-hash reproducibility needs the
  render to be a pure function of (project + config).
- **Nondeterminism audit** — verify no processor uses wall-clock time, unseeded RNG, or
  denormal-dependent output; define seeding for any stochastic effect. Canonical float formatting
  in the serializer.

### A5. CLI & file conventions
- **Extensions** — project `.slp`; confirm `.cmp`/`.slp`/`.isp` are all Nebula projects with an
  `app=` tag.
- **CLI grammar** — the command surface (`new/add-track/add-clip/add-note/rack/param/automate/
  branch/rebase/merge/render`) already sketched in the [README](../README.md §6); freeze verbs,
  argument shapes, exit codes, and stdout/stderr contract (scriptable).
- **Error/report contract** — how rebase/merge conflicts are reported on the CLI (machine-readable
  + human).

### A6. Audio I/O contract
- **Library WAV read/write** — decide the dependency: **libsndfile** (as `apps/wav_demo` already
  uses) vs a small **vendored `dr_wav`/`dr_flac`** (no system dep, matches the vendored-LibRaw
  precedent in ImageProcessing). Define supported formats/bit-depths.
- **Sample-rate handling** — resampling policy when a source clip's rate ≠ the project rate
  (define; a resampler may need implementing — see B2).

---

## B. What to IMPLEMENT (foundations, in dependency order)

Nothing here is Solaris UI; it is the substrate Solaris needs. Build bottom-up.

### B1. Nebula core — minimal viable (shared prerequisite, the big one)
Implement the smallest Nebula that supports a DAW end-to-end:
- [ ] **Model + canonical text (de)serializer** (round-trips byte-identically).
- [ ] **VCS store**: commit (content-hash), branch pointers, `base=`, history.
- [ ] **Semantic 3-way merge** with the field-scoped rules + a **type-resolver plug-in** hook.
- [ ] **Auto-rebase engine** (living branches): base-advance → replay → clean/conflict/pin.
- [ ] **Resource pool**: content-addressed put/get, relink, offline flag.
- [ ] **Embed resolver**: resolve `target@branch`, request an artifact, propagate on base-advance.
- [ ] **Nebula unit tests**: serialize round-trip, merge cases, rebase clean+conflict, embed
      propagation. (Determinism is testable here first.)

*Scope note:* Nebula is shared with Interstellar, so build it as its own library/module, not inside
Solaris. A stubbed subset (serializer + branches + merge, embeds later) is a legitimate M0 if
Interstellar isn't started yet — but the **contracts** (Part A1) must still be frozen so the stub
doesn't paint Solaris into a corner.

### B2. DSP additions / promotions (library-level; also benefit Pulsar & kitchen_sink)
- [ ] **Shared processor registry** — lift `RackEngine`'s catalogue into the DSP library
      (`src/…`), keyed by type name, with param **paths** + descriptors (A3). Keep it Open/Closed
      (adding a processor type registers it once).
- [ ] **Param path ↔ apply mapping** — the stable string-path layer over `ParamId`/`ParamDesc`
      (A3), versioned.
- [ ] **Offline render utility** — `render(graph, events, sampleRate, blockSize, nFrames) → float
      buffer`, deterministic; the pure-function core the DAW render sits on (A3/A4). Decide
      singleton-vs-passable `AudioConfig` here (A4).
- [ ] **Library WAV I/O** — reader (for audio source clips) + writer (master/stems), per A6. Move
      the `wav_demo` logic into the library (or vendor `dr_wav`).
- [ ] **Resampler** (only if A6 requires) — for source clips whose rate ≠ project rate.
- [ ] **Determinism fixes** — resolve anything the A4 audit flags.

### B3. Solaris core scaffolding (only after A + a minimal B1/B2)
- [ ] **Solaris Nebula schema** — register the node types + type-resolvers (note-list-by-time,
      automation-by-time) from A2.
- [ ] **Sequencer/scheduler** — turn note + automation nodes into per-block sample-accurate events.
- [ ] **Track/rack instantiation** — build each track's `SynthEngine`/rack from the registry (B2).
- [ ] **Mixer/bus summing** — the routing graph from A2.
- [ ] **Render pipeline** — tracks → racks → mixer → master/stems, via the offline render util.
- [ ] **Embed surface** — expose "render this Solaris project/stems" so Interstellar can embed it.

### B4. Harness & tests (alongside B3)
- [ ] **Headless CLI binary** — the Part A5 command surface over `solaris_core`.
- [ ] **Golden-render tests** — a project renders byte-identically across runs (determinism) and
      matches a stored reference.
- [ ] **Round-trip + VCS tests** — serialize/parse, branch/auto-rebase, merge-two-songs, embed
      propagation.

---

## C. Suggested build order (milestones)

- **M0 — Freeze contracts.** All of Part A written down (Nebula grammar/VCS/merge, Solaris schema,
  DSP param-path + render + config decisions, CLI grammar, audio-I/O + git-vs-store decisions).
- **M1 — Nebula minimal (B1).** Serializer + VCS + semantic merge + auto-rebase, with tests. (Embeds
  can trail.)
- **M2 — DSP DAW-enablement (B2).** Registry + param paths + offline deterministic render + WAV I/O.
- **M3 — Solaris core (B3) + CLI (B4).** Schema, sequencer, racks, mixer, render → `.slp` opens,
  edits, and renders to WAV from the CLI; branch/merge work.
- **M4 — Embedding.** Nebula embeds + the Solaris render-artifact surface, so a Solaris project can
  be embedded in Interstellar with live propagation. (Cross-checks against Interstellar's M-plan.)

Solaris "implementation" proper begins at **M3**; M0–M2 are the prerequisites this document is
about.

---

## D. Definition of Ready — checklist to start `solaris_core`

- [ ] Part A contracts are written and reviewed (Nebula grammar/VCS/merge frozen; Solaris schema
      with units/ranges; DSP param-path + offline-render + AudioConfig decisions; CLI grammar;
      audio-I/O + git-vs-store decisions).
- [ ] Nebula minimal (B1) builds, round-trips, merges, and auto-rebases with passing tests.
- [ ] DSP offers a registry + serializable param paths + a deterministic offline render + WAV I/O
      (B2), verified by a golden-render test.
- [ ] A one-track proof: a hand-written `.slp` (one instrument track, a few notes, one effect) can
      be parsed, rendered to a WAV deterministically, committed, branched, and auto-rebased. When
      that passes, the rest of Solaris is incremental feature work.

---

## E. Open decisions (make these in M0)

1. **Nebula on git vs a self-contained store?** (Affects merge/rebase implementation and tooling.)
2. **AudioConfig: keep singleton, or make render config passable?** (Affects determinism +
   multi-project.)
3. **Time authority: beats (bpm+PPQ) or seconds?** (Affects storage, merge, tempo changes.)
4. **WAV I/O: libsndfile (system dep) or vendored dr_wav (no dep)?**
5. **How much Nebula now?** Full shared library up front, or a Solaris-scoped subset that
   Interstellar later generalizes — while keeping the frozen contracts so it isn't a fork.
6. **Param-path scheme + versioning** — exact string form and how it maps onto `ParamId`.
7. **Determinism scope** — is byte-identical render across machines required, or per-machine
   reproducibility enough? (Affects float/denormal/SIMD policy.)

---

See [../README.md](../README.md) for the Solaris design brief,
[../../docs/shared-core.md](../../docs/shared-core.md) for Nebula, and
[../../docs/vision.md](../../docs/vision.md) for the suite workflow.
