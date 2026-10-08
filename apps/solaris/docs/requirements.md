# Solaris — Requirements (as-built tier)

**What the code contractually does today.** `DR-<AREA>-<n>`, descriptive present, `file:line`
anchors, each citing the `R-` tag it implements ([`../REQUIREMENTS.md`](../REQUIREMENTS.md)). An
entry with a dead anchor is a defect, and a behaviour with no entry does not ship.

## Conformance

Rung **4** of `arstro.rule` §5 — the generated API document is committed and drift-tested
(DR-API-1). The ladder:

| rung | means | lands with |
|---|---|---|
| 0 | spec only | done (S0) |
| 1 | core split out; `Command`/`Event`/model with one text codec | done — DR-SVC-1 |
| 2 | registered with the root `ctest`; L2 headless service tests | done — `solaris_service` |
| 3 | a real CLI that is the whole app without a window | done — `solaris-cc`, DR-SVC-3 |
| 4 | generated API document, committed, drift-tested | ← **here** — DR-API-1 |
| 5 | control socket + the GUI/headless equivalence test | X4 |

## Entries

Anchors without a path prefix are under `apps/solaris/`. Anchors into the DSP library are relative to `core/DigitalSignalProcessing/` — that is where
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

### DR-FMT-1 The `.slp` document (R-FMT-1, R-FMT-2, R-FMT-4)
`solaris_model` (`model/Project.h`) holds the document as typed data — header, `#aport`,
`#amixer`, `#atrack` (strip), `#asend`, `#arack`/`#aeffect`, `#alane`, `#apattern`/`#note`,
`#aclip` — and links nothing but the standard library: a device is its registry `type` plus its
parameters as text, which the core checks against the DSP registry. `parseProject`
(`model/Project.cpp:292`) reads the suite grammar: header `key = value`, node lines, indented
continuation lines, whole-line and inline `;` comments (kept with the node they follow), unknown
keys (kept in order) and unknown nodes (kept verbatim with their indented lines). The suite's
inline `#note`s under an `#aclip` become a pattern of their own — the one normalisation, reported.
`serializeProject` (`model/Project.cpp:516`) writes §10's canonical form: `canonicalNumber`
(`model/Format.cpp:33`, shortest round-trip, always a point), `canonicalBeats` (`:52`, rounded to
1/960 beat, fewest decimals that read back to the tick), seconds to the microsecond; defaults of
optional fields are omitted. **Parse → serialize is a byte-exact fixed point** for canonical text.
Refused, each naming what and where (`validateProject`, `model/Project.cpp:623`): duplicate ids,
`master` as an id, unknown strip kinds, dangling references, an audio clip on a non-audio strip, a
note clip on a non-instrument strip, `in ≥ out`, a clip before the song, two racks for one strip,
an input port as a destination, a header that is not `app = solaris` / `timebase = beats` /
`ppq = 960`. Repaired and counted (`Reader::num`, `:165`): a non-finite or unreadable number → the
field's default; out-of-range pan, pitch and velocity clamp. Guarded by `solaris_model` (8 tests;
mutants checked: dropping unknown keys on write breaks the fixed point).

### DR-MIX-4 Routing only goes forward (R-MIX-4)
`validateProject`'s `checkTarget` (`model/Project.cpp:640`) accepts an `out` or a send target only
when it is `master`, an output port, or a strip whose mixer's `order` is GREATER than the source
strip's; anything else is refused as `ch_1 (Main, on Buses) output → ch_2 (kick, on Sources): a
strip can only feed a strip on a LATER mixer, the master or a port (R-MIX-4)`. A file that routes
backward does not load. Because of this, `Project::stripsInOrder` (`model/Project.cpp:97`) —
mixer order, then strip order — is a topological order of the routing graph with no cycle check
at all. A new project is `newProject` (`model/Project.cpp:134`): Sources (`mx_1`), Buses (`mx_2`)
holding the bus Main (`ch_1` → master), the port Main (`prt_1`) fed by the master (R-MIX-3).
Guarded by `test_routing_only_goes_forward` (mutant checked: `<=` → `<` lets a same-mixer route
through and the test fails).

### DR-ENG-1 The engine renders a MixGraph through the DSP library's devices (R-MIX-1/5/6, R-DSP-1/5, R-RENDER-1)
`solaris_engine` knows no project and no file: it renders a `MixGraph` (`engine/MixGraph.h`) —
strips in processing order with their devices (registry types + values), audio regions in samples
over decoded PCM, note events in samples, an output and sends as forward indices, the master's rack
and gain, the ports. `Engine::build` (`engine/Engine.cpp:72`) refuses a target that is not LATER
(R-MIX-4, a second time — the graph might not come from the model), a missing port, an unknown
device type or parameter, an instrument strip whose rack does not start with an instrument; sets
the DSP library's process-wide sample rate (R-NFR-7); builds every device with
`DeviceRegistry::create`; and **warms** each with one block of silence (`warm`, `:47`), because the
library smooths every parameter write over a block and the first block of every render would
otherwise carry a ramp from the device's default. `renderPiece` (`:206`) per strip in order: sum
what earlier strips routed/sent to it; add its regions (looped, offset, offline = silent) or play
its instrument with the block **split at every note event** (`:249`, R-DSP-5); run the rest of its
rack (bypass skips); tap pre-fader sends; fader + balance pan; post-fader sends; add into its
output (`route`, `:179` — master, a later strip, or a port; a mono port gets the average of L and
R). A silent strip (muted or solo-silenced, decided by the core) sends nothing anywhere (`:276`).
Then the master rack, master gain, its ports. Meters (peak, RMS per render call, max peak since
`clearPeaks`), a captured strip for stems, live parameter writes, `seek` (instruments reset, effect
tails ring on). Guarded by `solaris_engine` (8 tests): a region sounds from its first sample to its
last; mute/pre/post/port routing; refusals; **a note moved one sample later renders exactly one
sample later, to the bit** (mutant checked: applying events at block starts fails it); **two engines
rendering in chunks of 128 and of 77 are byte-identical** over a second of drums, synth, chorus,
compressor, reverb and EQ — which is how DSP `a16e972` (the synth's shared noise) was found.

### DR-MIX-11 The mix laws are Interstellar's (R-MIX-11)
`engine/MixLaws.h`: dB → linear `10^(dB/20)`; `balancePan` (`:24`) — unity at centre, the side
panned away from falls on a quarter cosine; `fadeGain` (`:32`) — linear in amplitude — the same
formulas as `apps/interstellar/render/AudioMix.cpp`. Guarded by `test_pan_and_fades_follow…`, which
checks a 0.5 region at pan +0.5, −6 dB to 1e-7 against the closed forms.

### DR-SVC-1 One way in, one way out (R-SVC-1, R-SVC-2, R-G-4)
`SolarisService` (`core/service/SolarisService.h`) is the application: `dispatchText(line)` parses
with the grammar TABLE (`commandSpecs`, `core/service/Command.cpp:26`; `parseCommand`, `:199` —
longest verb match, flags checked against the row) and `dispatch` (`core/service/SolarisService.cpp:76`)
runs it; out come the `AppModel` (`core/service/AppModel.h`, refreshed after every command) and
`Event`s whose `formatEvent` text is the log line. **Every edit is all-or-nothing**: the project is
copied first, the command runs, `validateProject` runs, and any failure restores the copy
(`:173`) and refuses with the validator's sentence — a refused command changes nothing, and a
`set` line's `params.changed` events are emitted only once the whole line has landed. The core
holds no codec and no device API: decoding and WAV writing are `Host` functions (R-SVC-4).

### DR-SVC-2 Unknown input is refused, naming it (R-SVC-3)
An unknown verb or flag fails in `parseCommand` with the nearest candidates (`strp add` →
`did you mean: strip add?`). An address goes through `setAddress` (`core/service/SolarisService.cpp:353`):
an unknown node, field or DEVICE PARAMETER is refused with the nearest name (`set
dv_1.filter.cutof=1` → `did you mean: filter.cutoff?`); a value out of range is refused with the
range; a device parameter's value is read against the DSP registry — a choice by name (listing the
choices on a miss), a number clamped to the spec — and stored as its canonical text. `get`
(`:495`) reads every field `set` writes plus derived ones (`<clip>.length` resolved, `<strip>.out`,
a device parameter not stored = the registry default). A refusal is a `command.rejected` event and
`lastError`.

### DR-SVC-3 solaris-cc is the whole app without a window (R-SVC-1)
`cli/main.cpp` holds no behaviour: argv, stdout, the host's file functions; ` : ` chains lines,
`--script` reads them, `--watch` streams events, a refusal exits 3. End to end (2026-10-08): a
drum pattern + a synth bass + a 44.1 kHz sample + a reverb bus built entirely by command, saved,
audited, rendered with stems; measured — a drum hit on every beat with silence before it, the 4-beat
pattern looping across an 8-beat clip, the bass C2 at 65.4 Hz, the sample resampled to 48 kHz and
stored relative to the song's folder.

### DR-MIX-2 Every sample file gets its own strip (R-MIX-2, R-MIX-3)
`clip add --src` (`core/service/ServiceEdit.cpp:360`): a file no clip uses yet gets a new audio strip
named after it on the first mixer, routed by `defaultOutFor` (`:70`) to the first bus on a later
mixer ("Main"), and a new lane; a file already used reuses its strip; `--strip` overrides. The file
is decoded through the host to learn its length (refused if it cannot be read), and stored relative
to the song's folder when inside it (`relativePath`, `core/service/SolarisService.cpp:193`).

### DR-MIX-7 Solo keeps the soloed path alive (R-MIX-7)
`silentStrips` (`core/Compile.cpp:47`): with any strip soloed, a strip is audible only if it is
soloed, reachable downstream of one (its buses, its sends' returns) or upstream of one (what feeds
it); muted strips are silent regardless. The model shows it as `strips[].audible`; the engine gets
it as `silent`.

### DR-MIX-8/9/10 Fed by, the matrix, the audit (R-MIX-8, R-MIX-9, R-MIX-10)
`refreshModel` (`core/service/ServiceModel.cpp:70`) computes each strip's `clipCount`, `fromLanes`
and `fromStrips`. `matrix print [--json]` (`matrixText`, `:260`): rows = strips in processing order,
columns = the buses, master and output ports; `●` = the main output, `-6.0pre` = a send's dB and tap.
`audit` (`:193`): unused strips, strips that reach no output port, single-input and empty buses,
clips on silent strips, offline files, unknown device types and parameters, and any strip or the
master that went over 0 dBFS in the last render.

### DR-CLIP-2/3 Patterns and linked clips (R-CLIP-2, R-CLIP-3)
`compile` (`core/Compile.cpp:131`) expands a note clip: a clip longer than its pattern loops it, a
note is cut at the clip's end (`(C1)`, `:216`); events sort by time with note-offs before note-ons at
one sample (`(C2)`, `:230`). `clip duplicate` makes a second clip of the SAME pattern (`linked` = 2 in
the model); `clip unique` copies the pattern. `note add` replaces a note at the same pitch and tick.

### DR-RENDER-1 Offline render (R-RENDER-1, R-RENDER-2, R-RENDER-3)
`render` (`core/service/ServiceRender.cpp:31`): compile → build the engine → one pass capturing the
master bus (the mixdown, `--out`), any `--stems` (strips' post-fader outputs, `<out>.<ch>.wav`) and
with `--ports` every output port (`<out>.<port>.wav`); 24-bit PCM or `--bits 32f`; `--from`/`--to`
in beats (an explicit `--to` is exact). With no `--to` the tail runs past the song's end until the
first block the master spends below −90 dBFS, capped at 10 s (`:98`). The host writes the files
(`writeWav`, `host/AudioFiles.cpp:116`) and decodes sources through FFmpeg, resampled to the project
rate, mono at unity in both channels (`decodeAudio`, `:24`). Two renders of one song are
byte-identical (`test_render_writes_the_mix_deterministically`).

### DR-API-1 The API document is generated, committed and drift-tested (R-API-1)
`apiJson` / `apiMarkdown` (`core/service/ApiDoc.cpp:43`, `:80`) print, from the tables the code
runs on, every command (usage, summary, the R- tag that asked for it), every event with its fields,
every `AppModel` field (with `stable`), and **every DSP registry device with every parameter's unit,
range, default and choices**. `docs/api.json` and `docs/API.md` are that output, committed;
`solaris_api_current` (`tests/api_current.cmake`) regenerates and compares, failing with the command
that fixes it (checked: an edited summary turns it red). `test_the_dump_and_the_document` fails if
the codec writes a key the field table does not list.

### DR-SET-1 The machine's settings (R-SET-1, R-SET-2, R-DEV-1, R-DEV-3)
`Settings` (`core/Settings.h`) is the MACHINE's, never a song's: `sampleRate` (a new song's default
— `project new` without `--rate` takes it), `bufferSize` (and the model's `settings.latencyMs`),
`output`/`input` device ids, `folder` lines, `port.<name> = <device>:<channel>` (where a song's
logical port plays on this machine). `settings set` (`machineCommand`,
`core/service/ServiceMachine.cpp`) validates every key — an unknown key is refused with the nearest
— and writes the host-given file at once; a second process reads the same file
(`test_the_machine_settings_folders_devices_and_recents`). `folder add/remove/move` keeps the
browser's quick-access list in order; `folder add` refuses a folder the host cannot list.
`devices list` asks the host, which asks the sound server through the PulseAudio API
(`listDevices`, `host/Machine.cpp`) — outputs and inputs, monitors of outputs left out. The CLI and
the window share the files (`machinePaths`: XDG, overridable by `SOLARIS_SETTINGS` /
`SOLARIS_RECENTS`).

### DR-HOME-1 The recent songs (R-HOME-1)
Opening, creating or saving a song under a new path puts it first on the recent list
(`touchRecent`, `core/service/ServiceMachine.cpp`), at most 20, persisted in the host's recents file.
The model's `recents[]` cards carry each song's name, bpm, length and strip count, read from the
files when the list or a song changes (not on every command); a file that is gone or no longer a
song is a card marked `missing`. `recents remove` takes one off the list and leaves the file.

### DR-BROWSE-1 The browser lists a folder (R-BROWSE-1, headless half)
`browse <folder>` asks the host (`listDir`, `host/Machine.cpp`): sub-folders, audio files (by
extension), songs (`.slp`), hidden entries left out; folders first, then by name; the model's
`browser` holds the listing. Drag and drop is the UI's (R-BROWSE-3), over `clip add`.
