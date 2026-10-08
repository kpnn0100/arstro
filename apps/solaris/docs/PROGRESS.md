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

*Last updated: 2026-10-08 — D1: the DSP primitives (EQ, SVF, noise, decay envelope).*

---

## NEXT

**► D2 — the instruments**, in `core/DigitalSignalProcessing` under `arstro.dsp.implement`:
`BasicSynth` (2 OSC → SVF → amp ADSR) and `DrumMachine` (synthesized GM pads, hat choke), built
from D1's primitives and the existing `Oscillator`/`ADSREnvelope`.

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
- [ ] **D2** Instruments: `BasicSynth` (2 OSC → SVF → amp ADSR, filter ADSR, unison, noise);
      `DrumMachine` (10 synthesized pads on GM notes, hat choke).
- [ ] **D3** `Device` interface + `DeviceRegistry`: synth, drums, compressor, eq, reverb, delay,
      chorus, drive, filter — names, units, ranges, defaults, choices; one factory.

### M/E — model and engine
- [ ] **M1** `solaris_model`: the `.slp` (parse, serialize fixed point, validate, repair).
- [ ] **E1** `solaris_engine`: `MixGraph` → devices, strips in mixer order, sends, ports, master;
      sample-accurate notes; pan/fade laws equal to Interstellar's; meters; deterministic.

### V — the service (rung 1 → 4)
- [ ] **V1** `SolarisService` + grammar table + events + `AppModel` + codec; `solaris-cc`; L2 tests.
- [ ] **V2** `render` to WAV (master, stems, per port) through the host's writer; audio clips decoded
      through the host's decoder.
- [ ] **V3** `api --json|--md` generated, `docs/api.json` + `docs/API.md` committed, drift test.

### H/P — host and live sound
- [ ] **H1** Host: FFmpeg decode, WAV writer, settings + recents files, folder listing, device
      enumeration (PulseAudio).
- [ ] **P1** Real-time playback on one device (the clock): transport, meters, param edits through
      lock-free queues, structural edits by graph swap.
- [ ] **P2** Several devices: followers through an adaptive resampler (a DSP module), latency
      compensation, per-device drift/xrun/latency in the model.

### U — the UI (cosmo + interstellar family, teal accent)
- [ ] **U1** Home + Settings (devices, rate, buffer, folders, port map).
- [ ] **U2** Project view: browser, lanes + clips, transport, version chip placeholder.
- [ ] **U3** Mixer dock: a tab per mixer, strips (fader, pan, M/S, meter, rack chips, sends), the
      master, the Matrix tab; device panels generated from the registry.
- [ ] **U4** Drag and drop from the browser; linked selection; Arrange by Channel; the audit panel.
- [ ] **U5** Note editing: piano roll for the synth, step grid for the drum machine (views of a
      pattern).

### X — later (specified, not scheduled)
- [ ] **X1** Versions (R-VER) · [ ] **X2** Recording (R-REC) · [ ] **X3** Automation (R-AUTO)
- [ ] **X4** Equivalence test + control socket (rung 5)

---

## Decisions log (newest first)

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
