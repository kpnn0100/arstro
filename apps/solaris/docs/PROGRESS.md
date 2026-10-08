# Solaris — Progress Ledger

**The only authority on what is done and what is next.** Committed, so work resumes on any machine.
A session reads **NEXT**, does one task, updates this file, and commits — in the same commit.

- Rules: `.claude/skills/arstro.rule` · `.claude/skills/arstro.design.rule`
- Skill: `.claude/skills/arstro.solaris.implement` (the DSP half: `arstro.dsp.implement`)
- Intent: [`../REQUIREMENTS.md`](../REQUIREMENTS.md) · As-built: [`requirements.md`](requirements.md)
  · Format: [`project-format.md`](project-format.md) · Audio: [`../../../docs/audio-format.md`](../../../docs/audio-format.md)
  · Architecture: [`architecture.md`](architecture.md) · Defects: [`DEFECTS.md`](DEFECTS.md)
  · Why it is shaped like this: [`discussion.md`](discussion.md)
- Legend: `[ ]` not started · `[~]` in progress · `[x]` done + verified · `[!]` done but UNVERIFIED

*Last updated: 2026-10-08 — U3: the mixer dock — pages, the master, the matrix, device panels.*

---

## NEXT

**► U4 — the two halves talk**: linked selection (R-MIX-8) — a clip selected lights its strip in
the dock and scrolls it into view; a strip selected lights its clips on the lanes. Arrange by Lane
| Channel (R-LANE-2): one toggle regroups the same clips into a row per strip, and a clip dragged to
another row there is `clip move --strip`. Drag and drop onto the dock: an effect from the browser
onto a strip's rack → `device add <ch> --type <t> --at <slot>`; a sample onto a strip → a clip on
that strip. The audit panel (R-MIX-10): `audit` as a list in the dock, each finding naming what to
click. Read `app/NOTES.md` first.

---

## The build, in order

Each row is one commit (two where a submodule is involved: the submodule first, pushed, then the
umbrella's pointer — `arstro.rule` §7).

### S — specification
- [x] **S0** Second specification (`REQUIREMENTS.md`), `.slp` format, architecture, this ledger, the
      `arstro.solaris.implement` skill; the first specification archived in `docs/history/`.

### D — the sound, in the DSP library (R-DSP, R-INST, R-FX)
- [x] **D1** Primitives: `Biquad` (RBJ) + `ParametricEQ`; `StateVariableFilter` (TPT); `Noise`
      (seeded); `DecayEnvelope` (exponential). `## Math` in each README; unit + integration tests.
      — DSP `1256c64` (REQ-eq-1/2, REQ-svf-1/2, REQ-noise-1, REQ-decay-1); measured against
      formulas recomputed in Python: EQ 0.000 dB, SVF 0.042 dB, decay −298.5 dB/s vs −300.
- [x] **D2** Instruments: `BasicSynth` (2 OSC → SVF → amp ADSR, filter ADSR, unison, noise);
      `DrumMachine` (10 synthesized pads on GM notes, hat choke). — DSP `1fd677f` (REQ-inst-1,
      REQ-synth2-1…5, REQ-drum-1…5); equal temperament to 0.026 cents, kick settles on
      48·2^(tune/12) Hz, choke > 60 dB, every pad sounds and stops, renders byte-identical.
- [x] **D3** `Device` interface + `DeviceRegistry`: synth, drums, compressor, eq, reverb, delay,
      chorus, drive, filter — names, units, ranges, defaults, choices; one factory. — DSP `60a4090`
      (REQ-device-1…4); every parameter of every type at both ends renders finite; a name-written
      parameter matches the RBJ formula to 0.000 dB. DR-DSP-1, DR-INST-1/2, DR-FX-1 written.

### M/E — model and engine
- [x] **M1** `solaris_model`: the `.slp` (parse, serialize fixed point, validate, repair). — DR-FMT-1,
      DR-MIX-4; `solaris_model` 8 tests (fixed point over every node type + unknowns + comments;
      forward-only routing refused naming both ends; 24 structural refusals; 4 repairs counted).
      Device parameters are checked against the registry by the core (V1), not the model.
- [x] **E1** `solaris_engine`: `MixGraph` → devices, strips in mixer order, sends, ports, master;
      sample-accurate notes; pan/fade laws equal to Interstellar's; meters; deterministic.
      — DR-ENG-1, DR-MIX-11; `solaris_engine` 8 tests; its chunking test found a DSP bug (the
      synth's shared noise), fixed in DSP `a16e972`.

### V — the service (rung 1 → 4)
- [x] **V1** `SolarisService` + grammar table + events + `AppModel` + codec; `solaris-cc`; L2 tests.
- [x] **V2** `render` to WAV (master, stems, per port) through the host's writer; audio clips decoded
      through the host's decoder.
- [x] **V3** `api --json|--md` generated, `docs/api.json` + `docs/API.md` committed, drift test.
      — V1–V3 landed as one commit (render needs the host's codecs, the document needs the
      registry and the grammar): DR-SVC-1…3, DR-MIX-2/7/8/9/10, DR-CLIP-2/3, DR-RENDER-1, DR-API-1;
      `solaris_service` 11 L2 tests, `solaris_api_current`; rung 4. End to end with the real CLI:
      drums + synth bass + a sample + a reverb bus, rendered and measured.

### H/P — host and live sound
- [x] **H1** Host: FFmpeg decode, WAV writer (`host/AudioFiles.cpp`); settings + recents files,
      folder listing, device enumeration through PulseAudio (`host/Machine.cpp`). — DR-SET-1,
      DR-HOME-1, DR-BROWSE-1; `solaris_service` 12 tests; on this machine `devices list` finds 2
      outputs (analog, HDMI) and 2 inputs (Brio mic, analog).
- [x] **P1** Real-time playback on one device (the clock): transport, meters, param edits through
      lock-free queues, structural edits by graph swap. — DR-PLAY-1/2/3; live = offline sample for
      sample; edits heard; the audio path allocates nothing (counted); DSP `317bed2`. Plays on this
      machine through PulseAudio.
- [ ] **P2** Several devices: followers through an adaptive resampler (a DSP module), latency
      compensation, per-device drift/xrun/latency in the model.

### U — the UI (cosmo + interstellar family, teal accent)
- [x] **U1** Home + Settings (devices, rate, buffer, folders, port map). — DR-UI-1, DR-SET-2,
      DR-UI-6; `solaris_app_shots` (9 states × 2 sizes, looked at) and `solaris_app_ui` (5 tests,
      every transition caught mid-tween) over the REAL service; the window runs (`solaris`). The port
      map is a `settings set port.<name>=…` line today; its chips come with P2's several devices.
- [x] **U2** Project view: browser, lanes + clips, transport. — DR-UI-3, DR-BROWSE-2, DR-UI-7;
      `clip add --instrument` makes every drop one command (R-BROWSE-3); D-1 found and closed (a
      refused edit announced changes). `solaris_service` 17 tests, `solaris_app_ui` 9 (a tab's
      cross-fade, a clip arriving, moved and removed from a shell — each caught mid-tween; two
      mutants that snap fail it), `solaris_app_shots` 14 states × 2 sizes, looked at. The version
      chip waits for X1.
- [x] **U3** Mixer dock: a tab per mixer, strips (fader, pan, M/S, meter, rack chips, sends), the
      master, the Matrix tab; device panels generated from the registry. — DR-UI-8, DR-UI-5,
      DR-MIX-4 (`targetsOf`, `strips[].targets`); R-MIX-12 amended (groups start open). Cosmo's
      `ContextMenu` and `SliderRow` reused, `SliderRow` with an opt-in formatter. `solaris_service`
      17, `solaris_app_ui` 11 (a fader follows the pointer, a shell's gain travels — a 0 ms mutant
      fails it —, mute eases, the route menu is the service's list, tabs slide, a fold and the dock
      ease, + Effect, a generated panel, a matrix send), `solaris_app_shots` 21 × 2, looked at; the
      window runs. Waveforms and the meters' peak hold wait for U4/P2.
- [ ] **U4** Linked selection; Arrange by Channel; drag and drop onto the dock (an effect onto a
      rack, a sample onto a strip); the audit panel; waveforms (a peaks hook).
- [ ] **U5** Note editing: piano roll for the synth, step grid for the drum machine (views of a
      pattern).

### T — tasks found on the way
- [ ] **T1** Promote Interstellar's `TextFit`, `EasedScroll`, `Glyphs`, `AnimatedRows`, `FadePage` into Artboard (via
      `implement_artboard`); both apps include them from there.

### X — later (specified, not scheduled)
- [ ] **X1** Versions (R-VER) · [ ] **X2** Recording (R-REC) · [ ] **X3** Automation (R-AUTO)
- [ ] **X4** Equivalence test + control socket (rung 5)

---

## Decisions log (newest first)

- **2026-10-08 — the fader law is gain ∝ position², +6 dB at the top** (0 dB at 70 %, −24 dB at
  a quarter): the usual console taper, so the useful range takes most of the travel. A value set
  above +6 from a shell sits at the top.
- **2026-10-08 — a fold group starts open** (R-MIX-12 amended): a new song's few strips must not
  hide behind a click; the fold is for many one-shots, and it is the view's, like zoom.
- **2026-10-08 — cosmo's `SliderRow` grew an opt-in formatter**, rather than Solaris forking a
  slider: one more hook on the shared widget, cosmo's rows unchanged (its tests pass).

- **2026-10-08 — a strip's default colour comes from its id, not its place** (R-UI-7): by place,
  deleting one strip recoloured every later one's clips in one frame; and the mixer (U3) must agree
  with the lanes, so the service resolves it.
- **2026-10-08 — the browser's tabs are Samples · Instruments · Song** (R-BROWSE-1 amended): named
  for what is in them. Presets join the Instruments tab when a preset store exists.
- **2026-10-08 — an instrument drop is `clip add --instrument`**, not `strip add` then `clip add`:
  R-BROWSE-3 says every drop is one command, and two lines can half-land.

- **2026-10-08 — the accent is `#159387`, not `#5AEDDE`** (R-UI-2 amended): the hue-rotation recipe
  keeps HSL lightness, and teal at cosmo's lightness is three times as luminous — white on it is
  1.4:1. Matched to cosmo blue's luminance instead (white on it 3.8:1).

- **2026-10-08 — `render --out` is the MASTER BUS** (after its rack and gain), not "whatever reaches
  the Main port": a strip routed straight to a port (a headphone cue) is not part of the mixdown.
  `--ports` writes what each port receives.
- **2026-10-08 — a new clip with no `--lane` gets a new lane** (audio and notes alike), the CLI form
  of "dropped into empty space" (R-BROWSE-3). `--lane` puts it on an existing one.
- **2026-10-08 — a new source strip feeds the first bus on a LATER mixer** (that is "Main" in a new
  song); with none, the master. No header field names the default — renaming Main keeps it working.
- **2026-10-08 — Solaris has its own small `Json`**, the same shape as Interstellar's: that one lives
  in `interstellar_core`, which hosts all of Cosmo; a shared utility library is an Interstellar
  refactor, out of a Solaris task's scope.

- **2026-10-08 — the engine warms every device with a block of silence.** The DSP library smooths
  each parameter write over a block (`SignalProcessor`), so a freshly built device ramps from its
  default to the project's value over the first 128 samples of every render. Warming finishes the
  ramp before time zero. The alternative — a "snap" API in the library — would have touched every
  `SignalProcessor`; this touches none.
- **2026-10-08 — a muted strip sends nothing, pre-fader sends included.** Simplest rule a user can
  predict; revisit if a cue-mix workflow needs pre-fader sends to survive mute.

- **2026-10-08 — the open questions of the discussion, closed with the recommended defaults** when
  the user said "implement": one clip → one strip (Q-MIX-1); dragging in Arrange-by-Channel
  re-routes (Q-MIX-2); the mixer docks under the timeline (Q-MIX-3); a sample file gets its own
  strip (Q-MIX-4, the user's); routing is forward-only (Q-MIX-5); a new strip feeds "Main" on Mixer 2
  (Q-MIX-6); PulseAudio API first, Solaris's own drift correction (Q-DEV-1 — this machine runs real
  PulseAudio, not PipeWire, so PipeWire's adaptive followers cannot be relied on); versions are
  Interstellar's R-VER (§3); accent teal `#5AEDDE` (Q-LOOK-1); the touch brief is retired. Each is
  one line to change if the user disagrees.
- **2026-10-08 — parameters in engineering units, not 0..1** (R-DSP-3, changes SR-PARAM-2).
- **2026-10-08 — media by relative path, content hash deferred** (R-FMT-3, changes SR-FMT-3).
