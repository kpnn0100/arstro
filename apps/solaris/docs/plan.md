# Solaris — Implementation & Test Plan

Phased build plan for the [requirements](requirements.md). Each phase is a **shippable, verifiable
increment** with its own acceptance gate ("checked and confirmed, no bug") before the next starts.

## Strategy

1. **Basic workflow first.** The offline **arrange → render → play** loop lands first (P1–P5);
   recording, automation, versioning, embedding, and advanced editing come after.
2. **Offline-first engine, real-time as a driver.** The audio engine is built to run **offline and
   deterministically** first — that is the only way to unit-test audio ("golden renders"). Real-time
   playback (a v1 requirement, SR-SCOPE-2) is then a **thin driver** that runs the *same* graph on
   the device callback (P5). This gives real-time early **and** a testable core.
3. **Each phase gates on green tests + a manual CLI smoke** that proves that phase's workflow
   milestone. A phase is "done" only when its acceptance list passes and the smoke works.
4. **Determinism is the test backbone.** Because renders are pure functions (SR-NFR-1), most audio
   behavior is asserted by **golden-file comparison** (render a fixed `.slp` → compare to a stored
   reference WAV / hash) plus targeted numeric checks.

## Dependency graph

```
        P1 model + CLI ──────────────┬───────────────► P8 VCS + merge ──► P9 embed + bounce
        (foundation)                 │                    (parallel        (needs P4 render
                                     │                     workstream)       + P8)
        P2 DSP enablement ───────────┤
        (DSP repo, parallel w/ P1)   │
                                     ▼
                     P3 offline MIDI→synth→WAV ──► P4 audio clips + rack + mixer (offline)
                                                        │
                                                        ├──► P5 real-time playback ──► P6 recording + input matrix
                                                        │                               │
                                                        └──► P7a automation ────────────┴──► P7b multichannel/output routing
                                                                                              (needs P5 device)
        P10 advanced features: layered on the stable core (after P4/P7), each independent.
```

**Independent / parallelizable:**
- **P1 ∥ P2** — P2 lives in the `DigitalSignalProcessing` repo and does not need P1.
- **P8 ∥ P3–P7** — VCS/merge operate on the project *text*, not the audio engine; after P1 they can
  proceed alongside the audio phases (only P9 needs both P4 and P8).
- **P7a (automation, after P4) ∥ P5** — automation is offline math; can proceed alongside the
  device backend. **P7b (multichannel/output routing) needs P5.**
- **P10** items are each independent once the core (≤ P7) is stable.

**Hard dependencies:** P3→(P1,P2); P4→P3; P5→P4; P6→P5; P7b→P5; P9→(P4,P8).

## Phase table

| Phase | Delivers (workflow milestone) | Depends on | Parallel with |
|------:|-------------------------------|------------|---------------|
| **P1** | `.slp` model + serializer + headless CLI (create/edit/save) | — | P2, P8-start |
| **P2** | DSP: registry + param paths + deterministic offline render + dr_wav + resampler | — (DSP repo) | P1 |
| **P3** | **Author MIDI → synth → render master WAV** | P1, P2 | P8 |
| **P4** | **Full offline song**: audio clips + rack + mixer/buses/sends → master + stems | P3 | P8 |
| **P5** | **Live real-time playback** | P4 | P7a, P8 |
| **P6** | **Recording** + input matrix | P5 | P8 |
| **P7** | Automation (P7a) + multichannel & flexible output routing (P7b) | P4 (a), P5 (b) | P8 |
| **P8** | **Nebula VCS**: branch/auto-rebase/merge, merge-two-songs | P1 | P3–P7 |
| **P9** | **Embedding / nested import** + bounce-in-place/freeze | P4, P8 | — |
| **P10** | Advanced: stretch/pitch, sampler, expression, SMF, tempo map, VST3 | core stable | each other |

---

## P1 — Project model & CLI skeleton  *(foundation)*

**Goal.** A hand-editable `.slp` project (Nebula model + canonical text serializer + resource-pool
references) and a headless CLI that creates/opens/saves and adds/edits/removes nodes. No audio yet.

**Deliverables.**
- `solaris_core`: the node/field/order model + `.slp` reader/writer (canonical, round-tripping);
  resource-pool reference type (`res:<hash>`, relink/offline flag); the Solaris schema (track/clip/
  note/rack/effect/automation/routing node types with fields, even if some are inert this phase).
- CLI (`solaris`): `new/open/save`, `add-track/add-clip/add-note/rack/param/automate/route`,
  `--json`, non-zero exit on error (SR-CLI-*).
- `build.sh` `solaris` project + CMake target + `ctest` wiring (SR-NFR-6).

**Depends on:** none. **Parallel with:** P2, and the start of P8.

**Test gate (confirm no bug).**
- *Unit:* serialize→parse→serialize is a **fixed point** (byte-identical) over a corpus of hand-
  written `.slp` files; unknown-key preservation; malformed-input errors are clean.
- *Integration:* build a project entirely via CLI, save, reload, and assert the model matches;
  `--json` output schema is stable; bad args → non-zero exit.
- *Manual smoke:* `solaris new song.slp && solaris add-track … && solaris save` produces a legible,
  hand-editable file.

**Deferred out:** any audio (no render/playback); VCS (load/save only, no branches — that's P8).

---

## P2 — DSP enablement  *(in the DigitalSignalProcessing repo)*

**Goal.** Make DSP DAW-ready: a serializable processor registry, param paths, a deterministic
offline render, and library audio I/O. Benefits Pulsar/kitchen_sink too.

**Deliverables.**
- **Processor registry** promoted from `apps/kitchen_sink/RackEngine` into the DSP library: type
  name → factory + param descriptors (name/min/max/default/unit) (SR-RACK-2).
- **Param-path layer** over `ParamId`/descriptors: stable `<paramName>` strings, normalized 0..1,
  `paramVersion` + migration hook (SR-PARAM-*).
- **Per-render `AudioConfig`** (sample rate / channels / block size passed in, not a singleton)
  (SR-NFR-2).
- **Deterministic offline render** utility: `render(graph, events, config, nFrames) → float buffer`,
  fixed block size, no wall-clock, seeded RNG (SR-NFR-1).
- **Vendored `dr_wav`** reader/writer (SR-IO-1/2) and a **resampler** (SR-IO-3).

**Depends on:** none. **Parallel with:** P1.

**Test gate.**
- *Unit:* registry enumerates processors + descriptors; param-path get/set maps to the right
  `ParamId`; dr_wav round-trips (write→read→compare); resampler hits known targets (e.g. a sine at
  44.1→48 k stays a sine, error bounded).
- *Determinism:* render a fixed graph twice → **byte-identical**; a stored **golden** buffer matches.
- *Nondeterminism audit:* grep/verify no processor uses wall-clock/unseeded RNG on the render path;
  document/seed any that do.

**Deferred out:** timeline/mixer (that's the Solaris engine, P3+).

---

## P3 — Offline engine: MIDI → synth → master WAV  *(basic workflow #1)*

**Goal.** Author notes in a `.slp`, render the timeline to a master WAV offline. First "hear it".

**Deliverables.**
- **Transport/clock**: beats @ 960 PPQ, single tempo/meter, beats↔samples (SR-TIME-1/2).
- **Sequencer**: note clips → **sample-accurate** `noteOn/noteOff` on the track instrument
  (SR-TIME-3, SR-MIDI-1 core).
- **Instrument track** driving DSP `SynthEngine` (SR-INST-1); **MIDI routing** source→instrument
  (SR-ROUTE-3).
- **Minimal mixer**: per-track gain; sum to master (SR-TRACK-2 partial).
- **Offline render** of the timeline to a master WAV over a range (SR-RENDER-1 master).

**Depends on:** P1 (model/CLI), P2 (registry, render, dr_wav). **Parallel with:** P8.

**Test gate.**
- *Golden render:* a fixed `.slp` (a few notes, one synth) → a stored reference WAV; byte-identical
  across runs (SR-NFR-1).
- *Sample-accuracy:* a note scheduled at beat b starts within ±0 samples of the expected offset
  (assert on the rendered buffer's onset).
- *Manual smoke:* `solaris add-note … && solaris render song.slp --out master.wav` → the WAV plays
  the expected pitches at the expected times.

**Deferred out:** audio clips, effects rack, pan/bus/sends, real-time (P4/P5).

---

## P4 — Audio clips + rack + full mixer (offline)  *(basic workflow #2)*

**Goal.** A complete **offline** song: audio + MIDI clips, per-track effect racks, a full mixer with
buses and sends, exporting master + stems.

**Deliverables.**
- **Audio clip playback**: sample ref, trim in/out, start, gain, **fades**, **loop** (SR-CLIP-1/2);
  **free overlap** summing (SR-CLIP-4); resample source-rate mismatches via P2's resampler.
- **Track rack**: ordered add/remove/**reorder**/**bypass** from the registry (SR-RACK-1).
- **Routing graph**: free node routing, instrument/clip → track → bus → master, defaulting to master
  (SR-ROUTE-1/2); acyclic validation (SR-ROUTE-5).
- **Mixer**: gain/pan/mute/solo (SR-MIX-1), **buses** (SR-MIX-2), **pre/post-fader sends**
  (SR-MIX-3).
- **Render**: master **+ stems**, WAV **24-bit & 32-float**, arbitrary **range**, **tail** to
  silence (SR-RENDER-1..4).

**Depends on:** P3. **Parallel with:** P8, and P7a can start once this lands.

**Test gate.**
- *Golden renders:* a multi-track project (audio+MIDI+FX+bus+send) → reference master and stems.
- *Mixer math:* pan-law, mute/solo, send pre/post taps assert against hand-computed values; **stems
  sum to master** within tolerance.
- *Clip edits:* fade/loop/trim produce the expected envelope/sample ranges; overlap sums correctly.
- *Manual smoke:* build a 4-track song from the CLI, render master + stems, listen.

**Deferred out:** real-time, recording, automation, multichannel (>2), bounce.

---

## P5 — Real-time playback  *(basic workflow #3)*

**Goal.** Press play, hear the project **live**, running the same engine graph on the audio device
callback.

**Deliverables.**
- A Linux **device backend** (see *Open decision D1* below) streaming the engine graph.
- **RT-safe** callback path: no alloc/lock/I/O; lock-free param/event queues (`DSP::LockFreeQueue`)
  (SR-NFR-3); xrun counter.
- Transport **play/stop/seek/loop** live; live edits reach the audio thread safely (SR-RT-2).

**Depends on:** P4 (the engine it drives). **Parallel with:** P7a, P8.

**Test gate.**
- *Offline↔RT parity:* capture the RT output of a fixed project and compare to the offline render
  (SR-RT-3) within tolerance.
- *RT-safety:* static/audit check that the callback allocates/locks nothing (and a stress run with
  an **xrun counter at 0** under normal load).
- *Manual smoke:* `solaris play song.slp` → audio matches the render; stop/seek/loop respond.

**Deferred out:** recording (P6), multi-device/multichannel output (P7b).

---

## P6 — Recording + input matrix

**Goal.** Capture audio from hardware inputs into new clips, with a routing matrix to pick inputs.

**Deliverables.**
- **Input routing matrix** (SR-ROUTE-4): wire hardware inputs → armed tracks / monitor paths.
- **Record** armed tracks to new audio clips into the resource pool, transport-aligned (SR-REC-*);
  input monitoring.

**Depends on:** P5 (device I/O). **Parallel with:** P8.

**Test gate.**
- *Loopback:* feed a known signal to an input (or a virtual/loopback device) → the recorded clip
  matches the input (sample-aligned to transport).
- *Matrix:* routing N inputs → M tracks lands each on the right track.
- *Manual smoke:* arm a track, record, play back the take.

**Deferred out:** comping/takes management (later), punch-in (later).

---

## P7 — Automation + multichannel & flexible output routing

Two sub-streams; **P7a is independent of P5** (offline math), **P7b needs P5**.

**P7a — Automation.** Bezier-curve automation of any registry param + track gain/pan, sample-accurate
(SR-AUTO-*). **Depends on:** P4. **Gate:** curve sampling matches expected values at sub-block
offsets; a golden render with automation is stable; merge-by-time (feeds P8).

**P7b — Multichannel & output routing.** Per-project channel count (default 2) (SR-MIX-4); **any bus/
channel → hardware output**, 4-channel devices, and **multiple devices each carrying different
channels** (SR-OUT-1/2/3, SR-RT-4). **Depends on:** P5. **Gate:** a 4-channel routing test; a
two-device test (each device gets its assigned channels); routing persists in the `.slp` and
reloads.

**Deferred out:** MIDI CC lanes (SR-MIDI-2) can ship here or defer to P10 per capacity.

---

## P8 — Nebula VCS: branch, auto-rebase, semantic merge  *(basic workflow #4 — versioning)*

**Goal.** Living branches, semantic merge, and merge-two-songs — operating on the project text, so
this whole phase is **independent of the audio engine** (only needs P1's model).

**Deliverables.**
- Self-contained text **VCS store**: commits (content-hash), branches, `base=`, branching history +
  coalesced auto-snapshots (SR-VCS-*).
- **Auto-rebase** engine (living branches) with clean/conflict/pin outcomes.
- **Semantic 3-way merge**: field-scoped; note/automation merge **by time**; additions union
  (SR-MERGE-1, SR-MIDI-4).
- **Project merge** (concatenate/overlay), id-namespacing + asset dedupe, tempo-convert (SR-MERGE-2/3/4).
- CLI: `branch/rebase/merge` with machine-readable conflict reports.

**Depends on:** P1. **Parallel with:** P3–P7.

**Test gate.**
- *Unit:* merge cases (add/add, edit/edit same field = conflict, edit/edit different fields = clean,
  delete/edit = conflict, note/automation by-time union); auto-rebase clean **and** conflict paths
  (feature commits never lost); commit-hash determinism.
- *Integration:* fork → edit base → branch auto-rebases; **merge two songs** → golden combined
  `.slp` (concatenate and overlay).
- *Manual smoke:* `solaris branch … && … && solaris merge a.slp b.slp --into big.slp`.

**Deferred out:** cross-app embed propagation (P9).

---

## P9 — Embedding / nested import + bounce-in-place

**Goal.** Nest one Solaris project into another and cache a bounced mixdown for cheap playback.

**Deliverables.**
- **Nested import** (SR-EMBED-1): embedded project appended on the timeline after the host's last
  item; its mixer tracks stay separate with **their own master routed into the host master**.
- **Branch-following propagation** (SR-EMBED-2): source branch advances → host auto-rebases the embed
  (clean → update; conflict → flag).
- **Bounce-in-place / freeze** (SR-RENDER-5, SR-EMBED-3): cache a track/master mixdown clip; playback
  uses the bounce with a **live/bounced indicator**.
- **Interstellar-facing surface** (SR-EMBED-4): expose master mixdown + named stems following a
  branch.

**Depends on:** P4 (render) + P8 (VCS/embed). **Parallel with:** —.

**Test gate.**
- *Nesting:* nested render == the standalone render routed into the parent master (within tolerance).
- *Bounce parity:* bounced playback == live render of the same region.
- *Propagation:* advancing the source branch updates the host (clean) or flags (conflict).
- *Manual smoke:* embed song-B into song-A, render, then edit B and confirm A updates.

**Deferred out:** Interstellar-side integration tests (belong to Interstellar's plan).

---

## P10 — Advanced features  *(later; each independent on the stable core)*

Layer these onto the ≤P7 core, one at a time, each with its own golden/behavior tests:

- **Time-stretch / pitch-shift** on audio clips (SR-CLIP-3) — new DSP module; pitch-preserving tempo
  change (SR-MERGE-4).
- **Sampler** instrument (SR-INST-2).
- **Per-note expression / microtuning** (SR-MIDI-1) and **MIDI CC lanes** (SR-MIDI-2, if not in P7).
- **SMF import/export** (SR-MIDI-3).
- **Tempo map / meter map** (SR-TIME-5) — the reserved schema is filled in.
- **VST3** hosting (SR-RACK-3) — much later.

---

## Cross-cutting test strategy

- **Golden renders** (deterministic, SR-NFR-1) are the backbone: a growing corpus of `.slp` →
  reference-WAV (or reference-hash) cases; every audio phase adds to it; a golden mismatch fails CI.
- **Round-trip serialization** (P1) guards the format on every change.
- **Offline↔RT parity** (P5) guards that real-time doesn't drift from the tested offline path.
- **VCS property tests** (P8): merge/rebase invariants (never lose a commit; clean+conflict paths).
- **RT-safety audit** (P5): callback allocates/locks nothing; xrun = 0 under normal load.
- **Per phase**: all `ctest` targets green **and** the manual CLI smoke passes before starting the
  next phase. Independent phases (P2, P8) keep their own green gate.

## Open decisions to settle before the phase that needs them

- **D1 (before P5) — Real-time device backend.** Options: **RtAudio** (recommended for v1 — small,
  permissive, one API over ALSA/JACK/PulseAudio, multi-device), **PortAudio** (similar), or **JACK**
  (best fit for the pro **matrix routing** + **multi-device** + inter-app goals of SR-OUT-2/SR-ROUTE-4,
  but a heavier runtime dependency). Recommendation: RtAudio for P5 baseline, with a JACK backend as
  a first-class option for P7b's pro routing. The engine stays backend-agnostic (a thin device
  interface), so this is swappable.
- **D2 (before P8) — how much Nebula is Solaris-local vs the shared library.** Build the VCS/merge in
  a shared **Nebula** module (so Interstellar reuses it) vs a Solaris-scoped implementation lifted
  later. Recommendation: shared module, but a Solaris-scoped subset is acceptable if the frozen
  contracts ([shared-core.md](../../docs/shared-core.md)) are honored so it isn't a fork.
- **D3 (before P4) — solo policy** (solo-in-place vs AFL) and **pan law** (−3/−4.5/−6 dB) — fix in the
  P4 design so the mixer golden tests are stable.

See [requirements.md](requirements.md) for the numbered requirements and
[prerequisites.md](prerequisites.md) for the define/implement groundwork this plan realizes.
