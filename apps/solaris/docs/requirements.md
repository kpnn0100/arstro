# Solaris — Requirements (as-built tier)

**What the code contractually does today.** `DR-<AREA>-<n>`, descriptive present, `file:line`
anchors, each citing the `R-` tag it implements ([`../REQUIREMENTS.md`](../REQUIREMENTS.md)). An
entry with a dead anchor is a defect, and a behaviour with no entry does not ship.

## Conformance

Rung **0** of `arstro.rule` §5 — specified, no code yet. The ladder and where each rung lands:

| rung | means | lands with |
|---|---|---|
| 0 | spec only | ← **here** (S0) |
| 1 | core split out; `Command`/`Event`/model with one text codec | V1 |
| 2 | registered with the root `ctest`; L2 headless service tests | V1 |
| 3 | a real CLI that is the whole app without a window | V2 |
| 4 | generated API document, committed, drift-tested | V3 |
| 5 | control socket + the GUI/headless equivalence test | X4 |

## Entries

Anchors into the DSP library are relative to `core/DigitalSignalProcessing/` — that is where
Solaris's sound lives (R-DSP-1).

### DR-DSP-1 Every instrument and effect is a registry entry in the DSP library (R-DSP-1, R-DSP-2, R-DSP-3)
`DeviceRegistry::types()` (`src/device/DeviceRegistry.cpp:376`) holds nine `DeviceType`s
(`src/device/Device.h:46`) in a stable order — `synth`, `drums`, `compressor`, `eq`, `reverb`,
`delay`, `chorus`, `drive`, `filter` — each with its `ParamSpec`s (`src/device/Device.h:27`): name,
label, unit, range, default, choices. A `Device` (`src/device/Device.h:59`) is the one face a host
drives: `process` (effect in place, instrument adds), notes, `reset`, `setParam` by index or by name
in engineering units. `ParamSpec::clamp` (`src/device/DeviceRegistry.cpp:19`) is the write rule:
into range, rounded for integers and choices, non-finite → default; an unknown name changes nothing
and returns false. The instruments' defaults are read from the instruments, so registry and
instrument cannot disagree. **Solaris does not consume it yet** — that lands with E1/V1. Guarded in
the DSP repo by `unittest/deviceTests.cpp` (a sweep of every parameter of every type at both ends
renders finite) and `registry_eq_by_name` (a parameter written by name matches the RBJ formula to
0.000 dB).

### DR-INST-1 The Basic Synth (R-INST-1)
`BasicSynth` (`src/instrument/BasicSynth.cpp:171` renders): per voice two `Oscillator`s (waveform,
octave, semitone, cents, level, unison + detune) and `Noise` → a `StateVariableFilter` swept per
sample by its own ADSR and key tracking in octaves (`cutoffFor`, `:92`) → an amplitude ADSR. 16
voices; the same note retriggers, else an idle voice, else the oldest is stolen (`noteOn`, `:99`).
The oscillators' own ADSRs are gates, so the release tail is never cut. Registry type `synth`,
parameters `osc1.*`, `osc2.*`, `noise`, `filter.*`, `fenv.*`, `amp.*`, `volume`, `velocity`.
Guarded by the DSP repo's `instrumentTests.cpp` and `synth2_equal_temperament` (0.026 cents).

### DR-INST-2 The Drum Machine (R-INST-2)
`DrumMachine` (`src/instrument/DrumMachine.cpp:210`, one model per pad): kick 36, rim 37, snare 38,
clap 39, low tom 41, closed hat 42, mid tom 45, open hat 46, high tom 48, cowbell 56, each
synthesized from `Phasor`, seeded `Noise`, `StateVariableFilter` and `DecayEnvelope` — no samples.
Per pad `tune`, `decay`, `tone`, `level`, `pan` (registry `kick.tune` … `cowbell.pan`, plus
`volume`); the closed hat chokes the open hat (`:185`); `reset()` reseeds, so a pattern replays
byte-identically. Guarded by `instrumentTests.cpp` and `kick_settles_on_its_tuned_fundamental`.

### DR-FX-1 The basic effects (R-FX-1…4)
Registry types `compressor`, `eq`, `reverb`, `delay`, `chorus`, `drive`, `filter`. `eq` is
`ParametricEQ` (`src/equalizer/ParametricEQ.cpp:5`): seven `Biquad` bands built by the RBJ cookbook
(`src/equalizer/Biquad.cpp:46`) — low cut, low shelf, three peaks, high shelf, high cut — transparent
when fresh. `filter` is the synth's SVF with a mix (`src/device/DeviceRegistry.cpp:328`). The rest
wrap `Compressor`, `Reverb`, `Repeater`, `Chorus` and `Overdrive` unchanged
(`EffectDevice`, `src/device/DeviceRegistry.cpp:221`). R-FX-5 (a strip's rack) is Solaris's and is
not built yet.
