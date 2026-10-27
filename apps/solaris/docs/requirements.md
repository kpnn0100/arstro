# Solaris — Requirements (as-built tier)

**What the code contractually does today.** `DR-<AREA>-<n>`, descriptive present, `file:line`
anchors, each citing the `R-` tag it implements ([`../REQUIREMENTS.md`](../REQUIREMENTS.md)). An
entry with a dead anchor is a defect, and a behaviour with no entry does not ship.

## Conformance

Rung **5** of `arstro.rule` §5 — the generated API document is committed and drift-tested
(DR-API-1), and one script through the CLI and a live window gives one answer (DR-SVC-5). The ladder:

| rung | means | lands with |
|---|---|---|
| 0 | spec only | done (S0) |
| 1 | core split out; `Command`/`Event`/model with one text codec | done — DR-SVC-1 |
| 2 | registered with the root `ctest`; L2 headless service tests | done — `solaris_service` |
| 3 | a real CLI that is the whole app without a window | done — `solaris-cc`, DR-SVC-3 |
| 4 | generated API document, committed, drift-tested | done — DR-API-1 |
| 5 | control socket + the GUI/headless equivalence test | ← **here** — DR-SVC-5 (C4b) |

## Entries

Anchors without a path prefix are under `apps/solaris/`. Anchors into the DSP library are relative to `core/DigitalSignalProcessing/` — that is where
Solaris's sound lives (R-DSP-1).

### DR-DSP-1 Every instrument and effect is a registry entry in the DSP library (R-DSP-1, R-DSP-2, R-DSP-3)
`DeviceRegistry::types()` (`src/device/DeviceRegistry.cpp:445`) holds eleven `DeviceType`s
(`src/device/Device.h:65`) in a stable order — instruments `synth`, `drums`, `sampler`, then effects
`compressor`, `eq`, `reverb`, `delay`, `chorus`, `drive`, `filter`, `limiter` — each with its `ParamSpec`s
(`src/device/Device.h:27`): name, label, unit, range, default, choices. A `Device`
(`src/device/Device.h:87`) is the one face a host drives: `process` (effect in place, instrument adds),
notes, `reset`, `setParam` by index or by name in engineering units, and `latency()` (`:125`, DSP
REQ-device-8: the samples its output lags — the limiter's lookahead, 0 for the rest; Solaris compensates
it, DR-MIX-17). `ParamSpec::clamp` (`src/device/DeviceRegistry.cpp:21`) is the write rule:
into range, rounded for integers and choices, non-finite → default; an unknown name changes nothing
and returns false. The instruments' defaults are read from the instruments, so registry and
instrument cannot disagree. Solaris consumes it whole: the engine builds every device from it
(DR-ENG-1), the service reads its parameters for the `.slp`, the addresses and the API document, and
the device windows are generated from it (DR-UI-5). Guarded in
the DSP repo by `unittest/deviceTests.cpp` (a sweep of every parameter of every type at both ends
renders finite) and `registry_eq_by_name` (a parameter written by name matches the RBJ formula to
0.000 dB).

### DR-INST-1 The Basic Synth (R-INST-1)
`BasicSynth` (`src/instrument/BasicSynth.cpp:177` renders): per voice two `Oscillator`s (waveform,
octave, semitone, cents, level, unison + detune) and `Noise` → a `StateVariableFilter` swept per
sample by its own ADSR and key tracking in octaves (`cutoffFor`, `:98`) → an amplitude ADSR. 16
voices; the same note retriggers, else an idle voice, else the oldest is stolen (`noteOn`, `:105`).
The oscillators' own ADSRs are gates, so the release tail is never cut. Registry type `synth`,
parameters `osc1.*`, `osc2.*`, `noise`, `filter.*`, `fenv.*`, `amp.*`, `volume`, `velocity`.
Guarded by the DSP repo's `instrumentTests.cpp` and `synth2_equal_temperament` (0.026 cents).
**A live volume** (D-8; DSP `0707803`, REQ-synth2-4 amended): the velocity is the note's; the volume is applied per
block, ramped — set or automated while a note sounds, it moves it (a riser can swell). The Sampler's level likewise.
**Stereo unison** (R-INST-1 amended; DSP `94d259f`, REQ-synth2-6): `osc1/2.width` (0…50 ct, default 0) detunes the
channels apart by the Oscillator's per-channel offset, and unison voices start at spread phases (frac(v/φ), the
same on every channel) — a supersaw is wide (width 20: L/R correlation 0.44) and its attack no longer spikes
(0.9× the sustained level, against 7.8× aligned); width 0 is mono-identical, so saved songs sound as they did.
In the analog face the Width knob joins each oscillator's module.

### DR-INST-2 The Drum Machine (R-INST-2)
`DrumMachine` (`src/instrument/DrumMachine.cpp:210`, one model per pad): kick 36, rim 37, snare 38,
clap 39, low tom 41, closed hat 42, mid tom 45, open hat 46, high tom 48, cowbell 56, each
synthesized from `Phasor`, seeded `Noise`, `StateVariableFilter` and `DecayEnvelope` — no samples.
Per pad `tune`, `decay`, `tone`, `level`, `pan` (registry `kick.tune` … `cowbell.pan`, plus
`volume`); the closed hat chokes the open hat (`:185`); `reset()` reseeds, so a pattern replays
byte-identically. Guarded by `instrumentTests.cpp` and `kick_settles_on_its_tuned_fundamental`.

**Crash and ride** (R-INST-2 amended; DSP `79648e7`, REQ-drum-6): crash 49 (K12, 1800 ms: the hats' squares and more
noise, a 3.5 kHz band and a 5 kHz sheen, a burst over a wash) and ride 51 (K13, 1200 ms: a two-square bell in a
3 kHz band over a wash above 8 kHz), last in the pad order so older pads keep their indices; both 0.55 peaks.
Solaris has them with no code: `crash` and `ride` pad names, `crash.*` / `ride.*` addresses, two more step rows in
a kit's roll, two more pads in the plugin's editor.

### DR-INST-5 The piano (R-INST-5)
Registry type `piano` (DSP `9d6ef27`, REQ-inst-piano-1): the library's physical piano — `PianoEngine`
(`src/physical`, moved there from the DSP's demo app) as the `Piano` instrument (`src/instrument/Piano.cpp`):
16 voices of felt hammer, stiff inharmonic unison strings and a shared bridge and soundboard; velocity
`v/127`; a volume after its soft limiter; a stop lets the keys go (the pedals keep what they hold), a seek is
silence. Its parameters are the engine's voicing bank, titled Hammer (hardness, felt curve, hysteresis, felt
relax), Strings (sustain, brightness, stretch, unison detune, bass growl, attack glide, treble shimmer,
aftersound), Body (soundboard, resonance), Pedals (sustain, soft) and Output (drive, volume). A voice is
stolen longest-released first, so a held bass under an arpeggio keeps sounding; the same notes after a
seek are the same samples as from the start. Solaris has it with no Solaris code (R-DSP-2): `strip add
--instrument piano`, its addresses, the API document, its analog face (shot `piano-window`) and the MIDI
roll all come from the registry; a song's refusal of an unknown instrument now lists `synth piano drums
sampler`.

### DR-FX-1 The basic effects (R-FX-1…4)
Registry types `compressor`, `eq`, `reverb`, `delay`, `chorus`, `drive`, `filter`. `eq` is
`ParametricEQ` (`src/equalizer/ParametricEQ.cpp:5`): seven `Biquad` bands built by the RBJ cookbook
(`src/equalizer/Biquad.cpp:46`) — low cut, low shelf, three peaks, high shelf, high cut — transparent
when fresh. `filter` is the synth's SVF with a mix (`FilterDevice`, `src/device/DeviceRegistry.cpp:397`). The rest
wrap `Compressor`, `Reverb`, `Repeater`, `Chorus` and `Overdrive` unchanged
(`EffectDevice`, `src/device/DeviceRegistry.cpp:266`). The reverb's parameters are `size`, `decay`,
`lowcut`, `highcut`, `width`, `mix` (`reverbBindings`, `:340`) — R-FX-4's pre-delay is not built (`size`
is the network's delay spread, not a pre-delay). R-FX-5, a strip's ordered rack, is built in Solaris:
`device add | remove | move` and `<dv>.bypass` (DR-SVC-1, the rack drawn as chips, DR-UI-8).

### DR-FMT-1 The `.slp` document (R-FMT-1, R-FMT-2, R-FMT-4)
`solaris_model` (`model/Project.h`) holds the document as typed data — header, `#aport`,
`#amixer`, `#atrack` (strip), `#asend`, `#arack`/`#aeffect`, `#alane`, `#apattern`/`#note`,
`#aclip` — and links nothing but the standard library: a device is its registry `type` plus its
parameters as text, which the core checks against the DSP registry. `parseProject`
(`model/Project.cpp:534`) reads the suite grammar: header `key = value`, node lines, indented
continuation lines, whole-line and inline `;` comments (kept with the node they follow), unknown
keys (kept in order) and unknown nodes (kept verbatim with their indented lines). The suite's
inline `#note`s under an `#aclip` become a pattern of their own — the one normalisation, reported.
`serializeProject` (`model/Project.cpp:964`) writes §10's canonical form: `canonicalNumber`
(`model/Format.cpp:33`, shortest round-trip, always a point), `canonicalBeats` (`:52`, rounded to
1/960 beat, fewest decimals that read back to the tick), seconds to the microsecond; defaults of
optional fields are omitted. **Parse → serialize is a byte-exact fixed point** for canonical text.
Refused, each naming what and where (`validateProject`, `model/Project.cpp:1220`): duplicate ids,
`master` as an id, unknown strip kinds, dangling references, an audio clip on a non-audio strip, a
note clip on a non-instrument strip, `in ≥ out`, a clip before the song, two racks for one strip,
an input port as a destination, a header that is not `app = solaris` / `timebase = beats` /
`ppq = 960`. Repaired and counted (`Reader::num`, `:266`): a non-finite or unreadable number → the
field's default; out-of-range pan, pitch and velocity clamp. Guarded by `solaris_model` (8 tests;
mutants checked: dropping unknown keys on write breaks the fixed point).

### DR-MIX-4 Routing only goes forward (R-MIX-4)
`validateProject`'s `checkTarget` (`model/Project.cpp:1266`) accepts an `out` or a send target only when
it is `master`, an output port, or a strip `feedsForward` (`model/Project.cpp:1172`) allows — one LATER IN
PROCESSING ORDER: on a later mixer, or after it on its own (strip order, then the file's order on a
tie) — the ONE copy of the rule, for outputs, sends and sidechain keys alike; anything else is refused
naming both ends, as `ch_1 (Main, on Buses) output → ch_2 (kick, on Sources): a strip can only feed a
strip LATER in processing order (a later mixer, or after it on its own), the master or a port (R-MIX-4)`.
A file that routes backward does not load. Because of this, `Project::stripsInOrder`
(`model/Project.cpp:160`) — mixer order, then strip order — is a topological order of the routing
graph with no cycle check at all. (**AMENDED (audit, 2026-10-09, R-MIX-4 amended):** `feedsForward` was
"a later MIXER", so a bus made on Buses could not feed Main and every return skipped it.) A reorder
that would point a route backward — `strip move`, `mixer move` — is refused by the same validator
after the command. A new strip whose output is a strip on its OWN mixer is placed just before it
(`placeBeforeItsTarget`, `core/service/ServiceEdit.cpp:73`), so `strip add --kind bus` on Buses feeds
Main: its default output is the bus that LEAVES the nearest later mixer with buses, else the one
leaving its own (`defaultOutFor`, `:101`). `strip move --order <n>` puts the strip at position n of its
mixer and renumbers the mixer 0…n−1 (`placeStrip`, `:61`; `case K::StripMove`, `:302`), as `mixer move`
renumbers mixers — an order never ties. A new project is `newProject` (`model/Project.cpp:235`): Sources (`mx_1`), Buses (`mx_2`)
holding the bus Main (`ch_1` → master), the port Main (`prt_1`) fed by the master (R-MIX-3).
Guarded by `test_routing_only_goes_forward` and `test_forward_is_processing_order_and_moves_renumber`
(`core/tests/serviceTests.cpp:7845`: a bus made on Buses goes before Main and feeds it; a same-mixer route
forward is taken, backward refused naming both ends; `strip move` that would point one back refused;
`--order 0` first and renumbered; mutant checked: the old later-mixer rule refuses the new bus's
default route). The same rule is PUBLISHED: `targetsOf` (`model/Project.cpp:1203`) lists what a strip
may feed — later strips in processing order, `master`, the output ports — as `strips[].targets`
(`core/service/ServiceModel.cpp:286`), and `keyTargetsOf` (`model/Project.cpp:1183`) its strips as `strips[].keyTargets`;
a front end offers exactly that list (the route picker, `MixerDock::openOut`, `app/widgets/MixerDock.cpp:1319`),
and the test routes to every entry offered.

### DR-ENG-1 The engine renders a MixGraph through the DSP library's devices (R-MIX-1/5/6, R-DSP-1/5, R-RENDER-1)
`solaris_engine` knows no project and no file: it renders a `MixGraph` (`engine/MixGraph.h`) —
strips in processing order with their devices (registry types + values), audio regions in samples
over decoded PCM, note events in samples, an output and sends as forward indices, the master's rack
and gain, the ports. `Engine::build` (`engine/Engine.cpp:139`) refuses a target that is not LATER
(R-MIX-4, a second time — the graph might not come from the model), a missing port, an unknown
device type or parameter, an instrument strip whose rack does not start with an instrument; sets
the DSP library's process-wide sample rate (R-NFR-7); builds every device with
`DeviceRegistry::create`; and **warms** each with one block of silence (`warm`, `:114`), because the
library smooths every parameter write over a block and the first block of every render would
otherwise carry a ramp from the device's default; and sizes every latency-compensation delay
(`compensate`, `:288`, DR-MIX-17). `renderPiece` (`:995`) per strip in order: sum
what earlier strips routed/sent to it; add its regions (looped, offset, offline = silent) or play
its instrument with the block **split at every note event** (`:773`, R-DSP-5); run the rest of its
rack (bypass skips); tap pre-fader sends; fader + balance pan; post-fader sends; add into its
output (`route`, `:632` — master, a later strip, or a port, each through its connection's delay; a mono port gets the average of L and
R). A silent strip (muted or solo-silenced, decided by the core) sends nothing anywhere (`:887`).
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
with the grammar TABLE (`commandSpecs`, `core/service/Command.cpp:26`; `parseCommand`, `:571` —
longest verb match, flags checked against the row) and `dispatch` (`core/service/SolarisService.cpp:153`)
runs it; out come the `AppModel` (`core/service/AppModel.h`, refreshed after every command) and
`Event`s whose `formatEvent` text is the log line. **Every edit is all-or-nothing**: the project is
copied first, the command runs, `validateProject` runs, and any failure restores the copy
(`:350`) and refuses with the validator's sentence — a refused command changes nothing. An edit's
events (`project.changed` from `changed`, `:36`; a `set` line's `params.changed`) are HELD in
`mPending` and emitted only once the whole command has landed (`:433`), so a refused command
announces nothing either (D-1). An edit that made nodes prints the id it made first and `made:` with
the rest (DR-SVC-8). And a refusal always says WHY: a path that returns false without a
reason is caught in `dispatch` (`:304`) and named (`` `clip add` was refused without a reason``),
never silent (R-SVC-3) — B1's own wiring mistake showed the hole. The core
holds no codec and no device API: decoding and WAV writing are `Host` functions (R-SVC-4).

### DR-SVC-2 Unknown input is refused, naming it (R-SVC-3)
An unknown verb or flag fails in `parseCommand` with the nearest candidates (`strp add` →
`did you mean: strip add?`). An address goes through `setAddress` (`core/service/SolarisService.cpp:712`):
an unknown node, field or DEVICE PARAMETER is refused with the nearest name (`set
dv_1.filter.cutof=1` → `did you mean: filter.cutoff?`); a value out of range is refused with the
range; a device parameter's value is read against the DSP registry — a choice by name or index (listing
the choices on a miss), a number inside the spec's range (`:921`; **amended, R-SVC-8, 2026-10-09**: it was
clamped in silence, now a value outside is REFUSED naming the range with its unit, and a non-whole value
of an integer parameter is refused) — and stored as its canonical text. `get` (`getAddress`, `:1028`) reads every field `set` writes plus derived ones (`<clip>.length` resolved, `<strip>.out`,
a device parameter not stored = the registry default). A refusal is a `command.rejected` event and
`lastError`.

### DR-SVC-3 solaris-cc is the whole app without a window (R-SVC-1)
`cli/main.cpp` holds no behaviour: argv, stdin, stdout, the host's file functions; ` : ` chains lines,
`--script` reads them, `shell` reads stdin into one service, `--watch` streams events, a refusal is
`refused: line N: …` and exits 3, unsaved edits at the end exit 4 (**amended, R-SVC-8, 2026-10-09** —
DR-SVC-8 has the session, `--keep-going` and `--discard`). End to end (2026-10-08): a
drum pattern + a synth bass + a 44.1 kHz sample + a reverb bus built entirely by command, saved,
audited, rendered with stems; measured — a drum hit on every beat with silence before it, the 4-beat
pattern looping across an 8-beat clip, the bass C2 at 65.4 Hz, the sample resampled to 48 kHz and
stored relative to the song's folder.

### DR-MIX-2 Every sample file gets its own strip (R-MIX-2, R-MIX-3)
`clip add --src` (`core/service/ServiceEdit.cpp:674`): a file no clip uses yet gets a new audio strip
named after it on the first mixer, routed by `defaultOutFor` (`:101`) to "Main" — the bus that leaves the
nearest later mixer with buses — and, having no clips, a new lane; a file already used reuses its strip and, with no
`--lane`, its lane (`laneFor`, `:629`; **amended, R-SVC-8, 2026-10-09**: every clip got a new lane);
`--strip` overrides. The file
is decoded through the host to learn its length (refused if it cannot be read), and stored relative
to the song's folder when inside it (`relativePath`). A relative `--src` names a file in the SONG's
folder first when one is there (`:618`) — what the model stores and the browser's Song tab hands
back — else the caller's working directory.

### DR-MIX-7 Solo keeps the soloed path alive (R-MIX-7)
`silentStrips` (`core/Compile.cpp:128`): with any strip soloed, a strip is audible only if it is
soloed, reachable downstream of one (its buses, its sends' returns) or upstream of one (what feeds
it); muted strips are silent regardless. The model shows it as `strips[].audible`; the engine gets
it as `silent`.

### DR-MIX-8/9/10 Fed by, the matrix, the audit (R-MIX-8, R-MIX-9, R-MIX-10)
`refreshModel` (`core/service/ServiceModel.cpp:78`) computes each strip's `clipCount`, `fromLanes`
and `fromStrips`. `matrix print [--json]` (`matrixText`, `:641`): rows = strips in processing order,
columns = `matrixColumnsOf` (`model/Project.cpp:1202`) — every strip some strip may reach or does reach
(processing order), master, the output ports, so a route, send or key to an instrument or a same-mixer
strip has its cell — computed once and published as `matrix.columns` (`ServiceModel.cpp:282`), which the
GUI's matrix draws (DR-UI-8); `●` = the main output, `-6.0pre` = a send's dB and tap, `key` = a sidechain
key. (**AMENDED (audit, 2026-10-09):** the columns were the buses only, and the GUI's "strips off the
first mixer" — a key to an instrument strip was in neither.) Guarded by `test_solo_mute_matrix_and_audit`
(a key to a strip on its own mixer prints `-3.0key` in its column; mutant: buses-only columns → red).
`audit` (`:390`): unused strips, strips that reach no output port, single-input and empty buses,
clips on silent strips, offline files, unknown device types and parameters, any strip or the
master that went over 0 dBFS in the last render, and (R-SVC-8, `:501`) notes past their pattern's end,
empty patterns and patterns no clip plays.

### DR-MIX-13 A line added from the mixer; sources relinked (R-MIX-13, R-MIX-14)
`strip relink <ch> --to <ch2>` (`core/service/ServiceEdit.cpp:331`) moves EVERY clip playing through
`<ch>` to `<ch2>` in one edit (one undo step, "strip relink <ch>"), printing how many moved; refused
onto a bus ("a bus plays no clips"), across kinds (audio clips need an audio strip, note clips an
instrument strip), onto itself, and when `<ch>` has no clips. A pattern's `strip` follows its first
clip. In the dock (`app/widgets/MixerDock.cpp`) every mixer page ends with "+ Line" after its last
card — placed from the LIVE card widths, so it slides as cards grow in and shrink out
(`MixerDock::addLineRect`, `MixerDock.cpp:423`; drawn at `:2066`) — whose menu is an audio line, a bus
and every instrument of the registry, each ONE `strip add --kind … [--instrument <type>] --mixer
<mx>` (`MixerDock::openAddLine`, `MixerDock.cpp:1259`); the new card grows in. A strip's menu offers
"Move its clips to ▸" when it has clips and another strip of its kind exists — a second menu of those
strips → `strip relink` (`MixerDock.cpp:1626`). On the lanes a clip's menu (`Timeline.cpp:891`) offers
"Play through ▸" — the strips of its kind, the current one marked "now" → `clip move <ac> --strip
<ch>` — then Piano Roll (a note clip), Duplicate, Delete.

### DR-CLIP-2/3 Patterns and linked clips (R-CLIP-2, R-CLIP-3)
`compile` (`core/Compile.cpp:279`) expands a note clip: a clip longer than its pattern loops it, a
note is cut at the clip's end (`(C1)`, `:444`); events sort by time with note-offs before note-ons at
one sample (`(C2)`, `:495`). `clip duplicate` makes a second clip of the SAME pattern (`linked` = 2 in
the model) — `--count N` makes N, end to end, one edit (DR-SVC-8); `clip unique` copies the pattern.
`note add` replaces a note at the same pitch and tick; so does each note of `notes add`.

### DR-RENDER-1 Offline render (R-RENDER-1, R-RENDER-2, R-RENDER-3)
`render --out <file>` (`core/service/ServiceRender.cpp:74`; `--out` is required — the grammar row says so,
`CommandSpec::required`, `core/service/Command.cpp:239`, and a line without it is refused at parse):
compile → build the engine → one pass capturing the master bus (the mixdown, `--out`), any `--stems` (strips'
post-fader outputs, `<out>.<ch>.<ext>`; `all` = every strip) and with `--ports` every output port
(`<out>.<port>.<ext>`); `--from`/`--to` in beats (an explicit `--to` is exact). With no `--to` the tail runs past the
song's end until the first block the master spends below −90 dBFS, capped at 10 s. Every file starts at `--from`
EXACTLY (R-MIX-17): the engine's `outputLatency()` more is rendered at the end and dropped at the start — the
mixdown and the ports by it, a stem by its own strip's latency (`trim`). The format, rate, depth, dither and
loudness are DR-EXPORT-1's. The host writes the files (`writeWav`, `host/AudioFiles.cpp:117`; `writeAudio`, `:160`)
and decodes sources through FFmpeg, resampled to the render's rate, mono at unity in both channels (`decodeAudio`,
`host/AudioFiles.cpp:25`). Two renders of one song are byte-identical, dither and all
(`test_render_writes_the_mix_deterministically`).

### DR-API-1 The API document is generated, committed and drift-tested (R-API-1)
`apiJson` / `apiMarkdown` (`core/service/ApiDoc.cpp:45`, `:124`) print, from the tables the code
runs on, every command (usage, summary, the R- tag that asked for it), every event with its fields,
every `AppModel` field (with `stable`), **every DSP registry device with every parameter's unit,
range, default and choices**, a kit's pads by name (`:87`), and the Notation — pitches, the note token,
step rows, every chord quality (`:151`, from `chordQualities()`, R-SVC-8). `docs/api.json` and `docs/API.md` are that output, committed;
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
(`touchRecent`, `core/service/ServiceMachine.cpp:68`), at most 20, persisted in the host's recents file —
by its ABSOLUTE path (`absolutePath`, `:59`), so `project open song.slp` from one folder is still found
by a window started in another (audit, 2026-10-09: it stored `song.slp`); `recents remove` takes either
form.
The model's `recents[]` cards carry each song's name, bpm, length and strip count, read from the
files when the list or a song changes (not on every command); a file that is gone or no longer a
song is a card marked `missing`. `recents remove` takes one off the list and leaves the file. Guarded by
`test_the_machine_settings_folders_devices_and_recents` (a relative `project new` lands as its absolute path).

### DR-BROWSE-1 The browser lists a folder (R-BROWSE-1, headless half)
`browse <folder>` asks the host (`listDir`, `host/Machine.cpp`): sub-folders, audio files (by
extension), songs (`.slp`), hidden entries left out; folders first, then by name; the model's
`browser` holds the listing. The panel and its drag and drop are DR-BROWSE-2.

### DR-PLAY-1 Live playback is the offline render, and edits are heard (R-PLAY-1, R-TIME-4)
`transport play [--from]` (`transportCommand`, `core/service/ServiceTransport.cpp`) compiles the song,
builds an engine exactly as `render` does, opens the host's stream to the clock device (`settings
output`, the song's rate, the machine's buffer) and starts a `Player` (`core/Player.h`) — a thread
that renders blocks and WRITES them to the stream, whose blocking back-pressure is the clock.
Ports reach the clock device by the machine's port map (R-DEV-3): an unmapped port, or one mapped
to the clock device, lands at its channel; a port on another device waits for P2. Edits while
playing (`liveUpdate`): a strip's gain or pan, a mute or solo (every strip's silence recomputed),
a device parameter or bypass, the master gain go as lock-free `Live` messages applied at the next
block; anything else (a clip, a route, a device added) compiles a NEW engine on the service's thread
and sends it as a `Swap` — the player continues it from the same position and hands the old one
back to be freed on the service's thread (`collect`). `transport stop` leaves the transport where
it was heard; `transport seek` and `transport loop <from> <to>|off` work playing or stopped — the
player stops exactly at the loop's end and seeks back. `wait <seconds>` lets time pass for scripts.
`pump()` brings the heard position (rendered minus the device's latency AND the engine's
`outputLatency`, `Player::heard`, `core/Player.cpp:235`, R-MIX-17), the latency and the peaks into the
model; the metronome clicks on the beat the music is on (`:612`), the bound values read are those of
the heard block (`:324`). A live `set` that moves a device's latency (a limiter's lookahead) builds a
new engine instead of a message (`latencyOf`, `core/service/ServiceTransport.cpp:85`, checked at `:547`),
so the compensation is never stale. Guarded by `test_live_playback_is_the_offline_render` (a fake clock device: 4 beats
of drums + synth + a reverb send, **sample for sample equal to `render --ports`**),
`test_edits_while_playing_are_heard` (a clip added mid-play is heard after the swap; `gain=-120`
silences at the next block — mutant: dropping `liveUpdate` fails it), `test_loop_and_seek`. On this
machine: `transport play : wait 1.5` reads 3.0 beats at 120 bpm through PulseAudio, latency 39 ms.

### DR-PLAY-2 The audio thread never allocates, locks or touches a file (R-PLAY-2)
Everything the player touches is sized in `Player::start`: `Engine::prepare` reserves every port
buffer for the block, the interleaved buffer is allocated, the meters are a fixed array of atomics.
Messages and engine hand-backs go through the DSP library's SPSC `LockFreeQueue`. `Engine::render`
allocates nothing within prepared capacity (a capture buffer only when stems are asked for —
`test_live_render_allocates_nothing_and_takes_live_edits` counts allocations with a replaced
`operator new`: 0 over 220 blocks and every live setter; mutant: an unconditional capture buffer
→ 880). Every DSP registry device allocates nothing once warm (DSP REQ-device-5, `317bed2`). The bindings'
live values (R-MIX-16) leave the thread through a ring of slots of atomics sized when the `Player` is built
(`keepLive`, `core/Player.cpp:297`), and the service suite counts allocations on every thread but its own
while a song with bound gains, a link and a device formula plays (`test_a_bound_value_is_published_live`: 0;
mutant: a copy of `bindValues()` per block → 1019).

### DR-PLAY-3 Meters in the model (R-PLAY-3)
While playing, `pump()` copies the last block's peaks into `strips[].peak` and
`transport.masterPeak` (L/R, linear) — excluded from the stable dump. RMS, the held peak and the clip latch are
measured by the player too and published beside them (DR-METER-2): a window drawing at 60 Hz sees one block in
twenty, so it could not compute them — and a second front end must read the same numbers.

### DR-UI-1 The window, Home and the song bar (R-UI-1, R-UI-2, R-UI-3, R-HOME-1, R-G-4)
`solaris_app` (`app/`) is a platform-free Artboard tree that links no service: `App` (`app/App.h`)
draws from `AppHooks::model()` and sends text lines through `AppHooks::dispatch` (`App::dispatch`,
`app/App.cpp:434` — a refusal becomes a toast with the service's sentence). `linux_main.cpp` is the
only OS code: GTK3, the frame clock, the pickers, the host functions, the service pumped every tick.
The screen follows `AppModel::screen` and CROSS-FADES (260 ms, `App::render`, `app/App.cpp:951`).
`installSolarisAccent()` moves cosmo's accent slot to teal `#159387` before any widget exists
(`app/Theme.h`); every neutral, radius, font and spacing token is cosmo's, aliased. `HomeScreen`
(`app/widgets/HomeScreen.cpp`): the `solaris.` wordmark sized to fit, New/Open, Settings, recent-song
cards (name, `bpm · length · strips` over a plate of bars from the song's name; a moved song says
"missing" in red and is named by its file) whose grid geometry eases on reflow (`layout`, `:86`);
right-click forgets a card. `SongBar` (`app/widgets/SongBar.cpp`): wordmark, Home (asking first with
cosmo's `ConfirmDialog` when unsaved, `App::requestHome`, `app/App.cpp:545`), the song's name with an eased
unsaved dot, Play/Stop whose glyphs cross-fade, the position as bar.beat.tick in mono, the tempo,
the master meter (40 ms rise, 300 ms fall, `advance`, `app/widgets/SongBar.cpp:150`), Save, Settings. Keys: Space play/stop,
Ctrl+S save, Enter to the start, Ctrl+, settings. Every control's command line is in `app/NOTES.md`.

### DR-SET-2 The settings sheet (R-SET-1, amended)
`SettingsSheet` (`app/widgets/SettingsSheet.cpp`) in cosmo's modal style: the scrim, the popover
card, Done; 150 ms open / 120 ms close (the whole sheet one opacity group, FR-32 — `advance`, `:502`);
Escape, Enter, Done and the scrim close it. Opening sends `devices list` first (`App::openSettings`).
Every control is `settings set …`, every folder control `folder add|remove` (`handleGesture`, `:619`);
the sheet draws the model, so a setting changed from a shell shows on its page, eased. Its rows were
chips down one long card until K2 — now pages and combo boxes (DR-SET-5).

### DR-UI-6 Shots and UI tests over the real service (R-UI-6)
`solaris_app_shots` renders twenty-one named states — Home empty and with cards, Settings at rest,
mid-fade and mid-chip-ease, the song, Home→song mid-cross-fade, a refusal toast, the browser's
instruments and a browsed folder, a sample dragged mid-way, a clip mid-drag, a clip selected while
zoomed, the mixer's Sources and Buses pages, mid page-switch, the matrix, a fold, a device panel,
the dock folded down, the unsaved confirm — at 1440×900 and 1024×640, over the REAL `SolarisService` (`app/tests/Rig.h`: fake devices,
folders and decoder; a fixed 16 ms clock); `--check` fails a blank frame. `solaris_app_ui` (11
tests) clicks and drags the geometry the widgets publish and asserts the lines sent, the model that came back,
and a LIVE value caught mid-tween for every transition (the cross-fade, the sheet's fade, a chip's
fill, the close). The window ran on this machine's display (`solaris song.slp`, 4 s, no crash).

### DR-UI-3 The lanes (R-UI-3, R-LANE-1, R-CLIP-1…4, R-TIME-4, R-UI-7)
`ProjectScreen` (`app/widgets/ProjectScreen.cpp`): the song bar on top, the browser on the left
(234 px), `Timeline` (`app/widgets/Timeline.cpp`) filling the rest. A row per lane in lane order,
then a row per strip whose clips have no lane ("its strip's row"); a lane with no colour wears its
first clip's. A clip is drawn on its row, coloured by its STRIP (`surface::track`), its name pinned
to the visible edge when it begins off-screen (`paintClip`, `:979`); a note clip draws its
pattern's notes repeated where it loops, the seams marked, and "linked ×N" when it shares its
pattern. Ruler: bars from 1, the grid's ticks, every label faded in by its room (DR-UI-10). The
playhead is `destructive`; it follows the transport while playing and eases 140 ms on a seek.
Ctrl+wheel zooms ×1.25 a notch on a lattice about 28 px/beat — 4.7 to 637 px/beat — eased 220 ms
and anchored at the pointer (`:713`); the wheel scrolls, Shift+wheel sideways. The grid follows the
zoom and its finest level with room is THE snap step on the lanes (DR-UI-10). A clip dragged follows
the pointer exactly, on that step, between LANES only, and lands as `clip move <ac> --at <b> [--lane
<ln>]` (`:651`) where it was let go. A click selects (the teal ring cross-fades 200 ms between
clips); Delete/Backspace → `clip delete`, Ctrl+D → `clip duplicate` (`app/App.cpp:542`); a ruler
click → `transport seek <b>` on the grid you see (`Timeline.cpp:883`). A lane moved by `lane move` (DR-LANE-1)
slides to its new row through the same keyed rows. **The picture travels
(§1):** the rows are Interstellar's `AnimatedRows` keyed by lane; each clip keeps an eased beat, row
and opacity keyed by its id (`Timeline::advance`, `Timeline.cpp:348`) — it fades in when it arrives, fades out where it
was when it goes (taking no input), eases 200 ms with its row when moved from a shell; the zebra
follows the LIVE slot, a stripe's colour cross-fades, the empty-state words fade. Another song
places everything where it is. Empty, it says what to do in words.

### DR-BROWSE-2 The browser and drag and drop (R-BROWSE-1 amended, R-BROWSE-3)
`Browser` (`app/widgets/Browser.cpp`): tabs **Samples · Instruments · Song** under a highlight that
slides 220 ms. Samples lists the folders from Settings; a folder clicked is `browse "<path>"`
(`:451`) and shows its sub-folders and audio files (mono, the filename rule) under a row back up;
with no folders it says so and a click opens Settings. Instruments lists `AppModel::deviceTypes`
(the DSP registry, `core/service/ServiceModel.cpp:142`) — instruments, then effects. Song lists the
files the song plays. The list is `AnimatedRows` keyed by generation and content (`rebuild`, `:59`):
a tab or folder changed starts a new generation, so the old list fades where it was scrolled while
the new one fades in (`navigate`, `:118`); an inserted row fades in, a removed one out. A row is
dragged out: the browser reports the pointer and the drop; `ProjectScreen` draws the ghost (the
overlay pass) and the timeline's teal drop hint at the beat on the lanes' step (DR-UI-10; `overLanes`,
`app/widgets/ProjectScreen.cpp:115`), and on release sends ONE line (`place`, `:124`): a sample → `clip add --src "<file>" --at <b> [--lane
<ln>]`; an instrument → `clip add --instrument <type> --at <b> --length 4 [--lane <ln>]` (the new
strip and its empty note clip in one command, `core/service/ServiceEdit.cpp:736`); below the last
lane, `--lane new` — a new lane (`app/widgets/ProjectScreen.cpp:129`; **amended, R-SVC-8, 2026-10-09**: it sent no `--lane`, which
now means "its strip's lane", so the drop says what it wants); an effect → a notice that it goes on a strip (U3). A double-click
places at the playhead.

### DR-UI-7 A strip's colour (R-UI-7)
The model's `strips[].colour` is resolved by the service (`core/service/ServiceModel.cpp:243`): the
strip's own, else its id's number − 1 — never −1, and unchanged when other strips are added or
deleted. Every front end draws it as is.

### DR-UI-8 The mixer dock (R-UI-3, R-MIX-1/5/6/7/9/12, R-MIX-12 amended)
`MixerDock` (`app/widgets/MixerDock.cpp`) sits under the lanes (`ProjectScreen::dockTarget`,
`app/widgets/ProjectScreen.cpp:178`: 429 px wanted; its top edge dragged follows the pointer; the
chevron folds it to its tab bar, eased 220 ms; it gives way before the lanes, which keep 130 px).
Tabs: a mixer page each, "+" (`mixer add`), Matrix; keyed (`syncTabs`) so a tab added slides the
others along, measured in one weight so choosing a tab moves nothing; the highlight slides, the
pages cross-fade as layers. A page (`syncCards`, `app/widgets/MixerDock.cpp:324`) is a card per strip in processing order,
the strips feeding one bus (two or more) gathered under its header; the master pinned right. A card:
colour, name, "audio · N clips" / "bus · fed by N"; the rack in two labelled sections (DR-FX-5b); two sends (→ name, dB, P = pre; a third says "in the
matrix"); pan; the fader (`faderPos`, `:167`: gain ∝ position², 0 dB at 0.708, +6 at the top) beside
L/R meters (−60…+6 dBFS, green → amber at −12 → red at −3); M and S (solo amber); "→ <out>".
Commands (`handleGesture`, `:1405`): a fader or pan dragged — `set <ch>.gain|pan=…` (master:
`set project.masterGain=…`) at each step, double-click → 0; M/S → `set <ch>.mute|solo=…`; "→ out"
→ cosmo's `ContextMenu` of `strips[].targets` → `route`; "+ Effect" → the registry's effects →
`device add` (`openAddEffect`, `:1348`); a send → pre/post, make it the main output, remove; a send
dragged sideways → `set <sd>.gain=…`; right-click → rename (`set <ch|mx>.name=…`) or delete. A
fold header click folds its group (`:1591`, the view's, eased 260 ms: members narrow to nothing, the
header widens to "→ Main · N strips"). The Matrix (`paintMatrix`, `:2149`): rows by mixer, columns
= the model's `matrix.columns` (`:2153`; the same `matrix print` prints, DR-MIX-8/9/10 — amended, audit
2026-10-09: it was "strips off the first mixer", so a key to an instrument strip had no cell); ● the main
output, a send's dB, P pre-fader, K a sidechain key (`:2197`); a cell routing
refuses is hatched; a click on an open cell → `send add`, on a send → its menu, a double-click →
`route`, a vertical drag → its level. **Nothing snaps** (`advance`, `:954`): every model value is
drawn through an eased copy keyed by strip (fader and pan 220 ms, M/S/dim 200, colour cross-fade
200 from what is shown, meters 40 up / 300 down); a dragged control follows the pointer exactly;
cards and tabs are keyed — arriving grows, leaving shrinks, a moved card shrinks where it was and
grows where it is. A strip a solo silences dims. **AMENDED (R-MIX-16, R-UI-11, 2026-10-09):** a
right-click on a fader, a pan, a send's level (on a card or its matrix cell) or the master fader opens the
device parameter's menu instead of the strip's (`openParam`, `:1372`; DR-MIX-16); the strip's and a mixer
tab's menus gain Copy ID. A bound fader or pan is drawn at the model's `bindings[].live` — set each frame
while playing, eased when stopped or onto a new driver (`advance`, `:969`) — under a tag saying what drives
it (`paintTag`, `:1778`); with View › Show IDs each card, chip and send shows its id (DR-UI-11).

### DR-FX-5a An effect dragged to its place in the rack (R-FX-5a)
A press on an effect's chip in a card — never the generator, "+N more" or "+ Effect" — takes it
(`MixerDock::handleGesture` Down, `app/widgets/MixerDock.cpp:1423`). Dragged, it is the pointer's, up and down the card's
effects (`:1469`, drawn on top and lifted, `paintRack`, `:944`); where it would land is an effects slot, or past the
effects the card shows (the "+N more" and "+ Effect" slots): the end of the rack. Meanwhile `syncFx` shows the other
chips as if it were already there (`:837`), so they slide, eased, to make room. Let go (`:1532`), the chip stays where it
was let go and eases to its slot, and ONE `device move <dv> --to <index>` is sent — nothing when the place is the
same; the service refuses the generator's index as before (R-FX-5). A click (no drag) still opens the effect's window.
`fxDragging()` and `fxDragRect()` (`app/widgets/MixerDock.h:133`; `app/widgets/MixerDock.cpp:661`) publish the chip in
hand. UI `test_an_effect_is_dragged_to_its_place` (`app/tests/ui/uiTests.cpp:1066`: the chip at the pointer, the reverb
caught making room, nothing sent while in hand; the one line, the chip easing home from 1.4 slots; a drag back sends
nothing; the generator never taken; dropped past "+2 more" to the end; a click opens it); shot `mixer-effect-dragging`
(`app/tests/shots/renderShots.cpp:395`). Mutants, each seen red: no room made, the chip landing at its slot at once, the
generator taken (a survivor at first — the test now checks it is not in hand mid-drag), past the shown not the end, a
drag back still sent, the chip not following the pointer.

### DR-FX-5b A card's rack in two sections; its effect chips keyed (R-FX-5b; R-FX-5a's slide)
A card in the dock (`MixerDock`, `app/widgets/MixerDock.cpp`) shows its rack as two labelled sections, from the card's
geometry (`geoOf`, `:67`: a title row, the generator, a title row, three effect slots — the sends, pan and fader 22 px
lower than before, the fader's zone giving the room). `rackOf` (`:100`) reads a rack the way a card shows it: an
instrument first is the GENERATOR, apart; then every effect and "+ Effect", or the first effect, "+N more" and
"+ Effect". The titles (`sectionTitle`, `:617`) are GENERATOR — or INPUT on a bus and the master — and EFFECTS, drawn
uppercase and tracked as section headers, receding with a frozen rack (`paintRack`, `:876`). The generator's chip is the
instrument (its strip's colour at its edge; a click opens its window, R-WIN-1) or a line that says where the sound
comes from (`generatorText`, `:638`; a bus's or the master's drawn as the first name and "+N"): "Audio clips", the strips feeding a bus, the strips routed to the master — not
pressed. `chipSlots` / `chipRect` keep counting a rack's slots from its generator, so a click maps to the device
(`:1604`). The effect chips are KEYED by device (`FxChip`, `app/widgets/MixerDock.h:242`; "+N more" and "+ Effect" by
their own keys): `syncFx` (`app/widgets/MixerDock.cpp:809`, every frame from `advance`, `:958`) puts each in its slot,
eased (200 ms), and fades a chip that comes or goes (180 ms) — a rack reordered, grown or shrunk by anyone slides, never
jumps. UI `test_the_rack_shows_its_generator_apart_and_slides` (`app/tests/ui/uiTests.cpp:993`: the titles drawn and
what each section says per kind of strip; stacked in order; the generator opens its instrument; a shell's `device
move` caught mid-slide; one effect too many fading into "+2 more"; its menu); shots `mixer-sources`, `mixer-buses`,
`mixer-frozen`, `mixer-rack-sliding` (`app/tests/shots/renderShots.cpp:384`). Mutants, each seen red: the chips snapping,
popping in, no generator apart, a bus saying Generator, the titles not drawn.

### DR-UI-5 Device panels, generated (R-UI-5, R-WIN-2/3)
`DevicePanel` (`app/widgets/DevicePanel.cpp`) is the content of a device's WINDOW (DR-WIN-1). It knows no
device: per `DeviceModel::params` it builds — once, the first time the model has the device
(`build`, `:192`), kept — a row per parameter grouped by the name's prefix (`osc1.*` → OSC1): a number
is cosmo's `SliderRow` with the opt-in `formatValue` (Hz/kHz, ms, dB, st/ct/oct signed, a 0…1 amount in
%), a `logScale` parameter through a log taper, an `integer` one rounded; a choice is a row stepping
its names. It is the parameter LIST: the value column says what decides each row (`bindingText`,
`app/widgets/ParamMenu.cpp:33`) — its number, `auto au_1`, `= ch_2.gain`, `= <formula>` cut to the column — and the row
`DeviceModel::lastChanged` names is lit, the light easing from row to row (`ParamBody`, `:87`). A
right-click offers Create Automation, Formula… (cosmo's rename field), Clear Binding, Reset to Default
(`openParamMenu`, `:443`) — **AMENDED (R-MIX-16, R-UI-11, 2026-10-09):** built by the shared `ParamMenu`
(`paramMenuItems`, `:470`), the same menu the dock's numbers offer (DR-MIX-16), with Copy Address / Copy
Value (a bound row: its live value) / Copy as Formula (a choice row: no "as Formula", no binding items);
with View › Show IDs each row's full address fades in over its label (`ParamIds`, `:179`, DR-UI-11). The
App offers right-clicks to the windows first because a row's slider would
swallow them (`app/App.cpp:86`). Every change is one line: `set <dv>.<param>=…`, `auto create`,
`bind clear`, `set <dv>.bypass=…`, `device remove <dv>` (none for an instrument — it has "Piano Roll"
instead, DR-ROLL-1). While the pointer is
down `bind` re-seeds nothing (`:469`); the body scrolls with its own bar (`reveal` eases to a row).

### DR-AUTO-1 Automations and bindings in the `.slp` (R-AUTO-1, R-AUTO-4)
`Automation` (`model/Project.h`): id `au_n`, name, unit, min/max, `from`, points (beat, value, shape
`linear|hold|smooth|bezier`, sorted; a bezier point's handles — DR-AUTO-6). `Binding`: address +
formula (with its `=`), one per address. Written after the clips as `#aauto` with `#point` children
and `#abind` nodes (`serializeProject`); read by `parseProject` (`model/Project.cpp:731`), which also
reads the suite schema's earlier sketch — indented `<beats> = <value>` lines and
`node=/param=/interp=` — and normalises it once (`:801`). `validateProject` (`:1316`) refuses two
bindings on one address, a formula without `=`, a binding on something that does not exist, an empty
range, an unknown shape and a bezier influence outside 0 < x ≤ 100 % (`:1434`).

### DR-AUTO-2 Formulas: parsed, resolved, ordered (R-AUTO-2, R-AUTO-3)
`parseFormula` (`core/Formula.cpp:205`): recursive descent straight to the engine's postfix
`Expr`; numbers, `+ − * / ^` (right-assoc.), unary minus, parentheses, 17 functions with their
arities checked, `pi`, the clock words `beat bar bpm t`; every other name is a symbol, listed in first
use; errors name the text and its column; deeper than the evaluator's stack is refused.
`describeAddress` (`core/Bindings.cpp:13`) is the one answer to "is this a number a formula can
drive, and what are its range, unit, label and owner": a strip's gain (dB, −120…12) and pan, a send's
gain, `project.masterGain`, every numeric registry parameter (a choice or a switch is refused, saying
so). `orderBindings` (`:112`) resolves each symbol to an automation or a numeric address and orders the
bindings (Kahn, file order on ties) so a link reads one evaluated before it; a loop is reported naming
its members; an unreadable binding is left out — INERT, its own value plays. `checkBinding` (`:181`)
answers for one candidate beside the rest; `set` refuses on its answer.

### DR-AUTO-3 The engine evaluates them every 64 samples (R-AUTO-7)
`compile` (`core/Compile.cpp:520`) turns automations into `engine::Curve`s (`compileCurve`, `:654`:
Interstellar's keyframes in samples, speeds per sample, ranged as the automation — DR-AUTO-6) and
bindings into `engine::Bind`s — target (strip gain/pan, send gain, master gain, a device parameter by
registry index), clamp, integer, own value — with each symbol remapped: an automation → its curve's
slot, a bound address → its binding's slot, an unbound address → its own value as a constant.
`evaluateBinds` (`engine/Expr.h:253`) fills the clock, the curves (`Curve::valueAt`, `:182`, Interstellar's
`anim::eval`, clamped to the automation's range) and each
binding in order (`Expr::eval`, `:60`, a fixed stack, no allocation; a non-finite result keeps the last
good value). `Engine::render` (`engine/Engine.cpp:1096`) cuts pieces at every multiple of `kControl` = 64
samples and evaluates there (`evalBinds`, `:376`), so the result is the same however time is chopped;
bound gains, pans and sends ramp linearly across the period, unbound strips render exactly as before.
Values at time zero are applied before the devices' warm-up (`:275`); a seek evaluates at once
(`:535`). Measured: a 0 → −20 dB curve renders −5.00 dB at beat 1 and a formula-stepped compressor makeup
+12 dB from beat 2, byte-identical at chunks of 77, 128 and 1000, no zipper, no allocation.

### DR-AUTO-4 The service: commands, `set`, `eval`, the model (R-AUTO-1…5, 8, 9)
`set <address>="=<formula>"` binds (`core/service/SolarisService.cpp:736`); a plain number clears the
binding and sets the value; `get` prints the formula; an unquoted formula with spaces is refused with
the quoting shown. `auto create <address>` (`core/service/ServiceAuto.cpp:132`) makes `au_n` named
"<owner> · <label>", ranged as the address, holding its value from beat 0 to the song's end (at least
four bars), and binds the address to `=au_n`; `auto add|delete` (refused while read, `--unbind`),
`auto point add|move|delete|shape` (a value outside the range is refused naming it, `inAutoRange`, `core/service/ServiceAuto.cpp:69` —
**amended, R-SVC-8, 2026-10-09**: it was clamped; `shape` takes a bezier point's handles, DR-AUTO-6), `bind clear`; deleting a strip, a
send or a device drops the bindings that drive them. `eval <address> [--at] [--explain]`
(`:315`) compiles and evaluates with the engine's own function and prints each name it reads.
`refreshModel` (`core/service/ServiceModel.cpp:386`) publishes `bindings[]` (reads, ok, problem),
`automations[]` (points with their handles, usedBy, `now` — DR-AUTO-7), `params[].formula`,
`strips[].gainFormula/panFormula`, `sends[].gainFormula`, `masterGainFormula`, `devices[].lastChanged` and — **AMENDED (R-MIX-16, 2026-10-09)** —
`bindings[].live`, each binding's value at the heard position (`evaluateLive`, `:491`; DR-MIX-16); `audit` (`:483`) names inert
bindings and automations no formula reads. While playing, a binding or curve edit — or a `set` on an
address a formula reads — swaps in a new engine (`core/service/ServiceTransport.cpp:499`).

### DR-EDM-1 Undo and redo (R-EDM-1)
The history is a TREE now (DR-HIST-1): after every edit that LANDED, `dispatch` keeps the song as the edit left it
as a node (`core/service/SolarisService.cpp:389`); consecutive `set`s of exactly the same addresses overwrite the
newest node instead of adding one, so a dragged fader is one step (a rule a script sees the same way). `undo` /
`redo` (`historyCommand`, `:514`) move to the parent / the newest child, print the label of what they take back or
put back (`set ch_2.gain`, `clip add`, `strip delete ch_3`), emit `project.changed what=undo|redo node=<label>`, mark
the song unsaved and, while playing, swap in a new engine. A new edit no longer clears redo's steps: they stay a
branch. Not edits: machine settings, the transport, a save, `get`/`eval`/`audit`, `history`. `project
new|open|close` start a new tree. The model names both (`undoLabel`, `redoLabel`, `undoDepth`, `redoDepth`); the
window's undo and redo keys (the keymap's, DR-SET-4) send `undo` / `redo` (`app/App.cpp`).

### DR-HIST-1 The history is a tree (R-HIST-1, R-HIST-2)
`SolarisService` keeps `mHist`: nodes keyed by an id never reused while the song is open (ascending = the order
they were made), each the song as an edit left it, its label, its parent and its children
(`core/service/SolarisService.h`), in memory only — never saved, with or beside the song (R-HIST-4, the user's
choice). A new, opened or closed song resets it (`historyReset`, `core/service/SolarisService.cpp:484`; `dispatch`, `:407`):
one root, "New song" or "Opened", the song as it came. An edit that landed is `historyAdd` (`:480`): a child of the
current node — a BRANCH when that node already has children, and the old ones stay. `undo` goes to the parent,
`redo` to the child with the highest id (the newest branch), `history jump <id>` to any node (`historyCommand`,
`:518`): the song becomes that node's, the move is announced (`what=history.jump`), the song is unsaved, the player
hears it — and a move is no node itself; the next edit after one starts a node of its own. `history` prints the tree
— `*` at the current node, its id, its parent's (`-` at the root), its label — and `history --json` the same as the
model's `history[]` (`id`, `parent`, `label`) and `historyCurrent` (`core/service/ServiceModel.cpp:461`, which also
derives the undo / redo labels and depths from where the song is). At most 200 nodes (`historyPrune`, `SolarisService.cpp:476`): past
that the oldest leaf OFF the path from the root to the current node goes first; when the tree is one path, its root
does and the next node becomes the root. L2 `test_history_is_a_tree` (a branch kept, a jump, redo's newest branch,
the root, a drag one node, the cap, a song opened its own root). Mutants: a new edit dropping the undone branch (the
old stack), redo taking the oldest branch, pruning the root before an old branch — all seen red.

### DR-HIST-2 The tree, seen (R-HIST-3)
Edit ▸ Show History Tree… (`app/App.cpp:235`, as cosmo's History menu has it) is `App::openHistory` (`:450`): cosmo's
own `HistoryView` — compiled from `apps/cosmo/widgets`, never copied — shown over a snapshot of the model's
`history[]` (a node's parent mapped to its index; the hint under the title names this machine's undo and redo keys,
R-SET-4). It is modal: the gesture sink gives it every gesture while it is open (`:124`; a wheel notch pans a row),
Escape closes it, and only the undo and redo keys pass through, walking the tree it shows (`App::key`, `:627`). A
node clicked is ONE `history jump <id>`; the view stays open, and when the model comes back `syncHistory` (`:502`)
moves its current node there — or, if the tree itself changed (an edit from a script while it is open), shows the
new snapshot. Solaris opts in to `easeCurrent` (cosmo `widgets/HistoryView.cpp:187`): the highlight EASES from the
node it leaves to the node it reaches (200 ms) instead of jumping; cosmo leaves it off. Its open and close fade and
its pan glides as cosmo's do. The Edit menu's other items name the keymap's keys now (`keyHint`, `:408`), so a
re-keyed undo shows its new key. UI `test_the_history_tree` (opened from the menu, caught fading in; a click is one
line and the highlight caught mid-way between the two nodes; the undo key walks it; a script's edit shows; Escape
closes it, eased); shots `history-tree`, `history-tree-jumping`. Mutant: a highlight that snaps — seen red.


### DR-UI-9 The song bar's menus and Settings beside Home (R-UI-3 amended)
(**2026-10-16, R-UI-3 amended again:** the Home button is gone — the `solaris.` wordmark is Home, its hit rect the
measured word (`SongBar::hitRect(kHome)`), washed on hover; a click is `requestHome`, which asks about unsaved changes.
UI `test_song_bar_menus_and_settings_sections`, `test_home_unsaved_confirm_and_recents`; mutant (a wordmark that does nothing) seen red.)
`SongBar` (`app/widgets/SongBar.cpp`): wordmark (Home) · Settings · cosmo's `MenuStrip` (laid out by
`SongBar::layout`, `app/widgets/SongBar.cpp:40`) · the song's name; the transport is centred when there is room and otherwise
starts right of the menus and `kNameMin` (120 px) of name (`transportX`, `app/widgets/SongBar.cpp:51`), so the bar fits at
1024; the master meter and Save on the right. `App::buildMenus` (`app/App.cpp:154`): **File** — New
Song…, Open…, Save, Save As…, Render…, Render Stems… (the host's save picker, `onPickSave`, then
`project save "<path>"`, `render --out "<path>" [--stems <every strip>]`), Home; **Edit** — Undo / Redo
naming what they would take back (from `undoLabel`/`redoLabel`), Duplicate / Delete the selected clips (DR-CLIP-11);
**Song** — Add Mixer, Add Bus, Add Audio Line, Add Lane, Quantize Clip (the selected note clip's pattern at the
lanes' snap step, 1/32 of a beat when nothing snaps: ONE line, `pattern quantize <pt> --grid <step>`,
`app/App.cpp:216`); **View** — Hide/Show Mixer (the dock eases
to its tab bar), Hide/Show Browser (its width eases to 0, everything right of it follows the live
width), Metronome On/Off (`settings set metronome=…`), Show/Hide IDs (`settings set showIds=…`, R-UI-11,
`app/App.cpp:252`), Settings…. Labels follow the state
(`refreshMenus`, `:209`). A press outside an open menu closes it first (`:570`), as cosmo's does.

### DR-SET-3 The settings sheet in sections (R-SET-3)
`SettingsSheet`: **Audio** (Output, Input, Sample rate, Buffer),
**Playback** (Metronome Off/On, Click level −18/−12/−6/0 dB), **New songs** (Tempo 100…174, Meter
4/4 3/4 6/8 7/8), **Sample folders**, **Interface** (Reduced motion) — a 12 px title per section, a
hairline between; every chip `settings set <key>=…`. The service keeps the new keys in the machine's
file (`metronome`, `metronomeLevel`, `newBpm`, `newSig`, `reducedMotion`, and `showIds` — R-UI-11, set from
View, not the sheet — `core/Settings.cpp`),
`project new` starts at `newBpm`/`newSig` when no flag says otherwise, and the App ORs `reducedMotion`
with the OS's own setting into `artboard::setReducedMotion` (design rule §2.6). The sheet scrolls when
it is taller than the window (R6); `revealRect` brings a control in.

### DR-SET-4 Hotkeys are settings: the keymap (R-SET-4)
Every shortcut is an ACTION — `keyActions()` (`core/service/Keymap.cpp:8`): id, area, label, default keys, in the
sheet's order (Transport: play/stop back to the cue `Space`, stop here `Ctrl+Space`, to the start `Enter`; Song: save,
undo, redo `Ctrl+Shift+Z, Ctrl+Y`, settings `Ctrl+Comma`; Lanes: delete / duplicate / copy the selected clips, paste them (DR-CLIP-9, DR-CLIP-11); Piano roll:
select all, deselect, delete, duplicate, copy, paste, up / down a semitone, up / down an octave, earlier / later by
the snap — the keys the app had before they were settings). A combo is spelt ONE way, `Ctrl+Alt+Shift+<key>`
(`core/service/KeyCombo.h`, a pure header shared by the service and the window): A–Z, 0–9, F1–F12 and named keys
(Space, Enter, Escape, Backspace, Delete, Tab, arrows, Home/End/PageUp/PageDown/Insert, punctuation by name — `Comma`);
the codes are the window's, a letter or digit its ASCII, the rest Windows' virtual-key numbers. `settings set
key.<id>=<combos>` (`core/service/ServiceMachine.cpp:200`) takes any case and order and keeps the canonical spelling;
`none` takes every key away, `default` gives the defaults back (and drops the line); an unknown action is refused
with the nearest, a key not in the table naming it, and — once every key of the line is in, so one line can swap two
actions — a combo another action of the same keyboard has (Transport, Song and Lanes are one; the roll's is its own)
naming that action (`:231`). Only re-keyed actions are written to the machine's file (`key.<id> = <combos>`,
`core/Settings.cpp`); a hand-edited line that does not read is the defaults (`keysOf`, `Keymap.cpp:46`). The model
publishes every action with its keys and defaults (`settings.keys`, `core/service/ServiceModel.cpp:143`); the API
document's Keyboard section is generated from the same table. In the window `App::key` (`app/App.cpp:540`) spells the
press (`comboText`) and looks it up there: while a roll has the keys (R-ROLL-8) its keyboard first — `PianoRoll::action`
(`app/widgets/PianoRoll.cpp:502`), which declines a selection's action with no selection — then the song's
(`songAction`, `:633`), so the roll's keys shadow the song's only while it uses them. The host names the KEY, not the
character: GTK's keysym with no modifier (`linux_main.cpp:274`, `keyCodeOfKeysym` in `app/KeyInput.h`), so Shift+1 is
"Shift+1"; what the press types follows as a Text event (D-9). L2 `test_keys_are_settings`; UI
`test_the_keys_are_the_machines` (re-keyed by a script, the window follows; the roll's keys shadow the song's; the
host's mapping). Mutants: no conflict refusal, the window reading the defaults instead of the model — both seen red.

**The sheet's Keyboard page** (`SettingsSheet`, the last section — DR-SET-5; laid out and measured in the paint,
`paintKeyboard`, `app/widgets/SettingsSheet.cpp:1314`): a muted micro-label per area, then an action a line — its label,
its keys as chips right-aligned, a "+" at the right edge, a dot before it while it is not its defaults; "Reset to
defaults" beside the page's title, quiet while nothing is changed. A chip clicked (or "+") LISTENS (`listen`, `:606`;
the gesture at `:938`, a double-click a click there): its border and wash turn to the accent and its label cross-fades
to "Press a key…", its width easing between the two. The next key (`handleKey`, `:986`) is the slot's combo —
`settings set "key.<action>=<the keys>"`, ONE line; Backspace removes the slot's key, Escape cancels, a key no combo
can name keeps it listening, a click elsewhere lets it go. A key another action has is refused by the service and
the App's toast says which action. Reset is ONE line, `key.<id>=default` for every changed action. The chips are
keyed by combo (`bind`, `:411`): a key that arrives grows in from no width, one that leaves shrinks out where it was,
so its neighbours slide (design rule §1). Escape and Enter cannot be listened for with the sheet (they cancel and
— as Backspace — have their own meaning there); `settings set` takes them. UI `test_the_keyboard_section`
(listening caught mid-ease, a chip caught growing while the old one shrinks, a refusal toasted, Reset as one line);
shots `settings-keyboard`, `settings-key-listening`. Mutants: a listening look that snaps, chips that snap — both
seen red.

### DR-SET-5 Settings in pages; a choice is a combo box (R-SET-5, R-SET-6)
The sheet is a fixed card (720 × 520, smaller in a small window): a title band, a RAIL of the six sections — Audio,
Playback, New songs, Sample folders, Interface, Keyboard (`railRect`) — and beside it ONE page, under a footer with
Done (`onPaint`, `app/widgets/SettingsSheet.cpp:1023`). Each section is a `Page` child (`:174`): clipped (its own paint
too — `clipToBounds` clips only children), input-transparent (its controls take clicks; what the sheet draws goes to
the sheet), with its own eased scroll. A section clicked is `showSection` — no line, the view's — and `advance` (`:658`)
cross-fades the chosen page in as the last fades out (200 ms) and slides the rail's highlight to it (220 ms), each
name easing to the foreground as the highlight reaches it; an open list closes and a listening key stops. The sheet
opens again on the page it was left on. A setting that is one value of a list is a `SettingCombo` (`:107`):
Artboard's `ComboBox` in cosmo's tokens (`comboStyle`, `:82`, as Genesis dresses it) — the drop, the list, its scroll
and a pick are the library's; the field is drawn here (FR-41) so its label CROSS-FADES to a new value, whoever set
it. A row picked is ONE `settings set <key>=<value>` line. A value the list lacks — a device not connected now, a
tempo or a level set from a shell — is an entry of its own ("137 bpm (set elsewhere)", `bind`, `:264`), so the setting
is never invisible. An on/off setting is a `SettingToggle` (`:152`): Artboard's `ToggleSwitch`, its knob drawn from an
eased copy of `on()`, so a value set from the model slides as a click does; a click is ONE `settings set …=on|off`.
Controls are laid out each frame (`layoutPages`, `:611`) — name and note left, control right — and a control scrolled
out of its page is hidden so it takes no input. Escape closes an open list first, then the sheet (`:974`); a click
anywhere off a list closes it. The App draws the sheet in both passes now — its card, pages and controls, then an open
list over everything (`app/App.cpp:821`). UI `test_settings_combos_send_lines_and_ease` (a list dropped, a row one
line, the label caught mid cross-fade, a shell's change eased), `test_settings_in_pages` (the cross-fade and the
highlight caught mid-way; a toggle eased from a shell; a value its list lacks shown; Escape closes the list first; the
sheet reopens on its page); shots `settings-open`, `settings-folders`, `settings-combo-open`, `settings-page-changing`.
Mutants: a page cut instead of cross-faded, a label that snaps, a knob that snaps — all seen red.

### DR-WIN-1 Windows inside the song view (R-WIN-1, R-WIN-4)
`FloatWindow` (`app/widgets/FloatWindow.cpp`): a frame — a 26 px title bar (title, ×) over its content.
Dragged by the title it follows the pointer exactly, kept inside the layer; touched anywhere it is
raised; `open`/`close` record intent and `advance` fades it (150 / 120 ms); while closing it takes no
input (`handleGesture`, `:55`). `WindowLayer` covers the song view under the song bar (over the lanes,
dock and browser) and keeps windows keyed — `dev:<dv>` (`openDevice`, `:127`: a new one cascades from
the top right), `roll:<pt>` (`openRoll`, `:155`, DR-ROLL-1) and `auto:<au>` (`openAutomation`, `:172`, DR-AUTO-7) — reopening a closed one where it was; titles follow the model ("Basic Synth — Bass")
and a window whose device is removed by anyone closes (`bind`, `:226`). Rack chips and an instrument
strip's name (double-click) in the dock open them; a chip is outlined while its window is open.
Placement and stacking are the view's. The dock's floating panel is gone (R-UI-5 amended).

### DR-WIN-5 A window is resized by its corner (R-WIN-5)
Every float window carries a `ResizeGrip` (`app/widgets/FloatWindow.cpp:22`) — a child of its own added after the
content, so it is drawn and hit on top of it — at its bottom-right corner (`gripRect`, 13 px; placed by
`FloatWindow::layout`, `:115`). A press raises the window; dragged, the size is where the pointer has the corner —
direct manipulation, no easing — never below `kMinW` × `kMinH` (260 × 156) nor past the layer's edge. The content
is laid out at the new size each frame: a roll shows more rows (its grid grows by exactly the height added), an
instrument's face reflows its modules. Three strokes mark the corner, brighter under the pointer. The view's: not
saved, not a command (R-WIN-4). Closes the UX audit's U-5.

### DR-AUTO-5 Automation on the timeline (R-AUTO-6; drawn in clips since R-AUTO-14)
An automation is drawn in its CLIPS (DR-AUTO-9, part O2): there is no AUTOMATION section — an automation's track is a
lane row (`app/widgets/Timeline.cpp:171`), its header the accent stripe, its name and the address that reads it.
`TimelineAuto.cpp` draws the curve by the ENGINE's keys (`keysOf`, `:47` → `engine::curveKeys`; `paintCurve`, `:320`,
sampling it every 2 px and at every point so a hold steps upright) — holding the first value before the first point and
the last after, linear / hold / smooth / bezier per segment, on a log scale for Hz and ms (`valueToY`, `:105`) — with
the points as handles and a bezier point's two handle stems, in the clip's first pass. `autoGesture` (`:431`): a click
in a clip's curve adds a point at the lanes' snap step under the pointer (DR-UI-10) with the value under it (`auto point
add`) once the double-click has had its chance — a ghost point drawn at once and fading in meanwhile (`:563`, sent by
`advanceAuto`, `:291`; **AMENDED (R-AUTO-11)**: a double-click there opens the window instead); a point dragged is drawn
under the pointer while held (its beat on the same step, `:480`) and sends ONE `auto point move <au> --at <from> --to
<to> --value <v>` on release, staying where it was let go; a double-click ON a point deletes it; a right-click on a
point offers Linear / Hold / Smooth / Bezier and Delete Point (elsewhere in the clip, the clip's menu; on its track's
header, Delete Automation). A curve the model changes eases there point by point over 220 ms — handles too, in every
clip of it at once; a point added or removed, or a shape changed, cross-fades the two curves (`shownPoints`, `:87`).

### DR-ROLL-1 The piano roll (R-ROLL-1…5, R-EDM-6)
**Grammar.** `note move <pt> --pitch <p> --at <b> [--to-pitch <p>] [--to-at <b>] [--length <b>] [--vel
<1…127>]` (`core/service/ServiceEdit.cpp:1026`; a pitch by number, name or pad, DR-SVC-8) edits the one note at that pitch and beat — a moved note
landing on another's place replaces it; `pattern quantize <pt> [--grid <b>] [--swing <0…0.75>]`
(`:875`) moves every start to `k·grid`, odd `k` delayed by `swing·grid`, two notes landing together
merging into the louder. Both are edits (undoable, all-or-nothing). The model gives a pattern the strip
its first clip plays through and that strip's instrument (`patterns[].strip`, `.instrument`), and a
device type its named keys (`deviceTypes[].noteNames`, `core/service/ServiceModel.cpp:149`) — the DSP
registry's (REQ-device-6: the Drum Machine's ten pads, Kick = 36 …).
**The window.** `PianoRoll` (`app/widgets/PianoRoll.cpp`) is the content of a `roll:<pt>` window
(`WindowLayer::openRoll`, `app/widgets/FloatWindow.cpp:157`), opened by double-clicking a note clip
(`Timeline.cpp:908`) or by an instrument window's "Piano Roll" (`DevicePanel.cpp:570` — the strip's
pattern; a menu when it plays several; `clip add --strip` and then its roll when it has none). Titled
"Piano Roll — <pattern> · <strip>"; a pattern gone closes it. A toolbar: snap 1/4 · 1/8 · 1/16
(default) · 1/32 · Off, Notes | Steps; below them (DR-ROLL-11) Quantize… (a menu of three lines: straight, swing
25 %, 50 %, at the snap — and the grooves). **Notes:** keys on the left (C named with its octave, C4 = 60; a kit's keys by its pads,
nothing else named), black-key rows shaded, bars stronger; notes in the strip's colour, brighter the
louder; the velocity lane below; past the pattern's end dimmed, the end a handle (`PianoRoll::onPaint`, `PianoRoll.cpp:704`).
A click on empty grid → `note add <pt> --pitch --at <the snapped cell> --length <the last length>`; a
note dragged is drawn where the POINTER has it (beat snapped) and lands as ONE `note move … --to-pitch
--to-at`; its right 5 px — or its right third when it is narrower than 15 px, so a short note keeps a body to grab (D-7) — resize it (`note move … --length`, which becomes the last length); a
velocity stem dragged → `note move … --vel`; a double-click or right-click → `note delete`; the end
dragged → `set <pt>.length=` — a drag lands where it is let go however short (under the recognizer's 5 px
threshold too, `PianoRoll::track` at the release, the Click after it swallowed; D-6) (`PianoRoll::handleGesture`, `PianoRoll.cpp:267`). **Steps:** a row per pad (a kit) or per pitch in
use and middle C, a cell per sixteenth, a click toggling a note there (`note add … --length 0.25` /
`note delete`) (`PianoRoll::paintSteps`, `PianoRoll.cpp:674`). **Nothing snaps:** notes are keyed by (pitch, beat) — a new one
fades in, a deleted one fades out where it was, a dragged one is the pointer's and lands without
re-fading (`PianoRoll::bind`, `PianoRoll.cpp:75`; `PianoRoll::advance`, `PianoRoll.cpp:182`); the two modes cross-fade; Steps zooms to fit the pattern
(64–160 px a beat) and Notes gets its own zoom back, eased; Ctrl+wheel zooms about the pointer, the
wheel scrolls, eased; a length changed from a shell moves the end, eased.

### DR-ROLL-6 Play from the roll (R-ROLL-6)
**Grammar.** `transport play --clip <ac>` (`core/service/ServiceTransport.cpp:509`) plays the clip's span —
`at` … `at + clipLengthBeats` — LOOPED, the whole song sounding there, from its start (or from `--from` when
that lies inside it); a clip unknown or of no length is refused naming it. The song's loop is kept aside and
given back by `transport stop`, by a plain `transport play` (the song's) and when the clip is deleted
(`endClipLoop`, `:755`); a `transport loop` set meanwhile is the song's and stays. After every edit the
loop follows the clip — moved, resized, a looped clip lengthened (`followClipLoop`, `:768`, called from
`dispatch`, `core/service/SolarisService.cpp:446`) — sent to the Player at once. The model publishes it:
`transport.loopClip` (`core/service/AppModel.h:490`), with `loopFrom`/`loopTo` the clip's span while it
loops. L2: `test_a_clip_plays_looped_and_gives_the_songs_loop_back` hears the clip's kick at the top of
each of three loops and checks every way back to the song's loop.
**The window.** A roll knows its clip: the one it was opened from (a double-click or the clip's "Piano
Roll", `WindowLayer::openRoll(pt, ac)`, `app/widgets/FloatWindow.cpp:237`) while that clip lasts, else the
pattern's first in time; none when the pattern is on no track (`PianoRoll::bind`, `app/widgets/PianoRoll.cpp:88`). Its
toolbar starts with ▶ (`PianoRoll::togglePlay`, `:548`): `transport play --clip <ac>`, or `transport stop` as
■ while its clip loops — lit in the accent then, its glyph dimmed with no clip and the click saying nothing.
Space stops whatever plays; stopped, with a roll the front window (`WindowLayer::frontRoll`,
`app/widgets/FloatWindow.cpp:363` — the last open window in the stacking order) AND the keys its (DR-ROLL-8: the roll
last opened or pressed in), it is that roll's ▶, else the song's play (`App::songAction`, `app/App.cpp:908`) — so a
click on the lanes, on nothing or on a clip, gives Space back to the song while the roll stays open (R-ROLL-6 amended,
2026-10-21; `test_the_roll_plays_its_clip` clicks below the last lane, then on the roll's title bar). The playhead is drawn where the clip is heard — the transport's beat
less the clip's start, modulo the pattern's length for a looped clip — fading in and out with the play
(`app/widgets/PianoRoll.cpp:730`), and the grid follows it a page at a time, eased, when it leaves the view (`:739`).
### DR-ROLL-7 A note is heard when it is made (R-ROLL-7)
**Grammar.** `note preview <ch> <pitch> [--vel <1…127>] [--length <beats>]` (`core/service/ServiceTransport.cpp:335`)
— not an edit (no undo step, the song stays saved), never in a render; refused naming why for a strip that is not
an instrument's, an unknown strip, a pitch out of 0–127 and a velocity out of range. A pitch is a number, a name
(A4) or the strip's kit's pad. It compiles a song of its own (`:341`): the song's header, ports and mixers, the strip
— routed straight to the master, unmuted, unsoloed, its fader and pan kept — its rack, and a one-note pattern on a
clip at beat 0; master gain 0 dB, no master rack, no bindings (a formula may read the song's other addresses: the
preview is the strip's values as they stand). The SAME `compile` and `Engine` as the song, so the preview is the
strip's sound — the L2 test hears it equal, to 1e-6, to the render of that note on that strip in the song. Event
`note.previewed strip= pitch=`.
**The stream.** `NotePreview` (`core/NotePreview.cpp`) is the Auditioner's pattern for an engine: its own output
stream and thread, opened by a preview and closed after a second of quiet (`core/NotePreview.cpp:111`; the service hands a voice
over THEN starts a thread when none runs, and the thread looks for one before it closes, so neither misses the
other). A voice — engine and port buffers, sized on the service's thread — arrives through an atomic slot (`ServiceTransport.cpp:320`
in the service); the one ringing fades out under it over 480 samples (10 ms), never a click; a voice ends when it
has stayed under −80 dB for 0.1 s after its note, or four seconds past it; a finished voice goes back through a
ring of atomic slots and is freed by the service's `pump` (`ServiceTransport.cpp:83`) — the thread never
allocates, frees or locks.
**The roll** (`PianoRoll::preview`, `app/widgets/PianoRoll.cpp:243`) sounds through the track of its clip
(DR-ROLL-6), as a line beside the edit's own: a note added (`:806`, at the last length), a note pressed, each pitch
row a dragged note enters (`:703`), a Steps cell turned on, and a key pressed or swept (`:643` — the key lit while
held, eased in and out); lengths 1/8 to 1 beat. A pattern on no track is made, not heard.

### DR-ROLL-8 A selection in the roll, and what is done to it (R-ROLL-8)
**Grammar.** `notes move <pt> "<pitch@beat> …" [--by <beats>] [--semi <n>]` and `notes delete <pt> "<pitch@beat>
…"` (`core/service/ServiceCompose.cpp:280`) act on several notes as ONE edit (one undo step); `--by` may be
negative. Refused whole, naming the first: a name that matches no note, a note that would land on one not moved
(a chord moved together may pass over its own places), a pitch outside 0–127, a start before beat 0. A copy is
`notes add` with full tokens (`pitch@beat:length:vel`), which already replaces a note it lands on.
**The roll** (`app/widgets/PianoRoll.cpp`). The selection is the view's: a set of note keys, emptied of any note
the model no longer has. A click on a note selects it, Shift+click adds or removes it (`:1172`); a click on empty
grid adds a note (R-ROLL-2) and lets go of the selection; Ctrl draws a box that selects every note it touches
(Shift keeps what was selected) — wherever the press is, even on a note: Ctrl is selecting and never moves a
note, and a Ctrl+click on a note selects it (the box of no size touches it) (U-10, R-ROLL-8 amended). Selected notes are lit — brighter, outlined in the
accent — eased in and out (120 ms). A selected note dragged (with others selected) carries the whole selection
with the pointer, kept at or after beat 0 and inside 0–127, heard at each row the pressed note enters (R-ROLL-7),
and lands as ONE `notes move` (`:979`, `moveSelection` `:295` — the moved notes appear where they were let go
at once, still selected). Keys (`PianoRoll::action`, `:502`): ↑/↓ a semitone (Shift: an octave), ←/→ a snap step
(a sixteenth when Off) — `notes move`; Delete/Backspace — `notes delete`; Ctrl+D — the selection again right
after itself, by its span (earliest start to latest end, up to the snap), `notes add`, the copy selected
(`addSelected`, `:494`); Ctrl+C — to the clipboard every roll shares (the window layer's); Ctrl+V — pasted into
the roll at the same beats, selected — this roll's or another's, so notes go from one clip's pattern to another's;
Ctrl+A — every note; Escape — none. **The keys** go to the front roll when the pointer last pressed in its window —
the title bar too — judged once the press is routed, so a press that brings a roll forward gives it the keys
(`app/App.cpp:143`, `WindowLayer::inFrontRoll`, `app/widgets/FloatWindow.cpp:375`); and a roll just opened — from
its clip on the lanes, the browser, anywhere — takes them at once (`openRoll` calls `onRollOpened`, `:253`; the App's
hook, `app/App.cpp:87`), so Ctrl+V right after opening another clip's roll pastes there. The App offers a key to that
roll first (`:839`); a press on the lanes gives the keys back to them. (D-25: the keys used to follow a press in the
notes' area only, judged before the press was routed — a roll opened from its clip, or raised by its title bar, left
them with the lanes, whose Ctrl+V duplicated the copied CLIP.) UI `test_notes_copied_from_one_clips_roll_to_anothers`
(`app/tests/ui/uiTests.cpp:3153`).

### DR-ROLL-9 The pattern grows to hold what is written in it (R-ROLL-9)
`growToHold` (`core/service/ServiceCompose.cpp:174`) runs inside every edit that writes notes — `note add` (a chord
too), `note move` (moved or resized), `notes add` (a paste, a duplicate), `notes move`, `pattern steps`: when a
note's end passes the pattern's, the pattern's length becomes the end of the bar that holds it (`barBeats`, the
song's signature), and every clip whose length WAS the pattern's — it played it whole — takes the new length; a
clip cut shorter, or longer and looping on, keeps its own. Part of the same edit (one undo step takes both); the
command's output says it ("pt_1 grew to 8 beats to hold its notes; ac_1 with it"), so a script reads it; event
`project.changed what=pattern.length`. An end set back by hand (`set <pt>.length=`, R-ROLL-5) is obeyed, and the
audit names the notes it leaves outside. In the roll the end handle eases to the new end (DR-ROLL-1). Closes the
UX audit's U-3 (a note past the end was dimmed and never heard). L2 `test_a_pattern_grows_to_hold_its_notes`
(mutant: a whole-pattern clip that does not follow, seen red).

### DR-ROLL-10 The length a click makes is shown and chosen (R-ROLL-10)
The roll's toolbar names it after Quantize… (`PianoRoll::lengthRect`, `app/widgets/PianoRoll.cpp:559`; drawn at
`:1453`): "Length 1/16*" and a caret — a note value (`lengthName`, `:583`: 1/32 … 1/1, a beat being a quarter, else
"<n> beats"), starred while it is the LAST length (the note last pressed, made or resized — DR-ROLL-14 — the default). Its menu (`:996`)
offers "Last used (…)" and 1/32, 1/16, 1/8, 1/4, 1/2, 1/1; the choice is the view's — no line — and a click on the
grid makes a note of `clickLength()`, the choice or the last length. Closes the UX audit's U-4.

### DR-ROLL-11 Note tools (R-ROLL-11)
Each tool is ONE edit on a pattern (`core/service/ServiceCompose.cpp`), on every note or on the ones `--notes
"<pitch@beat> …"` names (`scope`, `:228`), and where it draws, a seeded splitmix64 stream (`Rng`, `:75`; `seedOf`,
`:87` — `--seed`, else a hash of the notes, printed), so the same line on the same notes writes the same notes.
**Scale lock** — `pattern scale <pt> <key> <scale>` | `off` (`:371`; `#apattern … scale="D dorian"`): twelve scales
(`scaleKinds`, `model/Project.cpp:1561`; a key in sharps, `parseScale` `:1403`), published as `patterns[].scale` and
its pitch classes `scaleMask`. A note PLACED — `note add` and each note of its `--chord` (`core/service/ServiceEdit.cpp:1032`),
`notes add` — goes to the nearest pitch of the scale, a tie DOWN (`snapToScale`, `model/Project.cpp:1613`), which
makes a major chord stamped on any degree that degree's own triad; a note MOVED in pitch (`note move --to-pitch`,
`ServiceEdit.cpp:926`; `notes move --semi`) goes on the way it moved to the scale's next pitch, two moved notes landing
on one refused whole. A move in time, an import, a step row, a recording and `pattern transpose` keep their pitches.
**Chance** — `Note::chance` 0 … 1 (`#note … chance=0.5`, written below 1; the note token's fifth field
`C4@0:0.25:100:0.5`; `note add|move --chance`, `notes set <pt> "<sel>" --vel --chance --length`, `:344`): the compile
rolls it per clip and per pass of the pattern (C4, `core/Compile.cpp:447`) by `noteSounds` (`model/Project.cpp:1696`
— FNV-1a over the clip id, splitmix64's finaliser over the pass, the pitch and the tick, 53 bits against the chance):
no clock, so a render is pure and two clips of one pattern vary apart; `project export-midi` rolls the same
(`core/service/ServiceMidi.cpp:281`), so the file is what the render plays. **Grooves** — `pattern quantize --groove
<name> [--strength 0…1] [--notes]` (`core/service/ServiceCompose.cpp:400`): nine templates (`grooves`, `core/service/Notation.cpp:172` — the MPC's
54/58/62/66 % swings, shuffle, hip-hop, house, push, lazy; empirical, listed in the API document) of grid steps from
beat 0, each an offset (a fraction of a step) and an accent (a velocity scale); strength moves a start and a velocity
that part of the way; a groove with `--grid`/`--swing` refused. **Humanise** (`core/service/ServiceCompose.cpp:450`): each start within ± `--timing`
beats, each velocity within ± `--vel` (neither given: 0.02 and 8), a triangular spread; never before beat 0, never
onto another of its pitch (it keeps its time). **Legato** (`:483`): each note to the next later start among those
acted on, a chord's notes together, the last to the pattern's end — overlaps shortened. **Arpeggiate** (`:501`): on
each `--rate` step of the beat grid inside the notes' span, what is held there (pitches ascending, a pitch once,
the loudest) over `--octaves`, indexed up, down, updown (a cycle of 2n−2: the turns once) or by the seeded stream;
`--gate` of a step long, at the held note's velocity and chance; the index back to the first after a step with
nothing held; the notes arpeggiated replaced, a step on another note of its pitch replacing that one. L2
`test_scale_lock_places_and_moves_notes_on_the_scale`, `test_note_chance_is_rolled_per_pass_and_the_render_stays_pure`
(heard 21 of 32 at 0.5; the same bytes twice; a second clip apart; the exported notes = the heard),
`test_the_note_tools_humanise_groove_legato_arpeggiate`; model `test_note_chance_and_scales_round_trip_and_are_checked`.
Mutants: the roll forgetting the clip, a tie snapping up, updown repeating its turns, humanise drawing from a stream
not of its seed, the compile or the MIDI file ignoring chance — each red. **The roll's tools.** The roll's toolbar is two
rows (`PianoRoll::kBarH`; its window opens 720 × 520, `app/widgets/FloatWindow.cpp:248`): ▶, snap and Notes | Steps;
then Quantize…, Length, Scale, Chord and Tools (`quantizeRect`, `app/widgets/PianoRoll.cpp:560`), each menu chip with a
drawn caret (`caret`, `:30` — the UI font has no ▾). Every tool acts on the selection when there is one, else the
pattern (`notesFlag`, `:578`, ` --notes "<sel>"`), each ONE line; the menus list what the model publishes —
`chords`, `scales`, `grooves` (`AppModel`, filled from `chordQualities` / `scaleKinds` / `grooves`). **Scale ▾**
(`:1096`): No scale, the scales in its key (C until one is chosen), Change key… (a second menu of the twelve, keeping
the scale) → `pattern scale`; the rows off the scale dim and their keys darken, per pitch class, eased (`:668`).
**Chord ▾** (`:1117`): Single notes, or a quality — the view's; a click on the grid then stamps it, its root the row
clicked (`note add <pt> --chord <root><q> --octave <o> --at --length`, `:1181`), a scale-locked pattern making it the
key's own. **Tools ▾** (`:1128`): Humanise, Legato, Arpeggiate up · down · up-down · random (at the snap), Chance 50 % ·
100 % (`notes set`). **Quantize…** adds a line per groove. **The Chance lane** — a fourth tab beside the lane below
(`kVelH` 65): each note's odds as a stem, dragged as velocity is, to a hundredth (`:854`), landing ONE `note move
--chance` (`:1445` draws them); a note is drawn fainter the less it sounds. A velocity or chance changed elsewhere
(humanise, a groove, `notes set`) eases to its value (`:651`). UI `test_the_rolls_note_tools`; shots `piano-roll-tools`,
`piano-roll-scale-fading`. Mutants: the scale's rows snapping, a chord click sending one note, a shell's velocity
snapping, the tools ignoring the selection — each red.

### DR-ROLL-13 Alt+wheel changes the selection's value in the lane shown (R-ROLL-13)
**The lines.** `notes set <pt> "<pitch@beat> …" --vel-by <±n> | --chance-by <±x>` (`core/service/Command.cpp:370`,
`core/service/ServiceCompose.cpp:364`) moves each note's own velocity / chance by that much, kept within 1–127 / 0–1;
with `--vel` / `--chance` it is refused (one or the other). `expr set <pt> "<beat>:<value> …" --lane <l>`
(`core/service/Command.cpp:251`, `core/service/ServiceMidi.cpp:68`) places many points of one lane in ONE edit — each
added, or the point at that beat moved — refused whole, naming the token, for a token that is not `<beat>:<value>`, a
beat below 0 or named twice, a value outside the lane's range; a point past the end grows the pattern (R-ROLL-9).
**The roll.** The host passes Alt with the wheel (`linux_main.cpp:247`; `App::wheel`, `app/App.cpp:642`). An Alt+wheel
over the grid or the lane below (`PianoRoll::handleGesture`, `app/widgets/PianoRoll.cpp:1206`) is a TURN (`wheelTurn`,
`:343`): the selection's notes — or, with none, the note under the pointer, or the stem under it in the lane — on the
lane shown (Velocity, Chance, Bend, Mod); a notch is 40 of `App::wheel`'s units (a touchpad's fractions add up), up
raises. Its values are drawn at once and eased there (`showWheel`, `:420`: a note's velocity or chance, the lane's
points — re-applied at every bind, `:165`, so a model arriving mid-turn does not take them back). Bend and Mod are
`wheelPoints` (`:370`): every point under a note moved; a point at its start and its last tick, moved from the lane's
value there; one tick before its start and one at its end — unless another note of the turn is there — keeping the
value the lane had, so the lane outside the notes plays as it did. The turn is ONE line (`commitWheel`, `:450`) once the
wheel has rested 400 ms (`advance`, `:625`), or at once when another gesture comes (`:868`) or a key acts (`:504`): the
`notes set … --vel-by 4n | --chance-by 0.05n`, or the `expr set`; a turn back to where it began sends nothing.
`wheelPending()` publishes a turn in hand (`app/widgets/PianoRoll.h:151`). L2 `test_notes_and_lane_points_moved_by_the_wheel`
(`core/tests/serviceTests.cpp:2646`: each note's own moved and clamped; refusals; many points, moved and added, the
pattern grown, one undo); UI `test_alt_wheel_changes_the_lane_shown` (`app/tests/ui/uiTests.cpp:3031`: a velocity caught
mid-ease and still there after a model mid-turn; nothing sent while it turns; the selection's line once it rests; a
note's chance under the pointer; a turn undone sends nothing; the next gesture lands it at once; Bend under two notes,
eased, and the lane after them at rest; Mod under a note between points: moved under it, as it was outside it); shot
`piano-roll-alt-wheel` (`app/tests/shots/renderShots.cpp:1064`). Mutants, each seen red: a line at each notch, the next
gesture waiting, a turn undone still sent, no edge points, nothing under the pointer, a bind dropping the turn, the App
dropping Alt, the velocity drawn at once; in the service, `--vel-by` unclamped, `--chance-by` ignored, `expr set` never
moving a point, a point past the end not growing the pattern.

### DR-ROLL-12 Shift-drag copies notes (R-ROLL-12)
The roll reads Shift at every step of a note's drag or the selection's (`PianoRoll::handleGesture`,
`app/widgets/PianoRoll.cpp:956`) and as it is let go (`:972`): held, the drop is `copyNotes` (`:319`) — ONE `notes add
<pt> "<pitch@beat:length:vel[:chance]> …"`, the tokens `tokens(…, full)` writes, so each copy keeps its length,
velocity and chance (a copy landing on a note replaces it, as `notes add` does); the copies are there at once (the
pointer showed them) and they are the selection. Not held, the drag is the `note move` / `notes move` it always was.
While held `mCopyAmt` eases in and out (120 ms, `advance`, `:627`): the originals are drawn where they are, at full
strength once it is in (`paintNotes`, `:1276`), and a "+" in the accent rides beside the note in hand (`:1290`). A drop
that moved nothing copies nothing (a Shift+click stays a click: it adds a note to the selection or takes it out).
UI `test_the_roll_copies_by_shift_drag_and_makes_notes_like_the_last` (`app/tests/ui/uiTests.cpp:2919`: the copy look
caught mid-ease, in and — Shift let go mid-drag — out; one note and a selection copied, their tokens, the originals
kept, the copies selected and at full strength; without Shift it moves); shot `piano-roll-copy-dragging`
(`app/tests/shots/renderShots.cpp:1042`). Mutants: Shift ignored at the drop, the copy look snapping, the copies keeping
no length or velocity, the copies not selected, the copies faded in — each seen red.

### DR-ROLL-14 A new note takes the last selected note's attributes (R-ROLL-14)
The note last pressed — clicked, Shift+clicked, dragged, resized, or its stem in the lane below (`app/widgets/PianoRoll.cpp:917`) — is the
roll's model (`mModelKey`, `app/widgets/PianoRoll.h:266`); `takeModel` (`app/widgets/PianoRoll.cpp:474`) reads its
velocity, chance and length (the length is R-ROLL-10's "Last"), again at every bind (`:96`, so a shell's edit of it is
followed), and keeps them once it is gone. A note the roll moves stays the model where it went (`:1014`, `:980`). A
click on empty grid sends `note add <pt> --pitch --at` with `clickAttrs()` (`:487`): `--length` (the Length menu's
choice, else the model's), `--vel` and `--chance` — left out while they are `note add`'s own defaults (100, 1), so
a click before any note is pressed sends the line it always did (`:1184`); a chord stamp takes them for each of its
notes, and a step in Step mode the velocity and chance with the step's length (`:1160`). The note made is heard at
that velocity (R-ROLL-7). `clickVel()` / `clickChance()` / `clickLength()` publish them (`app/widgets/PianoRoll.h:146`).
UI `test_the_roll_copies_by_shift_drag_and_makes_notes_like_the_last` (the defaults' line; a pressed note's
attributes in the next click's line and the note made, heard at its velocity; a shell's edit followed, a deleted
note's values kept; the model moved with a note and with a selection). Mutants: the press not taking the model, a
click leaving out the velocity, a shell's edit not followed, the preview at 100, a moved note — alone or in a
selection — losing the model: each seen red.

### DR-VST-1 Our instruments as VST3 plugins (R-VST-1…5)
**The SDK:** Steinberg's `vst3sdk` **v3.8.1_build_84**, cloned recursively to `~/sdk/vst3sdk`; its
`LICENSE.txt` (and `base`, `pluginterfaces`, `public.sdk`'s) is the **MIT licence** (Steinberg relicensed
the SDK from 3.8; VSTGUI, which the plugins do not use, keeps its own). Found through `VST3_SDK_ROOT`
(a CMake variable or the environment); with none, `core/DigitalSignalProcessing/apps/vst3` prints
`arstro VST3: no SDK at …` and builds nothing (verified by configuring against a missing path). The
SDK's CMake wants 3.25 and this machine has 3.22, so the DSP library compiles the SDK's sources itself
from the SDK's own source lists, as PIC static libraries scoped to that directory.
**The plugins** (DSP REQ-vst-1…5, `core/DigitalSignalProcessing/apps/vst3/README.md`):
`ArstroBasicSynth.vst3` and `ArstroDrumMachine.vst3` — a generic processor and controller around
`DeviceRegistry::create("synth" | "drums")`, the object Solaris renders; a parameter per registry
parameter, id = its index; normalised by the DSP library's `normalizedFromValue` / `valueFromNormalized`
(REQ-device-7); a block split at every note and parameter point; state as the registry's text.
`cmake --build build --target vst3_install` copies both to `~/.vst3` (done on this machine).
**One text:** Solaris's `parseParam` / `paramText` (`core/Compile.cpp:124`, `:126`) ARE the DSP library's
`paramFromText` / `paramToText`, so a `.slp`'s device values and a plugin's state are the same text by
construction (the model and service suites, unchanged, still pass on it). The device panel's log taper
is the same formula as `normalizedFromValue`'s (N2) but its own copy: the front end does not link the
DSP library (law 13), and every value it sends is parsed by the service.
**Verified** (root `ctest`): `vst3_validate_synth`, `vst3_validate_drums` — the SDK's validator, 47
tests, 0 failed, each; `vst3_equivalence` — each bundle hosted offline equals the device rendered
Solaris's way, 0 of 96 000 samples differing, not silence, its state read back as text.

### DR-MIX-15 The sidechain: a key into a later strip's compressor (R-MIX-15, R-EDM-3)
`send add <ch> --to <ch2> --sidechain [--pre] [--gain]` makes a KEY (`#asend … sidechain=true`,
`<send>.sidechain` settable): `feedsForward` (`model/Project.cpp:1137`) — the one rule, later in processing
order, its own mixer included, as for every route since R-MIX-4's amendment — and the validator refuses a
key to the master or a port (`:1106`); `strips[].keyTargets` (`keyTargetsOf`, `:1033`) is what a
picker offers. The engine sums a key into the target's KEY buffers instead of its input (`routeKey`,
`engine/Engine.cpp:624`, through its latency-compensating delay, DR-MIX-17) and hands them to every device
whose type `takesKey` before it runs (`:837`) —
the DSP Compressor with Sidechain on detects on it (DSP REQ-fx-sidechain-1); silence when nothing keys
it. A strip silenced by another's SOLO still keys (`keyLive`, `core/Compile.cpp:317`; `engine/Engine.cpp:925`) and
a key does not pull its source into a solo (`Compile.cpp:52`), so a soloed bass keeps its pump while the
kick stays silent; a MUTED strip keys nothing (live, a mute in a song with keys swaps the engine,
`core/service/ServiceTransport.cpp:530`). The audit names a key that nothing on its strip listens to — no
compressor or gate with Sidechain on, and no key-taking type without that switch (the vocoder, whose key is its
carrier, R-FX-16, always listens) (`core/service/ServiceModel.cpp:666`). In the dock a key reads "key <target>" in the solo amber,
and a strip's menu offers "Sidechain to ▸" = its `keyTargets` (`app/widgets/MixerDock.cpp:1702`).
Measured (L2): a ghost kick (−120 dB fader, pre-fader key) through −30 dB at 4:1 dips a −6 dBFS bass
6–22.5 dB 15–35 ms after each kick, back within 1 dB before the next; identical with the bass soloed;
nothing with the kick muted.

### DR-EDM-2 The metronome (R-EDM-2, R-TIME-4)
`settings set metronome=on|off`, `metronomeLevel=<dB>` (machine settings, DR-SET-3). The click is the
`Player`'s (`core/Player.cpp:506`), after the engine, so it is in what is heard and never in a render: its
own Drum Machine, made in `start`, triggers the rim on every beat and the cowbell on the bar's first, on
the beat's exact sample, into the clock device's first two channels at the level; on/off, level and
tempo are atomics the service sets (at play, on every live edit, on `settings set`), so a change is
heard at once and the audio thread never allocates. Measured (L2): on an empty song a click starts on
each beat's sample (24 000 frames apart at 120 bpm), the bar's first differs, off silences the next
beats, and a render with it on equals one with it off byte for byte.

### DR-EDM-4 The limiter (R-EDM-4)
The DSP library's `limiter` (REQ-fx-limiter-1: gain, ceiling, release, lookahead — its latency) is a
registry effect, so it is in "+ Effect", on the master or a strip, with no Solaris code; the output
never exceeds its ceiling by construction (measured in the DSP suites). Its lookahead is a latency, reported
(DSP REQ-device-8) and compensated (DR-MIX-17): on the master the render still starts on beat 0; on a strip
the others wait for it.

### DR-EDM-4a A true-peak limiter (R-EDM-4a)
The library's limiter has `truePeak` off|on (REQ-fx-limiter-2, DSP `aa56ae2`; off by default, so no song's sound
moved): on, its ceiling holds BETWEEN samples — detected every ⅛ sample through the meters' interpolator, each
frame's gain meeting its ±8 neighbours' needs — at 16 samples more latency, which the engine compensates like any
other (DR-MIX-17). A registry parameter, so `set <dv>.truePeak=on`, its row in the device window, its address and
its API line come with no Solaris code. L2 `test_a_true_peak_limiter_on_the_master`: a bright square (2.6 kHz,
filter open) driven into a −1 dB master limiter reads −0.52 dBTP with sample-peak limiting (0.48 dB over between
samples) and −1.0000 with `truePeak` on, by the library's BS.1770 meter over the rendered file; the render starts
on beat 0 either way. Mutants (in the DSP suites): no between-sample detection, the neighbourhood 8 short — red.

### DR-EDM-7 The loop region on the ruler (R-EDM-7, R-TIME-4)
`Timeline` draws the model's `transport.loopFrom … loopTo` as a brace on the ruler, its ends marked, in
the accent, and tints the region on the lanes under the clips (`app/widgets/Timeline.cpp:1528`, `:1317`;
`loopRect`, `:702`). It is eased (`:477`): it fades in where it is when a loop appears, fades out when
it goes, and a loop changed by anyone moves there over 220 ms. `loopGesture` (`:710`): Shift-drag on the
ruler draws the brace under the pointer (both ends on the lanes' snap step, like a seek — DR-UI-10,
`:719`) and sends ONE `transport loop <from> <to>` on release, the brace staying where it was let go
(both ends on one line is a Shift-click: no region, `:733`); a click inside the brace sends `transport
loop off`; a plain click elsewhere on the ruler still seeks. The engine already loops (DR-PLAY-1), so
the playhead wraps at the brace's end while playing.

### DR-EDM-8 A sampler (R-EDM-8)
The DSP library's `sampler` (REQ-inst-sampler-1: chromatic or one-shot, a span, reverse, an ADSR) is a
registry instrument that `takesSample`: the host decodes, the device copies. Solaris stores its sound
on the device node (`#aeffect … sample=<path>`, relative to the song as a clip's `src`), sets it with
`strip add --kind instrument --instrument sampler --sample <file>`, `clip add --instrument sampler
--sample <file>` (one drop, one command) or `set <dv>.sample=<file>` (`""` takes it away) — resolved and
refused unread by `resolveSample` (`core/service/ServiceEdit.cpp:124`; `:196`; `SolarisService.cpp:784`),
refused on a type that plays none. Compile decodes it through the same cache as clips and puts it on the
device's description (`core/Compile.cpp:235`); the engine hands it in at build, never on the audio thread
(`engine/Engine.cpp:98`); a change of sound is a structural live update (an engine swap). The model
gives `devices[].takesSample`, `.sample` and `deviceTypes[].takesSample`; the audit names a sampler with
no sound or an unreadable one (`core/service/ServiceModel.cpp:595`). In the UI a sampler's window names
its sound where others name their type (`DevicePanel::sampleText`, `app/widgets/DevicePanel.cpp:580`),
and a browser sample dragged over it lights the window (eased) and drops as ONE `set <dv>.sample=`
(`WindowLayer::samplerAt`, `app/widgets/FloatWindow.cpp:326`; `app/widgets/ProjectScreen.cpp:109`).
Measured (L2): at the root the render IS the decoded file sample for sample (a mutant whose engine skips
`setSample` fails it), an octave up is 2 kHz and over in half the time.

### DR-EDM-9 Audition in the browser (R-EDM-9, R-BROWSE-2)
`audition <file>` / `audition stop` — a machine command (not an edit: no undo, the song stays saved),
with or without a song open. The file is decoded through the clips' cache and played by an
`Auditioner` (`core/Auditioner.cpp:40`) — its OWN output stream and thread, so a preview is heard
whether the song plays or not, at the `auditionLevel` setting (dB, default −6), to its end, then it
stops itself; it never allocates on its thread and is never in a render (a render does not know it).
`auditionCommand` (`core/service/ServiceTransport.cpp:307`) refuses a file it cannot read naming it; the
service's `pump` (`:87`) carries its progress into `audition.{file, playing, progress}` and announces its
end (`audition.changed … playing=0`); the model keeps it across refreshes, and the App re-binds while it
plays and once when it ends (`app/App.cpp:444`). In the browser a click on a sample hears it, a click on
the one being heard stops it (`app/widgets/Browser.cpp:455`); its row fills as it plays with an accent
bar along its foot, keyed by the file and eased in and out (`:351`, `:560`) — a double-click still places
it. Settings › Playback has PREVIEW LEVEL. Measured (L2): the live stream carries the decoded file at
the audition level, sample for sample; it ends itself, said.


### DR-UI-10 The grid follows the zoom (R-UI-10, R-TIME-5)
**The levels.** The lanes' grid has seven: a bar (the song's meter), a beat, and 1/2, 1/4, 1/8, 1/16
and 1/32 of a beat (`Timeline::gridSpan`, `app/widgets/Timeline.cpp:59`). How much of a level is drawn
is a smoothstep of its line spacing at the EASED zoom — nothing closer than 6 px, all of it from 16 px
apart (`kGridHidePx`, `kGridFullPx`, `app/widgets/Timeline.h:103`; `room`, `Timeline.cpp:30`;
`gridAlpha`, `:65`) — recomputed every frame, so a Ctrl+wheel zoom (eased 220 ms) fades a level in or
out with it and never pops one. A line is drawn once, by the coarsest level it belongs to: bars
strongest (white 7 %), beats next (4 %), the divisions faintest (2.2 %); every fourth bar is always
drawn, so a far-out song keeps its phrases (`paintGrid`, `:1505`). The ruler repeats the levels as ticks
— long for a bar, shorter as they get finer — and its labels are MEASURED against their room: a bar's
number fades in as its every-1/2/4/… bars get room for the widest number in view, and zoomed in the
beats are named `bar.beat` (`2.3`), fading in as a beat gets room for one (`paintRuler`, `:1527`; the
labels `:1560`). The old fixed thresholds, which swapped the labels in one frame mid-zoom, are gone.

**The step.** THE snap step is the finest level whose lines are at least 11 px apart (`kSnapPx` — past
half drawn: you snap to lines you can see), never coarser than a bar; at the deepest zoom it is 0 and
nothing snaps (`snapStep`, `:73`; `atDeepestZoom`, `:71`). `snap()` (`:81`) is the ONE rounding of
every gesture on the lanes: a ruler click → `transport seek` (`:1217`), a clip drag (`:1125`), the loop's
Shift-drag (`:905`), an automation point added or dragged (`app/widgets/TimelineAuto.cpp:559`, `:480`),
the browser's drop and its hint (`app/widgets/ProjectScreen.cpp:130`) — at the deepest zoom the tick
under the pointer. Every gesture is still ONE command, its beats printed to the tick: rounded to 1/960
and spelled with the fewest decimals that keep it (`Timeline::beatText`, `Timeline.cpp:44` — the `%g`
it replaces kept six significant digits, so past beat 1000 a line lost ticks). The step is published
LIVE for tests (`snapStep`, `snapLabel`, `gridAlpha`, `snapLabelAmount`, `Timeline.h:153`) and named in
the ruler's corner over the lane headers — `Snap Bar`, `1/4` … `1/128`, `Off`, in the piano roll's note
values (a beat = 1/4) — cross-faded 200 ms when it changes, older names fading out from where they are
however fast the wheel turns (`stepName`, `Timeline.cpp:88`; `advance`, `:633`; `paintRuler`, `:1527`).

**The zoom.** A lattice of ×1.25 notches about 28 px/beat, 8 out (4.7 px/beat: a 4/4 bar is 18.8 px)
and 14 in (637 px/beat), so a notch in and a notch out are exact inverses and the deepest zoom is one
place (`deepestPpb`, `Timeline.cpp:31`). **Decision:** the deepest zoom rose from 320 px/beat so that 1/32 of a beat gets room —
20 px apart at 637, fully drawn, and THE step one notch before snapping turns off. The step by zoom:
a bar below 11.5 px/beat, a beat from 11.5, 1/2 from 22.4 (a song opens at 28: bars, beats and halves
drawn, the step 1/2), 1/4 from 55, 1/8 from 107, 1/16 from 209, 1/32 from 408, nothing at 637.

**Proof.** `test_the_grid_follows_the_zoom` (`app/tests/ui/uiTests.cpp:4136`) drives the real service:
at the default zoom a ruler click at beat 3.3 seeks 3.5 (a fixed quarter says 3.25); a level's alpha
is caught mid-fade during an eased two-notch zoom, between its two ends; at 1/8 a seek, a clip drag
(on the step while held, then `clip move ac_1 --at 3.125`) and an instrument dropped from the browser
land on the same step, the step's name caught mid cross-fade; at the deepest zoom a click seven ticks
past beat 2 is exactly `transport seek 2.007`; all the way out a click at 9.9 is `transport seek 8`.
Mutants — a fixed 1/4 snap, a level's alpha taken from the TARGET zoom (it pops), a deepest zoom that
snaps — each fail it. Shots `lanes-zoomed-in`, `lanes-zoomed-out`, `lanes-zoom-mid` (mid Ctrl+wheel)
(`app/tests/shots/renderShots.cpp:352`).
### DR-MIX-16 The mixer's numbers bound (R-MIX-16)
**One menu.** `paramMenuItems` (`app/widgets/ParamMenu.cpp:51`) builds the right-click menu of every number a
formula can drive from a `ParamTarget` (address, formula, value as shown, reset, bindable): Create Automation
unless it already IS one (`auto create <address>`), Formula… (cosmo's rename field seeded with the formula or
"=", → `set <address>="=<typed>"`, quoted when it has a space), Clear Binding when bound (`bind clear
<address>`), Reset to Default (`set <address>=<default>`), and Copy Address / Copy Value / Copy as Formula
(DR-UI-11). The device window (`DevicePanel::openParamMenu`, `app/widgets/DevicePanel.cpp:623`) and the dock
(`MixerDock::openParam`, `app/widgets/MixerDock.cpp:1372`, offered first on a right-click, `:1593`) both call
it, so the menus cannot drift; a hosted third-party VST3's parameter list (R-VST-6, not built yet) will call
it the same way. In the dock: a fader → `<ch>.gain`, a pan → `<ch>.pan`, a send's row on a card or its cell
in the matrix → `<sd>.gain`, the master fader → `project.masterGain` (the addresses that already bind,
R-AUTO-1); Reset is 0 (unity, the centre); any other right-click on a card is still the strip's menu.

**The value at the heard position, published.** `bindings[].live` (`AppModel.h`). Stopped: `evaluateLive`
(`core/service/ServiceModel.cpp:500`) compiles the song as `eval` does and evaluates every binding with the
engine's own `evaluateBinds` at the transport's position (an inert binding: its own value) — so it equals
`eval <address> --at <position>`. Playing: after every block the `Player` copies `Engine::bindValues()` into a
ring of 64 slots of pre-sized atomics (`keepLive`, `core/Player.cpp:297` — stores only; a sequence number
brackets each write), each stamped with the frames handed to the device and the generation of the engine
that played it; `pump` (`core/service/ServiceTransport.cpp:304`) reads the slot the listener hears now —
the newest at or before the newest minus the device's latency, of the engine the service last built
(`liveValues`, `core/Player.cpp:345`, its slot `pickSlot`'s, `:305`; `buildLive` bumps the generation, `ServiceTransport.cpp:81`, and a
`Swap` carries it, so a new bind order is never read with the old one) — and maps the engine's bind
order to addresses. While playing, `refreshModel` keeps the pumped values and evaluates only a binding new
since (`ServiceModel.cpp:84`, `:360`). At most 256 bindings report (`Player::kMaxBinds`). Not in the stable
dump: it moves with the transport.

**The dock draws it** (`MixerDock::advance`, `app/widgets/MixerDock.cpp:954`): a bound fader or pan takes its
live value; while playing it is SET each frame — it follows the audio like the playhead — and stopped a change
(a seek, a curve edited) eases 220 ms; a new driver (the formula changed) or play pressed mid-ease is a
catch-up ease that lands when it ends and then follows (`follow`, `:995`). A bound send shows its live
level. What drives each: a tag over the fader — `auto au_1`, `= ch_3.gain`, `= -2 + 2 * s…` (`paintTag`,
`:1778`) — "ƒ" after a bound pan and before a bound send's level, the readout in the accent; every tag fades
in and out (200 ms) and its text cross-fades when the formula changes (`:1136`).

Measured. L2 `test_a_bound_value_is_published_live` (`core/tests/serviceTests.cpp:1830`): stopped, `live` is
`eval`'s number at the position; playing (a fake clock device), a −2 dB-a-beat ramp is within 0.1 dB of the
curve at the heard position at beats 2…12 and falling, a link (`=ch_2.gain / 2`) exactly half of it, a device
formula `1000 + 500 * sin(beat)` within 25 Hz; after a structural edit while playing the new engine's order is
read; 0 allocations on the audio thread. Mutants: `pump` ignoring the player → stuck at 0 dB at beat 2;
`bindValues()` copied on the audio thread → 1019 allocations. UI `test_the_mixers_numbers_bind_from_the_dock`
(`app/tests/ui/uiTests.cpp:2143`): a fader's right-click is the parameter menu, Create Automation is `auto create
ch_2.gain` and the tag fades in (caught mid-tween); Formula… on a pan sends `set ch_3.pan="=0.25 - 0.75"` and
the knob EASES onto −0.5; a send copies `-8.0 dB` and `=sd_1.gain`; the master copies `project.masterGain`;
Clear Binding fades the tag out; playing, the fader equals `faderPos(live)` every frame and moves. Mutants:
the dock ignoring `live` → the pan never reaches −0.5; easing instead of following while playing → fader
0.707946 against live 0.705559. Shots `mixer-bound` (a fader on an automation, one linked to it, an auto-pan,
a send and the master on formulas) and `copied-toast`.

### DR-UI-11 IDs shown and copied (R-UI-11)
**The setting.** `showIds = on|off` is a MACHINE setting (`core/Settings.cpp:28`), `settings set showIds=…`
(`core/service/ServiceMachine.cpp:107`), published as `settings.showIds` — so an agent turns the ids on as the
View menu does (View › Show IDs / Hide IDs, `app/App.cpp:216`).

**Drawn, faded.** `ProjectScreen` eases ONE amount from the model's setting (`app/widgets/ProjectScreen.cpp:225`,
200 ms; `idsAmount()` is the live value) and hands it to every widget that draws ids; `drawNameWithId`
(`app/widgets/ParamMenu.cpp:96`) draws a name with its id right-aligned in the accent's mono, the name giving
way by the EASED amount. Where: lane headers (the lane's id; a strip's own row, the strip's) and automation
rows (`au_n`) (`app/widgets/Timeline.cpp:1594`, `:1019`); clips (`ac_n`, and a note clip's pattern `pt_n`;
`:862` — "linked ×N" fades out for them); in the dock each strip card (`ch_n`), the master (`master`), each
rack chip (`dv_n`) and each send (`sd_n`, cross-fading with "→ name") (`paintCard`,
`app/widgets/MixerDock.cpp:1845`); a device window's rows show their FULL address (`dv_1.filter.cutoff`) in a
pill over the label (`ParamIds`, `app/widgets/DevicePanel.cpp:146`); its header already names its id.

**Copied, by the host.** Copy Address / Copy Value / Copy as Formula on a device parameter row and on the
dock's faders, pans, sends and master (DR-MIX-16; a choice row has no "as Formula" — no formula reads a
choice); Copy ID on a clip (`app/widgets/Timeline.cpp:1207`), a strip's and a mixer tab's menu in the dock; Copy ID
and Copy as Formula (`=au_1`) on an automation row (`app/widgets/TimelineAuto.cpp:483`). The clipboard is the
HOST's (the core has none, R-SVC-4): `App::onCopy`, which `linux_main.cpp:391` gives GTK's CLIPBOARD and
PRIMARY selections and the test rig records (`Rig::copied`). A copy says so: `App::copy` (`app/App.cpp:270`)
toasts "Copied dv_1.filter.cutoff", outlined in the accent (a refusal's toast stays red). When hosting lands,
a third-party VST3's parameter list (R-VST-6) builds a `ParamTarget` per row and offers this same menu.

Measured. L2 (`test_the_machine_settings_folders_devices_and_recents`): `settings set showIds=on` reaches the
model and the file and a second process reads it; `showIds=yes` is refused. UI `test_ids_shown_and_copied`
(`app/tests/ui/uiTests.cpp:2358`): View › Show IDs sends `settings set showIds=on`, the ids' amount caught between 0
and 1, then 1, the item reads Hide IDs; a shell's `showIds=off` fades them out the same way; a parameter row
copies `dv_1.filter.cutoff`, `900 Hz`, `=dv_1.filter.cutoff` (toasted); a choice row offers no "as Formula";
a clip copies `ac_1`, an automation `=au_1` and `au_1`. Mutant: the ids SET instead of eased → the mid-tween
assertion fails. Shots `show-ids` (lanes, clips, an automation, the dock and a device window's rows) and
`show-ids-mid` (mid-fade).

### DR-AUTO-6 Bezier automation (R-AUTO-10)
ONE curve model in the suite (law 16): a curve IS Interstellar's keyframe list, and Interstellar's
header-only evaluator (`apps/interstellar/model/Anim.h` — After Effects' key, each side linear | bezier |
hold with a speed and an influence) is INCLUDED in place by the engine (`engine/Expr.h:23`), not ported:
it allocates nothing (a fixed 48-step bisection on the Bézier's time component, doubles only), so the
audio thread runs it every 64 samples as it runs the formulas. A `#point` (`AutoPoint`,
`model/Project.h:253`) keeps `shape=` and gains Interstellar's spelling for a bezier point's handles —
`speedIn inflIn speedOut inflOut`, speeds in the automation's unit PER BEAT (the `.slp`'s timebase),
influences in % (0 < x ≤ 100), read always (`model/Project.cpp:513`), written only for a bezier point
(`:1107`), so a file of linear / hold / smooth points reads and writes byte for byte as before.
`engine::curveKeys` (`engine/Expr.h:140`) is the ONE mapping of a point onto a key's two sides, used by
the compiler and the timeline's drawing alike: linear → both sides linear; hold → its out side holds;
smooth → After Effects' Ease on the segment after it (speed 0, influence ⅓ on both ends — a Bézier
whose time is exactly linear, so its value is exactly R-AUTO-4's smoothstep, to 1e-9 measured) unless
the next point is bezier, whose own handle wins on its side; bezier → both sides bezier with its
handles. `compileCurve` (`core/Compile.cpp:654`) converts beats to samples (speeds ÷ samples per beat)
and ranges the curve: `Curve::valueAt` (`engine/Expr.h:198`) clamps to the automation's min/max, so a
handle's overshoot stops where the drawing (which clamps to the row) stops. Grammar: `auto point shape
<au> --at <b> [--shape bezier] [--speed-in <v>] [--influence-in <%>] [--speed-out <v>] [--influence-out
<%>]` — flags not given are kept, so a bezier point takes one handle's numbers alone; refused, naming
it, are a handle flag on a point that is not (or is not becoming) bezier, an influence outside
0 < x ≤ 100 %, a speed that is not a number, no flag at all (`handleFlags`,
`core/service/ServiceAuto.cpp:33`; `:253`). `auto point add --shape bezier` starts flat (speed 0,
influence 33.333 %, Interstellar's default key). In the timeline (`app/widgets/TimelineAuto.cpp`) —
cosmo's `CurvePanel` gesture over Interstellar's numbers: a handle's end is its influence of the
segment on its side along its speed (`handleEnd`, `:66`); Alt-drag a point pulls out symmetric handles
(both sides the pointer's slope, both reaching the pointer's distance in beats), a handle dragged moves
its own side and MIRRORS the opposite, Alt-drag a handle moves only its own (`dragHandle`, `:242`); a
handle is clamped between its point and the neighbour. While held it is drawn under the pointer
exactly (`drawnPoints`, `:193`; `autoHandleAt`, `:214`); the release sends ONE `auto point shape …
--shape bezier --speed-in … --influence-in … --speed-out … --influence-out …` with the numbers it
draws parsed back from that text, so the model returns exactly what is drawn and nothing eases after
(`:508`). Measured (L2, `solaris_service`): a bezier gain −24 → 0 dB over 8 beats with 80 % / 70 %
influences renders, at beats 1…7, the level `eval` prints (= the cubic solved independently by Newton,
to 1e-6 dB) within 0.05 dB (worst 0.019 dB); the engine test checks linear and hold bit for bit, smooth
against smoothstep, bezier against the cubic to 1e-9 and no allocation.

### DR-AUTO-7 The automation's window (R-AUTO-11)
A double-click on an automation row's HEADER (`app/widgets/TimelineAuto.cpp:450`), or on its curve away
from a point (`:539` — the first click's pending point is dropped, so the gesture adds nothing),
calls `onOpenAutomation` (`app/widgets/ProjectScreen.cpp:66`) → `WindowLayer::openAutomation`
(`app/widgets/FloatWindow.cpp:174`): an `auto:<au>` FloatWindow (fading in and out as every window does)
around an `AutomationPanel`, titled "Automation — <name>" and closed when anyone deletes the automation
(`WindowLayer::bind`, `app/widgets/FloatWindow.cpp:257`). Every line is the model's (`AutomationPanel::bind`,
`app/widgets/AutomationPanel.cpp:74`): name, id, range in its unit, the address it was made from, its length, its rest and its clips (R-AUTO-14), its
points (beat, value, shape — a bezier point's speeds and influences), every formula that reads it
(`usedBy`, each with its binding's formula from `bindings[]`), and its value at the playhead —
`automations[].now`, which the service computes with the engine's own compiled curve
(`compileCurve`) at `transport.position` in `refreshModel` (`core/service/ServiceModel.cpp:469`,
`refreshNow`, `:473`) and again in `pump` while playing (`core/service/ServiceTransport.cpp:304`); it is
left out of `state print --stable`, as the position is. The rows are an `AnimatedRows` keyed by what
they say — a point added slides the rows below and fades in, an edit or a rename cross-fades its row;
the value at the playhead follows the transport continuously while playing and eases to a seek or an
edit (`app/widgets/AutomationPanel.cpp:183`). Rename is cosmo's field over the header's button and ONE
`set <au>.name=…` (an address that already existed, R-AUTO-4; `app/widgets/AutomationPanel.cpp:243`). A double-click ON a point still deletes it
(R-AUTO-6). Measured (L2): `now` equals `eval` at the sought beat and the cubic while playing; (UI) the
window caught mid-fade, the facts present, `now` and a new row caught mid-tween, the rename one line.

### DR-AUTO-8 An automation loops; a pump in one line (R-AUTO-12, R-AUTO-13) — ❌ WITHDRAWN (2026-10-19, R-AUTO-14)
Gone: `<au>.loop/.at` (refused, naming what replaced them — `core/service/SolarisService.cpp:1069`), `auto create
--pump` (refused as any unknown flag), ParamMenu › Create Pump, the row's span and its grips (`Timeline::spans()` is
false, `app/widgets/TimelineAuto.cpp:32`, until O2 draws clips), the window's Loop combo (now Length). What remains of
it: a song saved with a loop is ADOPTED when read — an automation as long as its cycle and a clip over its span, resting
where a cycle ends (`adoptAutomationClips`, `model/Project.cpp:586`) — so it plays as it did. DR-AUTO-9 replaces it.

### DR-AUTO-9 Automation clips (R-AUTO-14; docs/discussion.md Round 23) — part O1: the model, the engine, the service; part O2: the window
**The song.** An automation's points are in its OWN beats, 0 … `length` (beats > 0), and it has a `rest` and, a clip's
own curve, `of` (`struct Automation`, `model/Project.h:264`): `#aauto … [of=] length= rest=` (`model/Project.cpp:1124`).
A clip places it: `Clip::automation` (`model/Project.h:226`, `isAuto()` `:244`), `#aclip id= auto= lane= at= [length=]`
and nothing else — it sounds through nothing (`model/Project.cpp:1085`); an automation's track is `Lane::automation`
(`model/Project.h:122`), `#alane … auto=`, named by its automation (`laneTitle`, `model/Project.cpp:52`). Checked
(`:1421`, `:1437`, `:1505`): a track is a first's, never an instrument's too; an automation clip sits on a plain lane or
its automation's track — never an instrument's, never another automation's — and no audio or note clip on an
automation's track; a copy is of a first (no chains); length > 0; rest in range. The header says `autoclips = on`; a
song without it is ADOPTED in the read (`if (!sawAutoClips)`, `model/Project.cpp:919`; `adoptAutomationClips`,
`:586`): its points were song beats, so each automation gets a track and ONE clip at 0 whose length follows it, its
length the last point's beat in whole bars, its rest its first value (clamped into range) — it plays as before; an
R-AUTO-12 loop becomes length = its cycle and a clip over its span (to the song's end, as the model can tell it, when
0), resting where a cycle ends (a line, or a hold, between the points around it); a note says each. Every reader — a
render of a version, a merge — sees one meaning. `placeLanesUnder` (`:1724`, shared with `clip separate`) and
`automationRoot` (`:1741`) are the model's. **The engine** (`engine/Expr.h`): a compiled curve is its keys (the
automation's shape), `copies` (its copies' shapes) and `spans` — `CurveSpan{from, to, cycle, shape}` (`:179`), in beats
× samples-per-beat as every curve is; `placed` curves read (`valueAt`, `:198`) the span that BEGAN LAST at or before the
position (a binary search — no allocation; where two overlap the later cuts the earlier), at `(s − from) mod cycle`
inside it, where it stopped after it (a whole number of cycles: the cycle's end, `:216`), and `rest` before the first
and with none. **The compile** (`core/Compile.cpp`): a copy compiles into its first (`:602`, formulas read the first);
`shapeKeys` (`:686`) — the points up to the length (past it: silent); `compileCurve` (`:707`) — a span per clip of the
family, by where it begins; a clip's length 0 follows its automation's (`clipLengthBeats`, `:114`). A formula naming a
copy is refused, naming its first (`core/Bindings.cpp:127`). **The service.** `auto create` (`core/service/ServiceAuto.cpp:146`)
— as long as the song in whole bars, resting at the property's value, its track right under the property's strip's
track (`:166`) else last, and ONE clip at 0 that follows its length; `made:` names the lane and the clip. `auto add
--length --rest` (`:198`) — no clip. `auto delete` (`:232`) — its copies, their clips and its tracks with it. A point past
the end grows the length to the next whole beat (`lengthenToHold`, `:84`), so scripts that add points in song beats
(the EDM and canon demos — both pass unchanged) land where they did. `set <au>.length=` (`core/service/SolarisService.cpp:1042`;
a point past it kept and warned of), `.rest=` (`:1056`); `.loop`/`.at` refused (`:1069`); an automation clip's fields are
name, at and length (`:1224`); a lane that is an automation's track is not made an instrument's (`:1292`). `clip add
--auto <au>` (`core/service/ServiceEdit.cpp:678`) — on its track (made if none) or the lane given; `laneTakes` (`:647`)
decides where an automation clip may go, and that nothing else goes on its track, for add, move and duplicate; `clip
move` of one (`:851`) — in time and lane, never `--strip`; `clip unique` (`:943`) — a copy `of` its first, named
"<name> 2", the clip now playing it. Split, slice, consolidate, separate, to-midi and chords refuse it, naming it.
Time edits move its clips as clips and never its points (`core/service/ServiceArrange.cpp:100`). `ls` and `show` say
its clips, length, rest and copy. **The model:** `clips[].kind` `automation` and `clips[].automation`
(`core/service/ServiceModel.cpp:330`), `clips[].linked` counting its automation's clips, `lanes[].automation`,
`automations[].length|loop` (the cycle) `|rest|of|clips`, `at` 0 (`:508`). **The window (O1):** ParamMenu offers
Create Automation and no Create Pump (`app/widgets/ParamMenu.cpp:60`); the automation's window says Length, Rest and
Clips and its Length combo is ONE `set <au>.length=` (`app/widgets/AutomationPanel.cpp:45`, `:101`); O2 draws the clips. L2 `test_automation_clips_place_and_repeat_an_automation`
(`core/tests/serviceTests.cpp:1234`: a one-beat dip repeated by a clip 8 beats long — eval every beat the same, a
render's level at eight beats eval's within 0.15 dB, the dip > 15 dB, nothing after the clip; a linked copy heard where
it is put, one edit moving both; the rest before the first clip; Make unique's curve where its clip plays, still
`ch_2.gain`'s; time edits; ten refusals; the `.slp` a fixed point; delete taking copy, clips and track),
`test_a_song_from_before_automation_clips_plays_as_it_did` (`:1361`); model `test_automation_clips_in_the_slp`
(`model/tests/modelTests.cpp:191`); engine `test_a_curve_is_placed_by_its_clips` (`engine/tests/engineTests.cpp:523`:
rest, cycle, where it stopped, a copy's shape, clamped, nothing allocated); UI
`test_an_automation_is_made_with_its_clip_and_sized_in_its_window` (`app/tests/ui/uiTests.cpp:1707`); shot
`automation-length` (`app/tests/shots/renderShots.cpp:993`). Mutants: no rest before the first clip, no hold after one,
no repeat, copies unheard, no clip made, a loop adopted at 0, a copy readable by a formula, the rest not the property's,
Create Pump back, the combo sending `.loop` — each red; the overlap cut at compile survived as an EQUIVALENT mutant (the
engine already reads the span that began last) and was deleted as dead code.

**Part O2 — the window.** The AUTOMATION section is gone: an automation's track is a lane row (`Row::automation`,
`app/widgets/Timeline.cpp:171`), its header the accent stripe, its name and what reads it; its clips wear the accent
(`kAutoHue`, `app/widgets/Timeline.h:110`) and the automation's name — a copy's its own — with "linked ×N" kept clear
of it (`app/widgets/Timeline.cpp:1437`). A clip draws its automation's curve inside its own box, so the copy in hand
during a Shift-drag shows it too (`paintAutoClip`, `app/widgets/TimelineAuto.cpp:385`, from `paintClip`,
`app/widgets/Timeline.cpp:1455`): pass after pass of the automation's length — the first bright with its points and
handles, each repeat fainter with a seam at its start (when 6 px or wider), the last cut at the clip's end; the curve
sampled every 2 px and at every point by the engine's keys (`paintCurve`, `app/widgets/TimelineAuto.cpp:320`). Its frame (`autoFrame`, `:123`) is
the clip as DRAWN (eased) under its 15-px name strip; `autoClipAt` (`:146`) takes a press in that area only — not the
name strip (the clip's: moved, Shift-dragged to a copy, R-CLIP-9) and not its last 6.5 px (its end: resized, R-CLIP-7,
`set <ac>.length=`). The points are the first pass's — the one drawn with them, the one edited (`:228`, `:308`); a click
in a repeat adds the point at its place in the cycle (`passAt`, `:140`; `:559`). A double-click on the clip (its name)
or on its track's header opens the automation's window. The clip's menu (`app/widgets/Timeline.cpp:1340`): Make unique
(while it is linked), Length ▸ (1/4, 1/2, 1, 2 beats, 1, 2, 4 bars — `set <au>.length=`, the current marked), Open
automation, Duplicate, Copy ID, Delete; its track's header menu (`:1280`): Open automation, Copy as Formula, Delete
Automation, the lane's group items, and Copy ID — the automation's, the id its header shows (`:1317`). Its track is an
accessible list item valued "automation au_n" (`:339`). Ctrl+C / Ctrl+V and Shift-drag are the clips' own (`clip
duplicate`). UI `test_automation_clips_draw_and_edit_curves` (`app/tests/ui/uiTests.cpp:2153`: grows in, under its
strip's track; a click adds, a drag is drawn under the pointer and is one move, a shell's edit eases, a point's menu,
a double-click deletes; Length ▸ one line; a click in the third pass lands in the cycle; Duplicate linked, Make unique;
its track's Delete Automation), `test_automation_clips_copy_like_midi_clips` (`:2282`: Shift-drag by its name one `clip
duplicate --at`; Ctrl+C / Ctrl+V at the time bar; its end the pointer's then one line; a shell's edit eases two clips
together, caught mid-way); the bezier, IDs and en-XA tests aimed at the clip and its track. Shots `automation-clips`,
`automation-clips-easing` (mid-ease) (`app/tests/shots/renderShots.cpp:889`, `:921`), `automation-bezier`,
`automation-window`, `automation-length`, looked at. Mutants: a repeat's click not folded into the cycle, a shell's edit
snapping, no Delete Automation on its track, a dragged point drawn where it was, the clip's end taken by the curve,
Copy ID the lane's — each red.
### DR-SVC-8 Composition is agent-sized (R-SVC-8)
Built from the audit of an agent's 16-bar song (110 commands, 64 single `note add`s, every id guessed,
edits lost between calls, comment lines reprinting ids). The guide is `docs/AGENTS.md`.
- **A session that lasts.** `solaris-cc shell [--song <f>]` (`cli/main.cpp:183`) reads stdin lines into
  ONE service — the open song, its ids and its undo history last the session; `--song` opens the file
  or makes it when it is not there; a refusal is reported and the session goes on (exit 3 at the end).
  Every run ends in `finish` (`:170`): a song left with unsaved edits is named on stderr and the run
  exits **4**, unless `--discard`.
- **Scripts that say where.** A refusal prints `refused: line N: …` — the script's own line number, or
  the chained command's (`run`, `:158`); `--keep-going` runs past refusals and still exits 3 (`:227`).
  `parseCommand` drops a `#` that starts a word outside quotes (`withoutComment`,
  `core/service/Command.cpp:621`) — `F#3` stays a pitch; a comment or blank line clears the output
  (`core/service/SolarisService.cpp:61`), so nothing is printed twice.
- **Everything made is said.** `dispatch` diffs the song's ids around an edit (`:321`): the first line
  stays the command's own id, then `made: strip=… device=… pattern=… lane=… clip=…` lists the rest by
  kind. Each made node is announced by its own `project.changed` — the instrument of a new instrument
  strip (`core/service/ServiceEdit.cpp:257`), an auto pattern (`:615`), every new lane (`:460`).
- **Notes in bulk, by name.** `core/service/Notation.cpp` is the notation, pure: `parsePitchName`
  (`:41`, C4 = 60, `#`/`b`, c-1 = 0), `chordQualities` (`:80`, 21 qualities — maj, m, 5, dim, aug,
  sus2, sus4, 6, m6, 7, maj7, m7, mmaj7, dim7, m7b5, 7sus4, add9, madd9, 9, maj9, m9), `chordPitches`
  (`:108`, `--octave`, `--inversion` raising the lowest notes), `parseNoteToken` (`:136`,
  `<pitch>@<beat>[:<length>[:<vel>]]`), `parseSteps` (`:168`). `SolarisService::pitchOf`
  (`core/service/ServiceCompose.cpp:118`) resolves a pitch anywhere — `--pitch`, `--to-pitch`, a note
  token, a step row — as a number, a name, or a pad of the kit that plays the pattern (the registry's
  note names, case/space/hyphen-blind; every kit's when no clip plays it yet), refusing an unknown one
  with the pads listed. `notes add` (`:191`) parses every token before touching the pattern and refuses
  the first bad one by number and text — ONE edit, ONE undo step. `note add --chord` (`ServiceEdit.cpp:791`)
  adds a chord as one edit and prints its notes. `pattern steps` (`ServiceCompose.cpp:190`): x / X (127) /
  `.` per `--step` from `--at`, replacing that pitch in the span. `pattern duplicate | clear [--pitch] |
  delete` (refused while a clip plays it, naming the clips, `:248`) `| transpose --semi` (refused whole if a
  note would leave 0–127, `:259`). A note written past its pattern's end grows the pattern (DR-ROLL-9 — it used
  to be warned, `pastEndWarning`, `:122`, which the audit still uses); one left outside by an end set back by hand
  is audited (`core/service/ServiceModel.cpp:618`, with empty and unused patterns).
- **Arrangement.** `clip duplicate --count N` makes N copies end to end in one edit
  (`ServiceEdit.cpp:725`). A `clip add` with no `--lane` goes on the lane of its strip's newest clip; a new
  lane only for a strip with none; `--lane new` asks for one (`laneFor`, `:550`) — the GUI's drop below the
  last lane says `--lane new` (`app/widgets/ProjectScreen.cpp:129`), so R-BROWSE-3 holds.
- **Reading back** (read-only: in `mutates()`, `SolarisService.cpp:134` — no undo step, still saved):
  `ls` (`lsText`, `ServiceCompose.cpp:329`), `show <id>` (`showText`, `:414` — a device's non-default
  parameters with unit and default, a formula where one drives it), `pattern print` (`:294`, the notation
  with the pitch's number and name after `#`, a kit's pads by name), `state print --json --compact`
  (`compactJson`, `core/service/AppModelCodec.cpp:517`; `Json::dumpCompact`, `core/service/Json.cpp:118`):
  the song only, one line, a device's non-default parameters as `name=value`, notes in the notation.
- **Honest values.** A device parameter outside its registry range is refused naming the range and unit
  (`SolarisService.cpp:777`), as is an automation point (`inAutoRange`, `core/service/ServiceAuto.cpp:69` — a value within printing's
  rounding of a bound is that bound, so a dragged point at the top edge still lands).
- The API document gains the Notation section and each kit's pads (`core/service/ApiDoc.cpp:151`, `:87`).

Guarded by `solaris_service` (`core/tests/serviceTests.cpp:1906` … `:3664`: the notation tables; comment
lines print nothing; every made id printed and announced; notes add by name, a bad token, an unknown pad,
chords, one undo step; steps and pattern edits; the lane rule and `--count`; reading back, out-of-range
refusals) and `solaris_cli_session` (`tests/cli_session.cmake`: line numbers, `--keep-going`, exit 4,
`--discard`, the shell keeping undo). Mutants seen red, then restored: a comment line keeping the last
output (both suites), a `notes add` pushing a step per note, a device value clamped again, an auto pattern
not announced, every clip on a new lane, unsaved edits exiting 0, `pattern delete` while played. End to
end (2026-10-09): an 8-bar "Ode to Joy" (drums by steps, bass by `notes add`, chords by `--chord`, the tune,
a reverb bus) in 32 commands where the audit's 16 bars took 110; rendered and measured — RMS −21.8 dBFS,
peak −6.5 dBFS, the kick 18.6 dB or more above the level just before every one of the beats, onsets
500.0 ms apart (120.00 bpm), the lead at E4 329.6, G4 391.8, C4 261.5, D4 293.7 Hz.

### DR-SVC-9 The goal, measured (R-SVC-9)
A whole song made with `solaris-cc` alone: `demo/canon/` — Pachelbel's Canon in D (public domain) as a
128 bpm EDM track, 128 beats. `make_script.py` spells the notes once and writes `song.txt`, the committed
script: 86 commands (`project new`, seven instrument strips by `clip add --instrument`, `pattern steps`,
`notes add`, `note add --chord`, `pattern duplicate`, two buses and their sends, a delay time as a formula
`=60000/bpm*0.75`, two compressors keyed from the kick, a chorus, the pad's cutoff automated on bezier
points, a master limiter), ending in `ls`, `audit`, `project save` and `render --stems`. `measure.py`
measures the render.

Guarded by `solaris_demo_canon` (`tests/demo_canon.cmake`, registered in `cli/CMakeLists.txt:22`): the
committed script must equal what `make_script.py` writes, the run must exit 0 and write the song, its mix
and four stems, and `measure.py` must pass — skipped, and said, where there is no python3 with numpy.
Mutants seen red, then restored: a stray line in `song.txt` (stale), the kick a sixteenth late (200
samples off). Measured (2026-10-09): 62.30 s, peak −0.80 dBFS, RMS −16.8 dBFS; intro −28.2, verse −15.4,
build −17.6, drop −14.4 dBFS; the kick on all 64 beats within 1 sample; the bass 29.5 dB down just after
each kick; line A at 743 662 586 552 495 442 493 557 Hz (worst 8.4 cents); the pad's centroid 1157 →
2300 Hz across the intro. Found on the way: D-2 (a renamed strip's lane keeps the old name) and D-3 (a
kit's pad and parameter names differ).

### DR-SVC-10 The second song, made in the window, measured against its score (R-SVC-10)
`demo/prelude` (its README): J. S. Bach's Prelude in C, BWV 846 (public domain), on the piano (DR-INST-5). The
score is a table (`score.py` → `score.txt`, 549 notes, 140 beats); the song is made by `makePrelude.cpp`
(`solaris_demo_prelude`) through the real `App` and service by gestures alone — clicks, drags, keys and the
wheel at the geometry the app draws (the test rig, minus GTK), each counted — with the bars it builds checked
against the score as it goes. `measure.py` diffs the song's notes (`pattern print`) against the score and its
render against the score's own song (`score.py --reference`, made straight from the table with `solaris-cc`:
the same piano, reverb and tempo — render being pure, a bar with the score's notes is the same samples).
Measured: 549 of 549 notes at pitch, start and length, nothing extra; 35 of 35 bars ≤ −60 dB from the score's
render; 128.2 s; peak −2.8 dBFS; the pulse a sixteenth at 66 bpm. The rig's WAV writer writes nothing, so the
window's Render… is dispatched and the file is rendered from the saved song by `solaris-cc`. ctest
`prelude_made_in_the_window` (label `long`, ~7 min in a Debug build; outside the everyday `-R solaris`).

### DR-SVC-11 The UX found by making it (R-SVC-11)
`docs/ux-audit.md`: attempt 1 (the app as it was) — two bars cost 50 gestures, six findings (U-1…U-6); attempt 2
(with the fixes) — the whole song in 376 gestures, 0.68 a note against 1.6, and four more found and fixed on the
way (U-7…U-10). Closed: U-1 the tempo (DR-UI-13), U-2 the selection (DR-ROLL-8), U-3 the pattern growing
(DR-ROLL-9), U-4 the length chip (DR-ROLL-10), U-5 windows resized (DR-WIN-5), U-7 the sideways wheel (D-5),
U-8 the short drag (D-6), U-9 a short note's body (D-7), U-10 Ctrl selecting (R-ROLL-8 amended). Deferred with
the reason: U-6 (a unit the registry does not give) and what a person would still want (a chord stamp or an
arpeggiator, velocity by hand, a keyboard — B13).

### DR-SVC-12 An EDM track, original, measured as a master (R-SVC-12)
`demo/edm` (its README): *Perihelion*, written for this repo — i–VI–III–VII in A minor, its own hook, bass lines,
drums and form — 126 bpm, 72 bars, made with `solaris-cc` alone from `make_script.py`'s `song.txt` (~215 lines).
Its sounds are the library's, three of them gained for it: stereo unison (REQ-synth2-6), crash and ride
(REQ-drum-6), and a volume that moves a sounding note (D-8) so the riser swells. `measure.py` judges the master:
BS.1770-4 loudness (K-weighted, gated) −10.3 LUFS integrated; true peak −1.03 dBTP (4× oversampled); the drops
(−8.8 LUFS short-term) over the breakdown (−13.6); each build +3.1 LU and +15 dB brighter at 5–14 kHz, landing
under its drop; the low end mono (correlation 1.000 under 120 Hz) and the highs wide (side/mid −1.7 dB over 1 kHz);
the chords pumping 10.3 dB on the kick; the kick on all 128 drop beats within 0.19 ms; 98 % of the pitched peaks in
A minor; the sub to the air. ctest `edm_track_measured` (label `long`, ~1 min).

### DR-SVC-13 A trap / EDM track, its drop measured (R-SVC-13)
`demo/trap` (its README): *Magnetar*, written for this repo — i–VI–iv–V in F minor, its own hook, 808 line, drums and
form — 150 bpm in half time, 80 bars, made with `solaris-cc` alone from `make_script.py`'s `song.txt` (~270 lines). The
808 (R-INST-11) re-attacks on the kicks and glides where a note is held over the next one's start; the snare and clap
sit on the third beat of each bar; the hats roll in thirty-seconds and triplets; each build's parts end a beat early
(the pad faded by automation) so the drop lands out of near-silence. `measure.py` judges it: −9.7 LUFS integrated, −1.20
dBTP (the limiter's true-peak mode); the drops −7.4 LUFS over a −12.0 breakdown; the builds rising, brightening and
landing; the gap 27.5 / 30.3 dB under each drop's first bar; the snare on all 32 drop bars' third beat within 0.12 ms,
none on the backbeat; the 808 on each bar's root within 2 cents and, read cycle by cycle under 180 Hz, passing through
the pitches between its notes at all 28 slides; 92 thirty-second and 43 triplet gaps among the hats' onsets; the low
end mono, the highs wide; the supersaw pumping 16 dB; 88 % of the pitched peaks in F minor; the 808's octave the
loudest, the air −24 dB. ctest `trap_track_measured` (label `long`, `tests/demo_trap.cmake`: the script must be what
the generator writes, run to the end, and measure). Mutants on the music, each seen red: no gap, no held notes (no
glides). Making it found D-26 (the audit called a wavetable silent).

### DR-SVC-14 A progressive house track, its melody measured (R-SVC-14)
`demo/progressive` (its README): *Afterglow*, written for this repo — IV–I–V–vi in D major, two bars a chord, its own
melody, bass line, drums and form — 126 bpm, 104 bars, made with `solaris-cc` alone from `make_script.py`'s `song.txt`
(~300 lines). The melody (sixteen bars; the first phrase asks, the second answers home) is heard first on a bare piano
in the breakdown (its own track, so its pitch can be read), swells and opens on a legato lead through each build (volume
and cutoff automated), carries the drop, and returns in the last drop a whole tone up (every pitched part's pattern
lifted, E major) doubled an octave higher. `measure.py` judges it: −10.7 LUFS integrated, −1.21 dBTP; the drops −8.9 /
−8.5 LUFS over the breakdown (−15.6) and the verse (−11.2); each build −16 → −10 LUFS and 21 dB brighter, landing; the kick
on all 128 drop beats within 0.17 ms; the chords pumping 12.9 dB; the field; the melody as written (fifteen semitones,
64 % steps, its climax A5 in the second half of each phrase, home on D) and as HEARD — a YIN pitch read 0.15 beat into
each written note: the lead on all 54 of the first drop's within 50 cents (median 3), the piano on all 28 of the
breakdown's (median 0), the lead on all 54 of the last drop's a whole tone higher; the drops in D major and E major (97 %
of their pitched peaks over 250 Hz — below it a bass saw's seventh harmonic reads as a C natural). ctest
`progressive_track_measured` (label `long`, `tests/demo_progressive.cmake`). Mutants on the music, each seen red: no
lift, the lead a semitone off its melody.

### DR-SVC-15 A melodic trap / future bass track in the Trap Nation style, measured (R-SVC-15)
`demo/futurebass` (its README): *Halation*, written for this repo — VI–III–VII–i in C♯ minor, a bar a chord, its own
vocal-chop melody and syllables, 808 line, drums and form — 140 bpm half time, 80 bars, made with `solaris-cc` alone
from `make_script.py`'s `song.txt` (~410 lines). The chords are chopped on the song's grid by six shapers on their strip
(DR-FX-18): a `rate` is a choice and a choice is not automatable, so each rate is its own shaper switched on by a held
`depth` automation — a gate a quarter, an eighth, a sixteenth, a thirty-second through each build, a triplet-eighth
wobble in the drops, a sixteenth in the second drop's second half. The chops are one legato voice through the vowel
filter (DR-FX-19), the melody's syllables one 32-beat `auto add` automation bound to `vowel` and placed by `clip add
--auto` under every clip of the melody. A piece of the first drop is rendered from beat 97.5 as well. `measure.py`
judges it, 30 checks: −9.6 LUFS integrated, −1.21 dBTP; the drops −7.7 LUFS over the breakdown (−12.4) and the verse
(−11.2); each build −13.8 → −9.1 LUFS, 12 dB brighter, landing; the gap 18.6 / 19.4 dB; the snare on beat three of all
32 drop bars within 0.1 ms; the 808 on all 32 roots within 1 cent and through all 28 glides; the hats' triplet and
thirty-second rolls; THE CHOP — the chords' 2 ms loudness strongest at the shaper's rate in all 12 stretches, and folded
on the song's grid the shaper's own curve (SH3 through its smoothing SH4) at r ≥ 0.99, the piece from beat 97.5 too
(r = 1.00 on the song's grid, −0.91 counted from its own first sample); THE VOICE — an "ah" note's 550–1250 Hz band 9.6 dB
over its 1900–3200 Hz band, an "ee" note's 2.6 under, 104 of 108 drop notes on their written side, and at each pitch sung
both ways (C♯5 D♯5 F♯5 G♯5) the "ah" 6.3–29 dB over the "ee" in every drop pass; 76 of 78 held notes within 50 cents
(YIN); the field; the pump (14 dB); 95 % of the drops' pitched peaks over 250 Hz in C♯ minor. ctest
`futurebass_track_measured` (label `long`, `tests/demo_futurebass.cmake`). Mutants on the music, each seen red: the chop
half a cycle off the grid (the fold 0 of 12), the vowel filter bypassed (both vowel checks), the drops' wobble an eighth
instead of a triplet eighth (the rate 9 of 12), the 808 with no glide (16 of 28).

### DR-SVC-16 An electro / future house track in the NCS style, measured (R-SVC-16)
`demo/futurehouse` (its README): *Starward*, written for this repo — vi–IV–I–V in G major, a bar a chord, its own hook,
bass line, drums and form — 128 bpm, 80 bars, made with `solaris-cc` alone from `make_script.py`'s `song.txt` (~260
lines). The bass is the FM synth (REQ-fm: algorithm 5, a sine and its octave-down sine each struck by a fast-decaying
modulator) on the off-beats between the kicks, jumping octaves; the chords and the pad pump on a shaper each (`rate`
1/4, `shape` pump, DR-FX-18) — no compressor and no sidechain key anywhere in the song; the hook is eight bars in one
rhythm, on a legato lead in the drops and a pluck elsewhere. `measure.py` judges it, 26 checks: −8.7 LUFS integrated,
−1.17 dBTP; the drops −7.2 / −7.0 LUFS over the breakdown (−11.2) and the verse (−10.4); each build rising 3.6–5.3 LU,
17–23 dB brighter, landing; the gap 18.8 / 16.9 dB; the kick on all 128 drop beats within 0.12 ms; the clap on two and
four of all 32 drop bars and never on one or three; the bass's 2 ms loudness folded on the beat loudest 0.77 of the way
through and 38.5 dB quieter in the kick's eighth; the chords' folded on the song's beat matching the shaper's pump (SH2,
SH3 through SH4) at r = 1.00, an 11 dB swing; the hook as written (one rhythm in 8 of 8 bars, its motif returning) and as
heard (the lead on 224 of 224 drop notes within 50 cents, YIN); the field; 97 % of the drops' pitched peaks over 250 Hz
in G major; the air 12 dB under the loudest octave. ctest `futurehouse_track_measured` (label `long`,
`tests/demo_futurehouse.cmake`). Mutants on the music, each seen red: no pump (r < 0, a 0.9 dB swing), the pump half a
beat off the grid (r −0.37), the bass on the beat (loudest at 0.05), the lead a semitone off (0 of 224).

### DR-SVC-17 A future bass track that tells a story, measured (R-SVC-17)
`demo/storytrack` (its README): *Porch Light*, written for this repo — a story in eight scenes (a daughter packing up her
late mother's house; the porch light her mother always left on), each scene told by a device of harmony or melody —
150 bpm half time, 96 bars, E♭ major then G, made with `solaris-cc` alone from `make_script.py`'s `song.txt` (~370
lines). The leitmotif is ONE score (`THEME`); its transformations are computed from it — `augment` (onsets and lengths
×2), `invert` (a diatonic mirror about B♭4: scale steps negated), the transposition +4 with bars 5 and 7 rewritten for
their diatonic chords — so the measure checks the score the song was made from. The guitar is DR-INST-12 (San Holo's
clean electric: chorused, plucked); the wonk is the pattern's bend lane (`expr set … --lane bend`, a held −2 st sag at
each phrase's end); the pump a shaper's (DR-FX-18) switched by its depth; her voice and the growl the vowel filter
(DR-FX-19) — the growl's syllables a 4-beat `auto add` automation placed by `clip add --auto` at each phrase's end; the
photograph's right hand a piano strip of its own, so its pitch can be read. `measure.py` judges it, 40 checks: −9.4
LUFS, −1.22 dBTP; the arc (the drops 4 LU over the breakdown and 3 over the verse; the builds; the gap; the second drop's
air 1.3 dB over the first's; half time); the theme as written (motif, modified sequence, appoggiatura, ♭6→5 sigh, one
climax over ♭VI, 4–3 suspension, half cadence, augmentation, inversion, transposition) and as heard (YIN — the guitar 53
of 54 and 54 of 54, the lead 54 of 54, the piano 14 of 14, the inversion 14 of 14); the harmony as heard (peak-picked
chroma over 250 Hz: the borrowed C♭ / G♭ 35 % / 53 % of their bars and absent elsewhere; the second drop ≤ 5 % outside G
major in every bar — its saws' overtones; the keys E♭ and G, a major third apart; the deceptive root C♭ at 61.8 Hz; the
minor plagal E♭ 31 % under the iv, 0.7 % under the final G); the wonk (−2.00 st at all eight sags, a log-frequency
spectrum's shift); the growl (its formant balance cycling at 5.03 Hz); the field. ctest `story_track_measured` (label
`long`, `tests/demo_storytrack.cmake`). Mutants on the music, each seen red: no borrowed chords, no deceptive cadence,
the wonk flat, the growl not talking, no inversion, the second drop a whole tone up.

### DR-SVC-5 The control channel and attach (R-SVC-5, R-SVC-6)
**The window.** `solaris --control <socket> [song.slp]` (`linux_main.cpp:480`) builds a `ControlServer`
on the window's own service after any song opened and polls it from its own 16 ms GLib timeout
(`onControl`, `:255`; `:495`) — not the frame clock, so a minimised window is still drivable. The
wire is cosmo's `ControlChannel` **compiled in place** (`apps/cosmo/ControlChannel.cpp:382` `poll`,
`:402` `broadcast`; target `solaris_control`, `host/CMakeLists.txt:29`): a non-blocking Unix line
socket that moves lines and never parses them, replaces a stale socket node, refuses any other file
at the path and removes its node on close. Its only need from cosmo's host, the LOGI/LOGW macros, is
met by `cosmo_v2::log::writef` to stderr (`host/ControlLog.cpp:20`) — linking cosmo's Log.cpp would
bring its ProjectStore and GLib.

**The answer to a line** (`ControlServer::poll`, `host/ControlServer.cpp:51`; the prefixes,
`host/ControlServer.h:77`): every service event goes out as the `formatEvent` line `--watch` prints,
to every client, as it is emitted (the sink, `:33`); each line runs through THE door,
`dispatchText` (`:85`), and is answered after its events — its output one `[out] <text>` per line
(`:96`), then `[ok] <line>` (`:99`); a refusal is `[refused] <why>` (`:87`) right after its
`[evt] command.rejected`. A blank or comment line is `[ok]` and runs nothing (`:71`). Lines are run in
order, one write may carry many. **`wait` is the loop's** (`:77`): a valid `wait <s>` holds the queue
— later lines wait their turn while the window keeps ticking, `[ok] wait …` comes when the time is
up — instead of sleeping the GTK thread as the service's own `wait` would (`core/service/
ServiceTransport.cpp:131`); one the service would refuse is handed to it and refused at once.

**attach** (`cli/Attach.cpp:55`, hooked at `cli/main.cpp:96` before any service is built — it builds
none): lines from `--script`, chained ` : ` args, or stdin; sends ONE line, waits for its `[ok]`
(matched by echo) or `[refused]`, then the next (`:173`–`:207`), so a script ends when its last answer
arrives, never on a quiet-period guess. It prints what solaris-cc prints where solaris-cc prints it:
output on stdout, event lines on stderr (always — watching is the point), `refused: <why>` and exit 3
on a refusal (`:231`); `--follow` keeps printing events; `--timeout` (300 s). Empty lines are not sent:
the channel delivers none, so none would be answered (`:202`).

**Proof.** `solaris_control` (`tests/faces/controlTests.cpp`, no display needed): the wire for eight
lines — events, outputs, refusals, a comment, the stable dump — equals what a second service given
the same lines directly produces (`:148`); `wait 0.5` returns from `poll` at once, holds the next
line until the clock passes it, and `wait 99999` is the service's refusal, never held (`:192`);
`solaris-cc attach` against the in-process server prints byte for byte what `solaris-cc --watch
--script` prints for a 17-line song — stdout, the event stream, and the saved `.slp` (`:243`).
**The equivalence test (R-SVC-6)** `solaris_equivalence` = `tests/acceptance/run.sh` over the committed
`tests/acceptance/a-song-made-twice.txt` (drums + synth with their notes, a reverb bus and a send,
levels, a formula via `auto create`, undo/redo, `get`, `audit`, `matrix print`, `project save`,
`render`, `state print --json --stable`): run headless, then through a LIVE `solaris --control` window
with `attach`, in the same scratch directory with scratch `SOLARIS_SETTINGS`/`SOLARIS_RECENTS` and no
`transport play`; it asserts the window's event stream and requires the two runs' events, outputs,
stable state, saved song and rendered mix to be identical (`run.sh:114`–`:121`). With no display it
exits 77 and ctest reports it **Skipped** (`tests/CMakeLists.txt:29`), never passed. Ran live on
DISPLAY=:1 (2026-10-09): 28 lines, 31 events, all five artifacts identical, 18 of 18 runs. Mutants: a
channel that drops the event lines fails `solaris_control` (`:175`) and the equivalence test; an
attach that prints no output fails both (`:301`; "no state dump came back from the window").

### DR-SVC-7 The web face (R-SVC-7)
**The adapter** (`cli/NtwbAdapter.cpp`, cosmo's pattern, `arstro.ntwb.implement`) holds no behaviour:
the NTWB call `command {line}` is `dispatchText` and answers `{output, revision}` or fails with the
service's sentence; `wait` is refused — it would sleep the loop and its pings (`:111`); a `notify`
(a fader in flight) is the same call with no reply (`:78`). Every event is relayed under its own
`eventName` as `{line: formatEvent(e), fields}` (`:73`). The state `model` is `modelToJson(m, false)`
— exactly what `state print --json` prints (`:43`) — pushed when the revision moves (≤ 25/s), and the
state `transport` (playing, position, loop, latency, the master's and every strip's peaks, the
audition) when it changed (≤ 20/s), because `pump()` moves those with no revision (`:83`–`:102`).
`model` and `commands` (the grammar table: name, usage, summary) are the other calls (`:127`).
`apiDescription()` (`:152`) is generated from the event table and committed as
`docs/ntwb-api.json` (ctest `solaris_ntwb_api_current`, `cli/CMakeLists.txt:62`); it points to
`docs/API.md` for the grammar instead of copying it.

**`solaris-cc ntwb`** (`cli/NtwbMain.cpp:210`): `api`; `install [--data-dir D] [--web-dir W]` (`:88`)
writes `D/solaris/{ntwb.json, api.json, web/}` (D defaults to `$XDG_DATA_HOME/ntwb/apps`) — the web
UI plus what it borrows, copied not forked: cosmo's `js/core/signal.js` + `dom.js` (`:104`) and cosmo's
typeface (`:110`); the manifest is `single: true` (`:141`) — one engine on the clock device, its pages
sharing the song; `uninstall [--data-dir D]`; `serve` (`:153`) — the service with the window's host
(files, devices, the clock device, the machine's settings and recents), the adapter, and the loop that
owns the clock: `client.poll(8)`, `pump()`, `tick()` (`:198`).

**What the window's host gives it, the adapter gives the page** (added 2026-10-27 for the same-app
port): `browse {path?, kinds?}` → `{path, parent, entries: [{name, path, kind, size?}]}` from the
host's own `listDir` (kinds dir, song, audio, midi, video, preset, sfz, other — the web's stand-in for
the native pickers); `video.frame` / `video.thumb {file, t, w, h}` → a JPEG blob on a stream of the
same name to the calling page only (two decoders, as the native host has; GdkPixbuf's encoder); and
the state **`live`** (`liveJson`, ≤ 25/s, sent when its text changed): every value the window reads
each frame that `pump()` moves with no revision — the transport's meters, tempo, cpu, xruns, swaps,
devices, launched clips' progress, count-in, recording; every strip's peak, rms, hold, clip and cpu;
device readings (the tuner), bindings' live values, all of loudness, the video time, the audition and
the MIDI messages — each at its model path, so the page merges it over `model`. `install` copies the
cosmo files `web/BORROW` lists (globs allowed; a missing one fails the install by name), the
window's words (`lang/*.strings` → `web/lang/`) and writes `web/files.json` (the page's own file
list). `cli/webTokens.cpp` prints every token of `app/Theme.h` plus the cosmo ones the borrowed `ui/*`
read.

**The page, as built 2026-10-27 — the FRAME of the same-app port (R-SVC-7, amended 2026-10-26):**
`js/model/session.js` (state `model`, `transport`, `live`; events; blobs; `browse`, `video.*`) →
`js/vm/app.js` (`vm.model` = the model with `live` merged at its paths; `cmd(line)` with the refusal
said as App::showToast says it; the page's own view state — per-page UI scale, dock height and fold,
browser width, session view, the open float windows, the selection the lanes show, sheets, focus;
registries for accessibility nodes, rolls and the keymap) → `js/vm/words.js` (Strings.cpp + Words.h:
`tr`, stored words that re-say themselves when the language changes, en-XA generated in the page; a
language switch dissolves 180 ms) → `js/shell.js` (App.cpp: the logical-px root under the eased UI
scale, the window's floor 960×600, SCALED DOWN to fit a smaller window — web-only, so a phone sees
the whole app; home | project cross-faded 260 ms; KeyInput's keys; the toast; cursors; the menu,
popup and modal layers; ctx `{toast, menu, confirm, prompt, pick, copy, box, scale, screenPoint,
overlay, slot, open}`) → `js/project/screen.js` (ProjectScreen::layout's arithmetic: the browser's
eased width, the lanes floor 130, the dock's eased height and fold, the Session view's cross-fade, the
browser→lanes drag of `js/vm/dnd.js` sending ONE line per drop as ProjectScreen::place does, the
float-window layer) with one SLOT per native widget: `project/{songbar, browser}`, `timeline/timeline`,
`session/view`, `mixer/dock`, `windows/layer` (contents `device/panel`, `piano/roll`,
`automation/panel`, `mixer/loudness`, `video/panel` by kind), `home/home`, `sheets/{settings, export,
relink, conflicts}`, `history/view`. `js/kit/slot.js` loads a widget module (`export function
mount(host, vm, ctx)`) by dynamic import: one that is absent keeps a quiet placeholder in the native
panel colours, one that fails says so in its box, and the rest of the page keeps working.
`js/project/menus.js` holds the App's actions and the song menus as stored words;
`js/dialogs/picker.js` is the board file browser over `browse`. The widgets themselves are ported
module by module (PROGRESS: the web face is the same app). The first page (song bar, Home, lanes,
mixer, console — `js/views/*`, `js/vm/song.js`) is replaced by this frame.
**The widgets, ported 2026-10-27** (each a module in its slot, from its native file's layout / paint /
gestures with the native's numbers and command lines): `home/home.js` (HomeScreen), `project/songbar.js`
(SongBar with cosmo's MenuStrip and the song menus), `project/browser.js` (Browser: the four tabs, folders
through `browse`, audition, the drag and double-click to the lanes), `timeline/timeline.js` (Timeline's
core: ruler, lanes, clips, zoom, select / `clip move` / `clip duplicate` / `set <clip>.length`, seek,
markers band, loop brace, the drop target `vm.dnd.lanes`), `mixer/dock.js` + `mixer/law.js` (MixerDock:
strips, the fader law and meter scale of FaderLaw.h, pan, sends, M/S/R, master, tabs), `mixer/loudness.js`,
`mixer/parammenu.js`, `device/panel.js` (DevicePanel), `windows/layer.js` (FloatWindow + WindowLayer:
contents by kind with `ctx.window`), `piano/roll.js` (PianoRoll), `session/view.js` (SessionView),
`history/view.js` (cosmo's HistoryView as Solaris opens it), `sheets/{settings,export,relink}.js`.
Not yet: the automation and video windows' contents, the conflicts sheet, and the parts PROGRESS C4c lists.
Checked against the native shots' own models (`mixer-sources` 4.2 % of pixels, `piano-roll` 4.4 %,
`browser-instruments` 4.6 %, the lanes, session view and history line up) and live over the real
`solaris-cc` in a local Arstro Remote: a song built by commands draws its lanes, clips and strips, a
piano-roll window and the settings sheet open, a refusal is said; no console errors.

**Proof.** `solaris_ntwb` (`tests/faces/ntwbAdapterTests.cpp`): the real service, adapter and
`ntwb::Client` over `ntwb::MemoryTransport` with a fake host — calls become commands, a refusal is a
failed result with the service's words and one `command.rejected` event, `wait` and an unknown method
are refused, a notify lands, `model` and the pushed state equal `state print --json` but for the
revision, `commands` lists the whole table, the transport has its key, `live` carries what `pump()`
moves and is sent again only when it changed, every frame validates; `browse` lists a folder with the
host's lister; a `video.frame` is a JPEG to the calling page alone; `ntwb install` into a scratch data dir writes the manifest, the API, the page, the files
`web/BORROW` lists and the fonts, and `uninstall` removes them (`:207`). Mutant: an adapter that relays
no events fails it. The page was looked at in headless Chrome over a fake bridge fed a real `state
print --json` (desktop, Home, the console mid cross-fade) and its intents checked to send the window's
lines. **The first page ran through a real Arstro Remote on 2026-10-26** (an Orange Pi 5 Plus, Arstro Remote 2.1, NTWB 1.1.0): `solaris-cc ntwb install` listed it under Apps, `arstro-remote apps launch solaris` started `ntwb serve`, `apps call solaris command '{"line": "project new …"}'` answered `{output, revision}`, and the page at `/apps/solaris/` drew that song (song bar, lanes, mixer) at 1280×800 and at 390×844.

### DR-MIX-17 Latency compensated (R-MIX-17)
**A device says how late it is** — DSP `Device::latency()` (`src/device/Device.h:125`, REQ-device-8, DSP
`b611e4a`): the samples its output lags its input at its current parameters; the limiter's `EffectDevice`
reports its lookahead (`src/device/DeviceRegistry.cpp:273`, 96 at 2 ms and 48 kHz), every other type 0.

**The engine delays every other path** (classic plugin delay compensation). `Engine::build` calls
`compensate` (`engine/Engine.cpp:288`) once the bindings' values at time zero are in (`:278`), and walks
the graph in processing order (every target is later): a strip's INPUT is aligned to the latest arrival —
its own sound (an instrument's latency), earlier strips' outputs, sends and sidechain keys; its OUTPUT is
that plus its effects' latencies (`stripLatency(i)`, `engine/Engine.h:136`). The master's input is
aligned the same way, its output adds its rack's, and every port and the captured master meet at the
latest of all (`outputLatency()`, `engine/Engine.h:134`). Then each connection — a main output, a send, a
key — carries a `DelayLine` (`engine/Engine.h:58`) of what its target's alignment exceeds it by
(`route`, `engine/Engine.cpp:632`, and `routeKey`, `:624`, through `delayed`, `:360`); a strip's own clips or notes go
through one of their own when something later reaches its input (`:685`, `:837`); a bypassed device
runs a delay of its latency in its place (`:849`), so the timing never jumps with a bypass; a key
reaching a compressor behind a latent device in its OWN rack is delayed by those devices too (`:857`); a
silent strip's delayed connections drain what they hold, in time (`drain`, `:891`); the master is
delayed up to the ports' alignment (`:1051`). Every delay is sized at build — the audio thread
allocates nothing (the counting `operator new` test now plays two limiters, one bypassed live: 0).
Rendering stays byte-identical however time is chopped (128 vs 77).

**The render starts on beat 0; live hears it later.** `render` renders `outputLatency()` more and drops
it from the start of the mixdown and the ports, a stem its strip's own latency
(`core/service/ServiceRender.cpp:224`, `trim`, `:261`). `Player::heard` subtracts it from the position
(`core/Player.cpp:235`, set at `start` and on every swap, `:154`, `:275`); the metronome clicks on the music's beat
(`:612`) and the published bound values are the heard block's (`:303`). A live `set` that moves a
device's latency (a lookahead) builds a new engine instead of a message (`latencyOf`,
`core/service/ServiceTransport.cpp:86`, checked at `:877`; the live engine's latencies recorded by
`buildLive`, `:80`, via `Engine::deviceLatency`, `engine/Engine.cpp:600`).

**Stated limits.** Bound values (automation, formulas — R-AUTO-7) are evaluated at the engine's position,
so a value applied AFTER a latent device lands up to that path's latency early (≤ 10 ms, 2 ms by
default); a latency an automation moves while the song plays is compensated at its value at the start
(build time); toggling a latent device's bypass while playing clicks once (≤ its latency) — its own
buffer is not fed while bypassed.

**Measured.** Engine (`test_latency_is_compensated_everywhere_signals_meet`,
`engine/tests/engineTests.cpp:666`): a click+tone on the LEFT through a limiter and its twin on the RIGHT
with nothing meet at a bus — left and right equal sample for sample, the click at 1000 + 96 — by main
output or by post-fader send, the limiter bypassed or not, in chunks of 128 or 77; a master limiter
puts Main and a cue sent straight to Phones at 96 together; a key from a limited strip delays its target's
audio 96 to meet it, and a limiter before the keyed compressor delays the key 96 — the keyed strip's output
exactly the unlimited one 96 later (and it does duck). Mutants: every delay sized 0 → the twins fail;
no key delay in the rack → the key check fails. Service (`test_latency_is_compensated_and_a_render_starts_on_the_beat`,
`core/tests/serviceTests.cpp:7657`): two strips of one tone, a limiter on one — each stem from beat 0 and
equal, the mix exactly their sum and equal to the render with no limiter; a master limiter's render, port
and stems byte-identical to none (under its ceiling it is exactly its delay); `--to` exact; live, the
device's frames are the render 96 later; a lookahead set to 5 ms WHILE playing keeps the twins met — the
heard frames are the 5 ms render 240 later (mutants: no trim → red; no re-plan on a latency change → red).
End to end from a shell (`solaris-cc`, two Drum Machines of one pattern at −6 dB): with a master limiter
the beat-0 kick starts at sample 0 — its first samples −0.04054, 0.00870, 0.02645 equal the render without
it — where the build before this change put it at sample 96; with a limiter on one drum strip the two
stems are sample-identical over all 100 096 frames and the mix's cross-correlation peak against the plain
mix is at lag 0.

### DR-LANE-1 Lanes are reordered (R-LANE-1)
`lane move <ln> --to <index>` (`core/service/Command.cpp:127`; `case K::LaneMove`,
`core/service/ServiceEdit.cpp:566`) puts the lane at row `index` (0 = the top) and renumbers every lane
0…n−1 in the new order — one edit, one undo step; an index past the last row or an unknown lane is
refused. The lanes draw it through Interstellar's `AnimatedRows`, keyed by lane, so the rows slide
(DR-UI-3). Guarded by `test_lanes_are_reordered` (`core/tests/serviceTests.cpp:6764`: to the top and back,
renumbered, refused out of range, undone) and, drawn, by `test_instrument_drop_and_clip_drag`
(`app/tests/ui/uiTests.cpp:518`: a lane moved from a shell slides to the top with its clip, caught mid-tween).

### DR-TIME-6 The ruler dragged (R-TIME-6)
`Timeline::rulerGesture` (`app/widgets/Timeline.cpp:891`, called first in `handleGesture`, `:986`) takes a
press on the ruler without Shift and waits: a click is still the seek (R-TIME-5) or clears the loop
(R-EDM-7). A drag decides ONCE, by where it began: within `kBraceGrip` (6.5 px) of an end of the loop's
brace, or on its body, in the ruler's lower half (`:911`), it moves the loop; anywhere else it scrubs.
Scrubbing, the playhead is the pointer's on the snap step (`advance`, `:497`, overrides both the eased
seek and following the audio while it is held) and a `transport seek` goes at each new line — a fader
sends each step the same way; a seek is transport, not an edit, so no undo step and the song stays
saved — and once more on release if the last line was not sent; the playhead is `set` there, so
nothing eases back. The brace is the pointer's while held (`loopSpan`, `loopRect`), moved with its length
kept or resized never shorter than one step, and ONE `transport loop <a> <b>` goes on release (`:972`),
the brace placed where it was let go so the model's echo moves nothing.

Guarded by `test_the_ruler_is_dragged` (`app/tests/ui/uiTests.cpp:1966`): held, the playhead equals the
snapped pointer beat exactly and four or more seeks were sent; let go, it stays; PLAYING, it stays the
pointer's frame after frame and the song plays on from there; the brace moved 4–8 → 6–10, nothing sent
until release, then one line; resized by its right end, clamped at its left; a click still seeks. Mutants
seen red, then restored: the playhead following the audio while scrubbing, a seek only on release, a body
drag keeping the brace's end. Shots `ruler-scrubbing` and `loop-brace-moving`
(`app/tests/shots/renderShots.cpp:1474`, `:1483`), both sizes, looked at.

### DR-TIME-7 Stop takes the time bar back to where it was put — or leaves it (R-TIME-7, R-TIME-8)
The service keeps a CUE (`mCue`): where the user PUT the time bar. A `transport seek` puts it where the time bar went
(`core/service/ServiceTransport.cpp:679` — a ruler click or drag, the start key), and a play that says where it
begins puts it there — `--from`, a roll's `--clip` (`:521`); a plain `transport play` starts wherever the time bar
stands and leaves the cue alone. `transport stop --return` stops and moves the time bar back to the cue (`:650`); a
plain `transport stop` leaves it where it was heard (R-TIME-4) — a TEMPORARY place: the next play starts there, and
a stop with the return after it still goes back to the cue (R-TIME-8, amended 2026-10-16). A new or opened song
starts it at 0. Published as `transport.cue`. In the window: the play/stop key (Space) while playing is `transport
stop --return`, the stop-here key (Ctrl+Space) is `transport stop`, either one while stopped plays (`App::songAction`,
`app/App.cpp:569`, the keys being the keymap's — DR-SET-4); the song bar's Stop and a roll's ■ return too (`:55`).
On the ruler an outlined mark stands at the cue while playing — where Stop takes the time bar back — fading in
with the play and out with it (`Timeline`, `app/widgets/Timeline.cpp:1659`, eased at `:479`), beside the
playhead's filled one. L2 `test_stop_returns_to_the_cue_or_stays`; UI `test_space_returns_and_ctrl_space_stays`
(Ctrl+Space, then Space plays on from there, then Space's stop is back at the cue); shot `ruler-cue`. Mutants: a
stop that ignores `--return`, a mark that snaps, every play moving the cue — all seen red.

### DR-LANE-3 Instrument tracks (R-LANE-3; R-LANE-1 amended)
**The model.** `Lane::track` (`model/Project.h`) names the instrument strip a lane is the TRACK of; the
file writes it `#alane … track=ch_2` (the suite's word for a strip, as a clip's `track=`), and every
save writes the header line `tracks = on` (`model/Project.cpp:1000`; read at `:805`, any other value
refused). `validateProject` refuses a track of no strip or of a non-instrument (`:1331`) and a clip on a
track that does not play through it — audio named as audio (`:1428`). `laneTitle` (`:49`) is the name a
lane shows: its own, else a track's strip's — so renaming the instrument renames its tracks (D-2's
lane half).

**The service.** A lane made for an instrument is its track with no name of its own (`newLane`,
`core/service/ServiceEdit.cpp:604`); an instrument's clip with no `--lane` goes on its track (`laneFor`,
`:619`; `trackOf`, `:608` — the track its newest clip is on, else its newest, else a new one), and only
an instrument with no track and clips on plain lanes keeps R-SVC-8's rule. `clip add --lane <a track>`
(`:644`) implies the strip and places `--pattern` there; audio, `--instrument` and a different `--strip`
are refused there by name. `clip move` (`:845`): onto a track a note clip is re-routed in the same
edit (one undo step) and the output says `<ac> plays through <ch> (<name>)`; `--strip <other>` on a
track moves it to that one's track; audio and `--lane` with a different `--strip` are refused. `lane add
--strip <ch>` (`:529`); `set <ln>.strip=<ch>|none` (`core/service/SolarisService.cpp:874` — refused while
the lane holds another strip's or an audio clip; `none` keeps the name it showed). `strip relink` takes
the strip's tracks with its clips (`ServiceEdit.cpp:327`); `strip delete` leaves them plain lanes named
as they were (`:267`). A song saved before tracks (no `tracks` line) is adopted on opening
(`adoptTracks`, `SolarisService.cpp:76`, called at `:520`): a lane whose clips all play through one
instrument becomes its track, a name it was made with (its strip's, or its instrument's registry
label) cleared so it shows the strip's, each said as an `info` event; opening stays clean (not dirty).
Published: `lanes[].name` (what it shows), `lanes[].strip`, `lanes[].ownName`
(`core/service/ServiceModel.cpp:282`); `ls` marks `· track of <ch>` (`core/service/ServiceCompose.cpp:680`).

**The view.** A track's row takes its strip's colour unless it has its own and names its instrument
under its name (`Timeline::bind`, `app/widgets/Timeline.cpp:173`), that line fading in and out as the
lane becomes and stops being a track (`advance`, `:613`). A lane header's right-click (`:1261`): Track
of ▸ (the instrument strips) → `set <ln>.strip=<ch>`, Plain Lane → `…=none`, Copy ID. A sample or an
instrument dropped from the browser onto a track asks for a lane of its own (`--lane new`,
`app/widgets/ProjectScreen.cpp:129`) — it could not play through another's instrument.

Guarded by `solaris_model` (`model/tests/modelTests.cpp:459`: the four refusals; `tracks` read, refused
other than `on`, written in the canonical text), `solaris_service` (`test_instrument_tracks`,
`core/tests/serviceTests.cpp:2200`; `test_a_song_from_before_tracks_adopts_them`, `:3624`) and
`solaris_app_ui` (`test_a_lane_is_an_instruments_track`, `app/tests/ui/uiTests.cpp:262`: a rename
followed, a clip dragged onto another track re-routed with its colour caught mid-ease, Track of ▸ and
Plain Lane caught mid-fade, a browser drop onto a track → `--lane new`). Mutants seen red, then
restored: no re-route onto a track, no adoption, a track not showing its strip's name, the line placed
instead of faded, a drop onto a track not redirected.

### DR-CLIP-6 MIDI clips made on a track, looped by their end, moved between tracks (R-CLIP-6, R-CLIP-7, R-CLIP-8)
**Made** (R-CLIP-6): a double-click on an instrument's track where no clip is (`app/widgets/Timeline.cpp:1120`)
or its right-click menu's New MIDI Clip (`:1153`) sends ONE `clip add --lane <ln> --at <the snap step>
--length <a bar>` (`newClipOn`, `:880`) — the track's instrument implied (DR-LANE-3), a new empty pattern
made; a double-click on the clip opens its piano roll as before (R-ROLL-1). **Looped** (R-CLIP-7): a press
within `kEdgeGrip` (6.5 px) of a clip's right edge takes its END (`:1077`); dragged, its length is the
pointer's on the grid you see, never shorter than a step (`:1119`); let go, it stays where it was let go
and ONE `set <ac>.length=<beats>` is sent (`:1146`). A clip's drawn length is eased like its place (`len`,
`:835`; `liveLength`, `:392`), so a length set from a shell or an undo grows or shrinks it rather than
cutting it; the pattern's notes repeat inside it with a seam at each repeat (`paintClip`, `:1417`, from
the live length). **Moved** (R-CLIP-8): the clip drag's `clip move --lane` onto another instrument's track
re-routes it (DR-LANE-3); audio is refused there and eases home.

Guarded by `test_midi_clips_are_made_looped_moved_and_listed` (`app/tests/ui/uiTests.cpp:331`): a
double-click on the Bass track → the exact line and a clip of a new pattern through the bass; the empty
track's menu; the end dragged → the snapped length while held with nothing sent, one line on release,
no ease back; a shell's `set ac_1.length=6` caught mid-ease; a clip dragged onto the drums' track plays
through the drums. Mutants seen red, then restored: a length that snaps, an edge never taken. Shot
`clip-end-dragging` (`app/tests/shots/renderShots.cpp:1030`), both sizes, looked at.

### DR-CLIP-9 A clip copied: Shift-drag, Ctrl+C / Ctrl+V (R-CLIP-9, R-CLIP-10)
`clip duplicate <ac> [--at <b>] [--count <n>] [--lane <ln>]` (`core/service/ServiceEdit.cpp:902`): with `--lane` the
copies go to that lane under the move's rule (R-CLIP-8) — onto an instrument's track a MIDI copy plays through that
instrument (the pattern still shared, R-CLIP-3; "it plays through ch_3" said), an audio copy is refused there, a plain
lane (or `none`) keeps its strip. In the window, the lanes' clip drag reads Shift at every step of the drag
(`Timeline`, `app/widgets/Timeline.cpp:1124`) and as it is let go: held, the drop is ONE `clip duplicate <ac> --at <b>
[--lane <ln>]` and the original stays where it is (`:1185`); not held, it is the `clip move` it always was. While held
`mCopyAmt` eases in (120 ms): the original, dimmed during a move, comes back to full strength, and a "+" in the accent
fades onto the copy in hand (`:1652`) — pressed or let go mid-drag, it eases back. `expectCopy` (`:504`) makes the clips
the model brings next the selection (`bind`, `:224`) and — after a drop — places it at full strength where the
pointer left it, never fading in what was already shown; a refused copy brings none and the expectation lapses.
Ctrl+C and Ctrl+V are the keymap's `lanes.copy` / `lanes.paste` (DR-SET-4) and the Edit menu's Copy Clip / Paste Clip:
copy keeps the selected clip's id in the window (nothing sent; a toast says it, naming the paste key); paste
(`App::pasteClip`, `app/App.cpp:849`) is ONE `clip duplicate <copied> --at <the time bar>` on its own lane, the copy
selected — and with the time bar unmoved since the last paste, right after that copy, so pastes run end to end. A copied
clip since deleted is said, not pasted. While a piano roll has the keys its Ctrl+C / Ctrl+V are the notes' even with
nothing to copy (`:844`), never a clip's. L2 `test_a_clip_copied_onto_another_lane`; UI
`test_clips_are_copied_by_shift_drag_and_ctrl_c_v` (the copy look caught mid-ease; the copy at full strength and
selected; onto another lane; a plain drag still moves; pastes at the time bar, end to end, then where it moved; a
roll's Ctrl+V sends nothing); shot `clip-copy-dragging`. Mutants: Shift ignored at the drop, the copy look snapping,
pastes all at the time bar, the dropped copy fading in — all seen red.

### DR-CLIP-11 Several clips selected by a box, handled together (R-CLIP-11)
**The grammar.** `clips duplicate "<ac> …" [--by <b>]` and `clips delete "<ac> …"` (`core/service/Command.cpp:213`,
`core/service/ServiceArrange.cpp:261`) beside R-ARR-3's `clips move`: every clip named, or none — the first that is not
the song's is named and nothing is copied or deleted; a clip named twice is one. A copy is the clip again (its pattern,
its automation, its file: linked as `clip duplicate`'s, R-CLIP-3, R-AUTO-14) on its own lane, `--by` beats later —
by default the clips' span (the earliest start to the latest end), so the copies follow them; one before beat 0 is
refused naming it. It prints the copies in the order named; a delete says how many. Each is ONE edit: one undo step,
one `clip.added` / `clip.deleted` per clip.

**The window.** The selection is the timeline's (`Timeline::mSelection`, `app/widgets/Timeline.h:489`), with the clip
clicked last (`selectedClip`: Split at the time bar and Quantize act on it); `setSelection` (`app/widgets/Timeline.cpp:520`)
keeps that one while it stays selected, else the selection's first in the song's order. Every clip's ring is its own,
eased in as it joins and out as it leaves (`ClipLive::sel`, `advance`, `:843`; drawn `:1631`); a clip that goes leaves
the selection (`bind`, `:285`). **The box**: a press on empty lane space (`handleGesture` Down, `:1089`) or, with Ctrl,
anywhere on the lanes — on a clip too (`boxGesture`, `:575`, first in `handleGesture`) — becomes a box once it drags.
Its corners are kept in the lanes' content (beats; y from the first row), so it stays put while they scroll
(`selectBoxRect`, `:551`); every move selects every clip it touches (`boxTo`, `:558`) — with Shift held added to the
selection there was — so each lights as the box reaches it and unlights as it leaves. Let go, the selection stays and
the box fades (120 ms; drawn `:1678`); a press that never drags is a click. A click on a clip selects it alone, on
empty space none; Shift+click or Ctrl+click adds a clip or takes it out (`:1237`). **Handled together**: a press on a
clip of a selection of several carries the selection — R-ARR-3's group drag, generalised (`mDragGroupClips`): every
selected clip drawn the pointer's beats on (`clipRect`, `:455`; `paint`, `:1641`), the one in hand where the rest are,
none before beat 0 (`:1130`), on their own rows; let go, ONE `clips move "<ac> …" --by <b>` — or, Shift held, ONE
`clips duplicate "<ac> …" --by <b>`, the originals staying at full strength and the copies selected where the pointer
left them (`:1156`). The keymap's lanes actions take the selection (`App::songAction`, `app/App.cpp:918`): Delete is
ONE `clips delete`, Duplicate ONE `clips duplicate` (copies after it, selected — so Duplicate again runs on; one clip
keeps `clip duplicate`, now also selecting its copy), Copy keeps the selection's ids ("3 clips copied"), Paste
(`App::pasteClip`, `:849`) ONE `clips duplicate "<ac> …" --by <the time bar − the earliest>`, again end to end.
L2 `test_a_selection_of_clips_is_copied_and_deleted_in_one_edit` (`core/tests/serviceTests.cpp:7333`: MIDI, audio and
automation clips copied after themselves and `--by`, linked, on their lanes, one undo; refusals; deleted and undone);
UI `test_clips_are_selected_by_a_box_and_handled_together` (a ring caught mid-ease as the box reaches it and as it
leaves; the box fading; nothing sent; Shift/Ctrl+click; a Shift box adding; the selection dragged — the carried clips
the pointer's — moved, Shift-copied at full strength, deleted; a Ctrl box from on a clip; Ctrl+C / Ctrl+V / Ctrl+D);
`test_ruler_seek_keys_and_selection` now selects the Ctrl+D copy before Delete. Shots `clips-box-select`,
`clips-selection-dragging` (`app/tests/shots/renderShots.cpp:895`, `:907`). Mutants, each seen red: the ring snapping,
the box never fading, clips lit only at release, no selection carried, a Shift-drop moving, Shift+click replacing, no
Ctrl box, a Shift box replacing, the copies faded in, a deleted clip left selected, a paste `--by` the time bar, Delete
taking one; and in the service, a delete taking the first only, copies on top of the clips, a copy before beat 0
clamped, an unknown clip skipped.

### DR-BROWSE-4 The song's MIDI list (R-BROWSE-4)
The Song tab lists a MIDI header, **New MIDI**, then every pattern, then the samples under SAMPLES
(`Browser::rebuild`, `app/widgets/Browser.cpp:189`) — keyed rows that grow in and fade out. A pattern's row
reads its name and, in mono at the right, its length (bars when whole bars), its note count and the
strips that play it, or "unplayed" (`bind`, `:93`, from the model's `patterns[].strips`, published with
`patterns[].lastStrip` — the strip its newest clip plays through — by `core/service/ServiceModel.cpp:378`).
New MIDI sends `pattern new --name "MIDI <n>"` (`:390`); a double-click opens the pattern's piano roll
(`:421`); its menu (`:427`): Rename… (`set <pt>.name=`), Piano Roll, Duplicate, Copy ID, Delete (the
service refuses while a clip plays it, naming them). Dragged onto the lanes (`ProjectScreen::place`,
`app/widgets/ProjectScreen.cpp:136`): onto an instrument's track → `clip add --pattern <pt> --lane <ln>
--at <b>`, through that instrument; onto a plain lane or below the last → `clip add --pattern <pt> --strip
<its lastStrip> --at <b> --lane <ln|new>` (below: a new track for it); a pattern nothing plays, dropped off
a track → a notice, nothing sent. One drop, one command (R-BROWSE-3).

Guarded by the same test: MIDI listed first with "1 bar" and "Bass"; New MIDI → the line, its row caught
mid-grow, "unplayed"; dropped on the Bass track, below the last lane, and (unplayed) off a track → the
notice and nothing sent; a double-click → `roll:pt_1` open; the menu's five items, Delete → `pattern
delete`. Mutant seen red: a drop on a track ignoring the track. Shot `midi-list` (`:448`), looked at.

### DR-VST-7 The plugins' own editor, in the Arstro look (R-VST-7, R-VST-8)
**The seam** is the DSP repo's (REQ-vst-6, DSP `e53edb6`): `Controller::createView("editor")` returns
`createEditor(*this)` when the build defines `ARSTRO_VST3_EDITOR`, nothing otherwise. The umbrella
compiles its editor into both plugin modules and defines it (`plugins/CMakeLists.txt:29`) — built from
the DSP repo alone the plugins offer none and a host draws its generic view.

**The content is Solaris's DevicePanel**, generated from the registry (law 2): `InstrumentEditor`
(`plugins/InstrumentEditor.cpp`) builds a model of ONE device each frame the host's values moved
(`refresh`, `:59`) with `deviceModelOf` (`core/service/RegistryModel.cpp:7` — the ONE registry→model
mapping, which the service now uses too, `core/service/ServiceModel.cpp:63`), the panel's song controls
off (`DevicePanel::songControls`, `app/widgets/DevicePanel.h:101`: the header names the instrument with the
suite's mark and teal dot instead, `app/widgets/DevicePanel.cpp:752`). Solaris's accent and embedded fonts
are installed before a widget is built. The panel speaks text as in Solaris; `command` (`:82`) turns
`set dv_1.<name>=<value>` into the host's edit through `ParamAccess` — begun ONCE per parameter per
press, performed per step (a step that moved nothing says nothing, `:98`), ended when the pointer lets
go (`pointer`, `:117`), a click or a double-click's reset all three at once. `ControllerAccess`
(`plugins/PluginAccess.h:15`) is the plugin's side: the controller's `editPerform` normalises by
`normalizedFromValue`. A value the HOST changes is a new revision: the slider's thumb springs there
(Artboard's Slider eases a programmatic value) and the row it changed is lit (`:66`) — and nothing is
echoed back as an edit.

**The view** (`plugins/X11View.cpp`): an X11 child of the host's window on a connection of its own
(`attached`, `:76`), with the XEmbed info hosts read on creation (`:82`); the host's `IRunLoop` — queried
from the plug frame — drives it: our connection's descriptor for input, a 16 ms timer for frames (`:99`).
Each frame is drawn into a back buffer and copied to the window (`frame`, `:175`). Resizable: the host's
size is the editor's, never below 360×320 (`checkSizeConstraint`, `:132`); it opens at 520×600.
`createEditor` (`:243`). Everything linked into the module is position-independent (`solaris_app`,
`artboard_core`, `solaris_instrument_editor`).

Guarded by `solaris_plugin_editor` (`plugins/tests/editorTests.cpp`, the editor over the REAL plugin
controller with a fake host's component handler, headless): a row per registry parameter at both sizes
(`:164`); a drag → beginEdit once, performEdit per step equal to `normalizedFromValue` of the value the
editor chose, strictly rising, endEdit on release; a choice clicked → the next name, begun/performed/
ended (`:194`); a host change caught between, then lit, nothing echoed (`:251`); the drum machine's
editor builds (`:316`). The validator passes both plugins with the editor linked in. Mutants seen red,
then restored: no beginEdit, an end per step, a host change never reaching the panel, a linear mapping in
the plugin. **Live (L5, 2026-10-09):** `vst3_editorhost` (the SDK's X11 host) opened Drum Machine's editor
in a real host window, 520×600, drawn as above; a drag sent to its window moved Kick Decay 450 ms →
29.9 ms with its row lit.

**Drum Machine's pads** (E6; DSP `9634166`, REQ-device-9 and REQ-vst-7). A kit — a type with `noteNames` —
gets a `PadGrid` (`plugins/InstrumentEditor.cpp:140`) in the panel's band (`DevicePanel::band`,
`app/widgets/DevicePanel.h:104`: room under the title, the rows below it; opt-in, 0 in Solaris), five to a
row (`:264`): a pad per key the registry names, in its order, each joined to its parameters by the
registry's `notePrefixes` (`:154` — the join D-3 lacked). A press plays the pad through the plugin —
`ParamAccess::play`, the controller's `playNote`, an `arstro.note` message to the processor, heard at the
next block — and picks it; the release is its note-off (`handleGesture`, `:199`). The pick eases its ring in
and the hit flashes and dies away like the sound (`advance`, `:174`); the panel scrolls, eased, so the pad's
group begins at the top (`DevicePanel::revealGroup`, `app/widgets/DevicePanel.cpp:490`, called at `:33`).
Playing a pad edits nothing.

Guarded by `test_a_pad_plays_and_shows_its_parameters` (`plugins/tests/editorTests.cpp:333`: the pads are
the registry's, Closed Hat joined to `chat`; a press plays 42 at 100, the release 42 at 0; flash and pick
caught mid-ease; the CHAT group at the top of the rows; no edit) and `test_a_pad_is_heard_through_the_processor`
(`:378`: the REAL processor and controller connected by the SDK's host classes — silence, then a pad clicked
in the editor heard as the kick at the next block, peak 0.358). Mutants seen red, then restored: a pad not
played, the pick snapped, the least scroll instead of the group, the controller never sending the note.
**Live (L5):** in `vst3_editorhost` a click on Closed Hat picked it and brought the CHAT group to the top.

### DR-UI-12 An instrument's face is an analog synth panel (R-UI-12; R-WIN-2, R-VST-7 amended)
`AnalogFace` (`app/widgets/AnalogFace.cpp`) is an instrument's DevicePanel body — built in the rows' place when
the device is an instrument (`DevicePanel::build`, `app/widgets/DevicePanel.cpp:194`; `analogFace`, opt-out,
`app/widgets/DevicePanel.h:94`), so the same face is Solaris's window and the plugins' editor. Still generated
(law 2): `build` (`AnalogFace.cpp:128`) makes a module per parameter group in the registry's order, the ungrouped last, titled
by `devices[].groups` (DSP REQ-device-10, G2), a knob per number and a switch per choice; a module's SCREEN is
chosen by what its parameters say (`:156`): a `wave` choice → the oscillator, `cutoff` + `res` → the filter,
`attack` `decay` `sustain` `release` → the envelope. `layoutFor` (`:213`) flows the modules into the width —
rows share their height and spare width; at 980 px the synth's six fill two rows, at 390 one a row. The panel
scrolls the face (R6). Values are written through the shared `ParamFormat.h` (`toUnit`/`fromUnit`, the
readout, the stored text), so a knob and a list row read alike.

**Knobs** (`paintKnob`, `:504`): a 270° dial, its arc in the accent from the minimum or from zero for a range that
spans it, detents for a whole-number range of a few steps (`:517`), a cap lit from above (a radial fill between
two theme greys), the label and the value in its unit under it; a bound one names its binding in the accent and
draws its arc fainter; the last change a halo. **Switches** (`paintSwitch`, `:560`): a key per choice drawn as its
shape (sine, saw, square, triangle; lowpass, bandpass, highpass, notch — `drawGlyph`) or named, the light sliding
(`:567`). **Screens** (`paintScreen`, `:592`): the oscillator (`:626` — its wave morphing with the switch's light,
its level, its pitch as cycles, its unison voices spread by the detune), the filter (`:659` — `responseDb`, `:114`:
the DSP's documented SVF prototype (S4) at (S5)'s Q, its mode morphing, the cutoff marked and where the envelope
sweeps it), the envelope (`:696` — linear stages, as the library's ADSR, each stage as wide as the root of its time).
Tokens: `surface::moduleBg`, `screenBg`, `knobCap`, `knobTrack`, `screenGrid` (`app/Theme.h:84`).

**Motion and gestures** (`advance`, `:272`; `handleGesture`, `:429`). A value set anywhere turns its knob there
(220 ms) and every screen redraws through the turning knob; a switch's light slides (220 ms); the last change's
halo fades in and out. A drag is direct manipulation — up turns it up, Shift five times finer (`:455`), the knob
the pointer's — and each step that moved the value is ONE `set` through the panel; a bound knob takes no drag
(`:439`); a double-click is the default; a right-click opens the parameter's menu (`DevicePanel::openParamMenuFor`,
`app/widgets/DevicePanel.cpp:639`, R-WIN-3); the wheel scrolls. Show IDs puts each address over its label (`app/widgets/AnalogFace.cpp:742`).
An instrument's window opens 820 × 580 (`app/widgets/FloatWindow.cpp:145`), three modules a row; an effect's
keeps the list at 390 × 520. The plugins' editor opens at the face's own size — 980 wide, as tall as its modules
(`InstrumentEditor::preferredSize`, `plugins/InstrumentEditor.cpp:40`, asked by `plugins/X11View.cpp:64`).

**The filter screen found a defect** (D-4): measured against the real `StateVariableFilter`, every resonance
peaked at −3.01 dB — the synth's Resonance had never done anything (`tick` read a smoothed value only the block
path advances). Fixed in the DSP library (`66b5ecb`, REQ-svf-2 amended); the screen and the filter now agree to
0.000 dB over 4 modes × 3 resonances × 5 frequencies.

Guarded by `solaris_app_ui` (`test_an_instruments_face_is_an_analog_panel`, `app/tests/ui/uiTests.cpp:447`: six
titled modules, a window ≥ 800 wide; a shell's change caught mid-turn and lit; a key's light caught mid-slide;
a double-click's default; the menu's Create Automation, after which the knob names `au_1` and takes no drag; the
flow at 390 and 980; an effect's list) and `solaris_plugin_editor` (`plugins/tests/editorTests.cpp:164`: the
modules, screens and rows at 980, one a row at the minimum; `:200` a knob dragged up → beginEdit once, performEdit
= the shared mapping of each value, strictly rising, endEdit, Shift 5× finer, a key's choice; `:257` the host's
change caught mid-turn and a choice's light mid-slide; `:285` the filter screen against the filter). Mutants seen
red, then restored: a knob that snaps, a light that snaps, a bound knob that drags, the screen's Q drifting.
Shots `device-panel` (the face), `device-face-turning` (mid-turn and mid-morph), `effect-window`
(`app/tests/shots/renderShots.cpp:334`, `:345`), and the plugin's `plugin-editor-synth.png` — looked at.

### DR-UI-13 The song's tempo is set in the song bar (R-UI-13)
The song bar's "<bpm> bpm" is a control (`SongBar`, `app/widgets/SongBar.cpp`; its hit rect `kBpm`, `:93`, placed
after the position by JetBrains Mono's fixed 0.6 em advance, so it needs no render target). A press takes it;
dragged up or down it is the POINTER's — a bpm per `kBpmStepPx` (3.25 px) of vertical travel, a tenth with
Shift, 20…999 — and every step that moved it sends ONE `set project.bpm=<n>` (`SongBar::handleGesture`, `:205`;
the recognizer reports a drag past its slop, so the first line may cover two steps). A double-click opens the
App's text field with the tempo, and what is typed is one `set project.bpm=` — the service refuses what is not a
tempo, said in its toast (`:249`). It is lit while held, washed on hover, and a tempo set from elsewhere is
drawn easing there over 220 ms (`kCatchUpMs`) — `bpmShown()` is the LIVE value (`:146`). Closes the UX audit's
U-1. UI test `test_the_tempo_is_set_in_the_song_bar` (mutant: a tempo that snaps, seen red).

### DR-UI-14 The pointer says a drag resizes (R-UI-14)
`Cursor` (`app/Cursor.h`): Default, ResizeH ↔, ResizeV ↕, ResizeDiag ⤡, each with its CSS name. Every widget with a
grip answers twice, from the SAME zone its press takes: `dragCursor()` — the cursor a resize in hand keeps, wherever the
pointer is — and `cursorAt(local)`. `Timeline` (`app/widgets/Timeline.cpp:469`): ↔ over a clip's end (`kEdgeGrip`,
R-CLIP-7) and the loop brace's ends (`kBraceGrip`, the ruler's lower half), and while either is held (pressed, not yet
moved, included). `PianoRoll` (`app/widgets/PianoRoll.cpp:770`): ↔ a note's end (the zone `noteAt` gives, D-7) and the
pattern's end handle, ↕ a velocity bar, and while any is held. `MixerDock` (`app/widgets/MixerDock.cpp:1241`): ↕ its top
edge (`Part::Resize`). `WindowLayer` (`app/widgets/FloatWindow.cpp:327`): ⤡ a window's corner (`FloatWindow::resizing`
while it is held); the front-most window under the point decides, so it covers the lanes beneath. `ProjectScreen::
cursorAt` (`app/widgets/ProjectScreen.cpp:20`) asks every drag in hand first, then the window, the dock, the lanes;
`App::cursor` (`app/App.cpp:622`) gives the arrow while a modal is open (a menu, a dialog, the settings, the history
tree) or off the song, else the screen's answer at the last pointer position (`App::pointer` keeps it). The GTK host
asks after every pointer event and, when it changed, sets the window's cursor by name (`showCursor`,
`linux_main.cpp:117`) — the OS's own cursors, which do not ease. UI `test_the_pointer_says_a_drag_resizes` (every grip;
a clip's end held off the edge; a window over the lanes; a modal). Mutants: no clip-end zone, a resize in hand
forgetting its cursor, a modal not the arrow — all seen red. The GTK half is compiled and read, not driven live (D-5).

### DR-SAFE-1 Autosave and recovery (R-SAFE-1, R-SAFE-2)
While a song has unsaved edits the service writes it beside itself, `<song>.slp.autosave` — the `.slp` text exactly as a
save would write it — through a temporary file renamed over the last one, so a crash mid-write keeps the previous
autosave (`SolarisService::autosave`, `core/service/SolarisService.cpp:696`). When: once `settings.autosave` seconds
(default 60; `settings set autosave=<1…3600|off>`) have passed since the song opened or was last autosaved — checked by
`pump`, so an idle window still writes (`core/service/ServiceTransport.cpp:138`) — and AT ONCE after an edit whose verb
deletes or removes something (`dispatch`, `core/service/SolarisService.cpp:426`). Each write is announced (`project.autosaved path=`). A save drops it
(also the old place's, when saved elsewhere); so does a close — closing is choosing. A session that dies leaves it.
`recoveryOf` (`:558`) finds an autosave NEWER than its song: Home's card says "unsaved changes kept" (`recents[].recovery`);
`project open` opens the SAVED song and says `recovery: …` (`:684`), emits `project.recovery path= age=`, and the model's
`recovery` / `recoveryAge` name it until it is answered: `project recover <path>` (`:690`) opens the autosave as the song
— at `<path>`, unsaved, its history rooted "Recovered" — and `project discard-recovery <path>` deletes it. The window asks
once per offer (`App::askRecovery`, `app/App.cpp:517`: Recover · Open the Saved One); Settings › Interface has an
Autosave combo. L2 `test_autosave_and_recovery` (the clock, a removal at once, atomic, save and close drop it, a dead
session's autosave offered, recovered, discarded, off writes nothing); UI `test_a_recovered_song_is_offered`. Mutants:
a removal not written at once, the window not asking — both seen red.

### DR-MIDI-1 MIDI files (R-MIDI-1)
The model reads and writes Standard MIDI Files as pure functions, bytes in and out, beside the `.slp` parser
(`model/Midi.cpp`): `parseMidi` (`:54`) reads formats 0, 1 and 2 — running status, a note-on of velocity 0 as a note-off,
notes paired first-in first-out per channel and pitch, a note still sounding at its track's end ended there, meta names
and tempos, system exclusive skipped — and both divisions; `midiBeats` (`:177`) turns a tick into beats (ticks over the
division, or for SMPTE time seconds through the tempo map, 120 bpm until its first tempo); `writeMidi` (`:194`) writes
format 1 at 960 ticks a beat — Solaris's own tick — a tempo track (name, meter, tempo) and a track per entry, offs
before ons at one tick. The service reads and writes the files (`core/service/ServiceMidi.cpp`): `pattern import`
(`:48`) makes a pattern per track with notes (or `--track n`'s), named after the track, whole bars long, a pitch
started twice at one tick kept once (the longer); with `--lane` / `--instrument` the tracks are merged into ONE pattern
and a clip of it is made by `clip add`'s own rules (`:160`) — so a track's instrument plays it, or a new strip is made.
`pattern export` and `project export-midi` (`:173`) write a pattern, or the song as it plays (a track per instrument
strip, each note clip's pattern at its place, repeated to fill it and cut at its end; a kit on channel 10). The
browser lists `.mid` files (the host classes them `midi`) and a drop is ONE line (`ProjectScreen::place`,
`app/widgets/ProjectScreen.cpp:144`): on a track `pattern import <file> --lane <ln> --at <b>`, elsewhere `--instrument
synth`. L2 `test_midi_files` (a pattern's round trip to the tick; a hand-made file with running status, velocity-0
offs, 96 ticks a beat, named tracks; SMPTE time through its tempo; a drop on a track merged, one undo; a new
instrument; the song exported as it plays; refusals); UI `test_a_midi_file_dropped_on_a_track`. Mutants: velocity 0
not an off, running status unread — both seen red.

### DR-METER-1 The master's loudness (R-METER-1)
The engine writes the master as it leaves — after its rack, gain and latency delay, exactly what `capturedMaster`
holds — into buffers the CALLER owns (`Engine::tapMaster`, `engine/Engine.h:130`; written in `renderPiece`,
`engine/Engine.cpp:1054`), so the tap never allocates. The player owns those buffers and the DSP library's meters
(`LoudnessMeter`, `TruePeakMeter`, `CorrelationMeter`, `SpectrumAnalyser` — REQ-meter-1…4), built in `start` after the
engine's build set the rate; after every block `Player::measure` (`core/Player.cpp:408`) runs the tap through them and
stores momentary, short-term, integrated (re-read only when a gating block was added), true peak (the most since play
or a clear), correlation and 61 bands into atomics. `pump` publishes them as `loudness` (`core/service/ServiceTransport.cpp:272`)
— not in the stable dump, −144 for nothing measured, KEPT after a stop so a played song's integrated loudness can be
read, reset by another song (`core/service/SolarisService.cpp:420`). `render --measure` runs the trimmed mixdown through
the same meters 100 ms at a time and prints ONE line, `measure integrated= momentaryMax= shortTermMax= truePeak=
correlationMin=` (LUFS, LUFS, LUFS, dBTP, −1…+1; the lowest correlation only where the momentary is above −70 LUFS —
silence has no phase; `core/service/ServiceRender.cpp:51`). The window: the master's card has a chip with the
integrated LUFS where a strip has mute and solo (`MixerDock::loudnessRect`; caught up over 220 ms, "— LUFS" cross-faded
in for none — `app/widgets/MixerDock.cpp:1097`); a click opens the Loudness window (`WindowLayer::openLoudness`,
`app/widgets/FloatWindow.cpp:276`), a `LoudnessPanel` (`app/widgets/LoudnessPanel.cpp`): the five readings (each
catching up over 220 ms; nothing measured cross-fades to "—"), the correlation's needle, the 61 bands on −72…0 dB
(rising in 40 ms, falling in 300) and Reset — `meters clear`. L2 `test_meters_measure_what_plays` (a −6 dBFS 1 kHz
sine: −6.02 LUFS momentary, short-term and integrated, −6.02 dBTP, correlation 1, band 34 at −6, two octaves down 20 dB
less), `test_render_measure`; UI `test_meters_and_the_loudness_window`; shots `meters-playing`, `loudness-window`.
Mutants: the tap's right channel silent (−9.02 LUFS), the clear not resetting the loudness, the readings snapping to
"—" — each seen red.

### DR-METER-2 Strip meters: RMS, held peak, clip latch (R-METER-2)
For every strip and the master, after every block (`Player::ballistics`, `core/Player.cpp:395`): the RMS through a
300 ms one-pole on the blocks' mean squares (the engine's per-strip sums; the master's from the tap), and a held peak
— the most of the last 1.5 s, then falling 20 dB a second — in SAMPLES, so it is the same however the window draws.
A block at or over full scale sets the strip's clip flag; `pump` takes the flags (exchange) into a latch the SERVICE
keeps (`mClipLatch`, published by `refreshModel`, `core/service/ServiceModel.cpp:263`), so a clip light outlives a
stop and an edit. Published: `strips[].rms/.hold/.clip`, `transport.masterRms/.masterHold/.masterClip` (not stable);
a stop zeroes peaks, RMS and holds and leaves the lights lit. `meters clear` (`core/service/ServiceTransport.cpp:614`,
not an edit, `meters.cleared`) puts every light out, zeroes the holds and starts the true peak and integrated loudness
again: on the audio thread it is a `Live::ClearMeters` MESSAGE (`core/Player.cpp:266`), so it lands in order after a
gain edit sent before it; until the thread has taken it (`Player::clearing`) `pump` reads no holds, flags or loudness,
which are from before it. The window (`MixerDock::paintFader`, `app/widgets/MixerDock.cpp:1809`): the peak a lighter bar,
the RMS its solid core, the held peak a 1.5 px line in its zone's colour, all eased (40 ms up, 300 ms down); above the
meters a clip light lit red, eased, whose click is `meters clear` (`:1565`). L2 `test_meters_measure_what_plays` (RMS
0.354 and a held 0.5 for a 0.5 sine; +12 dB lights both latches; a clear while playing allocates nothing on the audio
thread; latched through a stop and an edit), `test_held_peak_holds_then_falls`; UI `test_meters_and_the_loudness_window`.
Mutants: no hold (the held line = the peak), a latch lost on refresh, the clip light snapping — each seen red. The
pending-clear guard's race was not reproduced by its mutant in three runs; it is kept for the window it closes.

### DR-EXPORT-1 Delivery formats (R-EXPORT-1)
`render` (`core/service/ServiceRender.cpp:74`) takes the format from `--format`, else the file's extension (`:79`):
WAV through the host's `writeWav` (16 / 24-bit PCM as x·2^(bits−1) clamped — the dither's grid exactly — or 32-bit
float; `host/AudioFiles.cpp:117`), FLAC (16 / 24, the 24 in the top of 32 as FFmpeg's FLAC reads it), MP3
(libmp3lame), Ogg Vorbis (libvorbis) and Ogg Opus (libopus) through `writeAudio` (`host/AudioFiles.cpp:167`: an
FFmpeg muxer and encoder, the sample format the encoder takes, a last frame padded with silence where the codec
needs whole frames). `--bits` is for WAV and FLAC (default 24; 32f WAV only); `--kbps` for the lossy ones (MP3
32…320, default 320; Ogg 45…500, 256; Opus 6…510, 192); each refused elsewhere, naming the way. **`--rate` renders the
SONG at that rate** (`:112`): the project copied with its rate set, compiled with its samples decoded at the rate
(a cache of its own; the song's own is untouched), so every device is built for it — no resampler; the library's
rate is put back after (`RateBack`), and a render at another rate while playing is refused (the playing engine
shares it). Opus is 48 kHz (its default, any other refused); MP3 goes to 48 kHz. `--normalize <LUFS>` (`:243`)
measures the trimmed master with the DSP library's LoudnessMeter and TruePeakMeter, gains every file alike by the
difference — no more than keeps the master's true peak under `--ceiling` (dBTP, default −1) — and says so:
`normalize integrated= gain= truePeak= [held=ceiling]`. Integer depths are then TPDF-dithered by the library's
`Dither` (REQ-out-dither-1, an exact zero kept silent), seeded per file in order, so a song renders the same
bytes every time (`:271`); `--measure` reads what was written. The window: File › Export… opens `ExportSheet`
(`app/widgets/ExportSheet.cpp`): a combo a choice — Format, Sample rate, Bit depth, Quality, Loudness, Ceiling —
and a Stems toggle; a row the format does not take greys out, eased, and takes no input (`applies`, `:111`); a
choice the new format cannot take moves (`formatChanged`, `:99`); Export… hands the host's save dialog the
name with the format's extension and sends ONE `render --out <file> --format …` (`flags`, `:128`; `App`,
`app/App.cpp:115`). L2 `test_delivery_formats` (each format and its defaults by the extension or --format; 2 s at
44.1 kHz is 88200 frames and the tone still 1 kHz; 16 bits on the grid, dithered, the same bytes twice; −20 LUFS
by the meter; held at −2 dBTP; ten refusals); L3: the CLI wrote FLAC 24 / 48 kHz, MP3 44.1 kHz 192 kbps, Vorbis,
Opus 48 kHz and WAV 16 that ffprobe reads as such, FFmpeg's own ebur128 reading the −14 LUFS FLAC at −14.0 and the
44.1 kHz MP3 in tune; UI `test_the_export_sheet_is_one_render_line`; shot `export-sheet`. Mutants: no dither, the
rate not rendered at, the ceiling ignored, the greying snapping, greyed rows taking input — each seen red.

### DR-MIX-18 A side-chain in one line (R-MIX-18)
`strip duck <ch> --under <key> [--amount <dB>]` (`core/service/ServiceEdit.cpp:373`) is made of the commands that make each
part, so each one's rules hold: `device add <ch> --type compressor` (at the end of the rack), then `set` on it —
threshold −30 dB, the ratio R = 30 / (30 − amount) at which a full-scale key cuts the amount (default 8, 1…24), attack
1 ms, release half a beat at the song's tempo, Sidechain on — then `send add <key> --to <ch> --pre --sidechain` (a
pre-fader key: the kick's fader does not move the duck). It is ONE edit: one undo takes both back; a key that would go
back in processing order is refused by `send add`'s rule (R-MIX-4) and the whole line leaves nothing; a pair already
ducked is refused naming its send. It prints the compressor, then `made: send=…`. A real kick peaks under full scale
and cuts less than the amount: the default Drum Machine kick, 12 dB asked, takes the bass 7.3 dB down just after it,
back within 2 dB before the next beat at 120 bpm. The window: a strip's right-click menu has Duck Under ▸ listing every
strip whose published `keyTargets` include it — the strips that may key it — each ONE line (`app/widgets/MixerDock.cpp:1703`).
L2 `test_strip_duck_is_one_line` (the settings, the key, the duck measured on the bass's stem, one undo, six
refusals); UI `test_a_strip_ducks_under_another_from_its_menu`. Mutants: Sidechain left off, the send not a key,
the submenu's strips from the wrong side of `keyTargets` — each seen red.


### DR-MIX-19 Group / VCA faders (R-MIX-19; docs/discussion.md Round 13) — part 1: the model, the compile, the grammar
A VCA is a strip of kind `vca` (`strip add --kind vca [--name]`, default "VCA"; refused with `--out`,
`core/service/ServiceEdit.cpp:195`): a name, a colour, a gain, a mute and a solo, and no sound. The model keeps it so
(`model/Project.cpp:1289`): no output, no rack (`device add` refused there), no sends from it, and nothing routes or
keys to it (`:1276`); `targetsOf` offers none and gives none (`:1206`), so no picker lists one. A strip follows at most
one VCA — `set <ch>.vca=<vca id or name>|none` (`core/service/SolarisService.cpp:1141`; a VCA follows none; a VCA has no
`pan`, `input` or `monitor`), `vca=` on its `#atrack` line (docs/project-format.md §4). The compile leaves VCAs out of
the graph (`core/Compile.cpp:314` — strip indices and meters unchanged) and plays a member at `stripGainDb` — its own
gain plus its VCA's, either at the −120 floor the floor (`:144`), the one copy a live fader move reads too
(`core/service/ServiceTransport.cpp:818`; it used to send the member's own gain only — fixed here); a member whose
gain is a formula or an automation gets the VCA's dB as its `Bind`'s offset, added after the clamp (`core/Compile.cpp:628`,
`engine/Engine.cpp:389`). A VCA's mute mutes its members and its solo solos them, decided in `silentStrips` with the
rest (`core/Compile.cpp:158`). A VCA's own gain as a formula or an automation is refused for now (`SolarisService.cpp:1004`,
`core/service/ServiceAuto.cpp:138`; a file edited by hand: inert, said, `Compile.cpp:622`). Deleted, its members are
their own again in the same edit (`ServiceEdit.cpp:289`). While playing, a change to a VCA swaps the engine (its fast
path is part 2). Test: L2 `test_a_vca_moves_its_members` (`core/tests/serviceTests.cpp:5767`): a member under a
−6 dB VCA renders sample-exact as the member at −6; the VCA muted as the member muted; soloed as the member soloed; a
formula member (=−3) under −6 as the member at −9; the `.slp` round trip; each refusal; no picker offers it; a
member's own fader moved while playing under a −20 dB VCA heard as the render of the same (0.6 dB); deleted and undone.

Part 2 — live, and seen. While playing, the VCA's fader moves its members at once through the live messages each
member's gain already travels by — its own plus the VCA's, `stripGainDb` (`core/service/ServiceTransport.cpp:840`) —
with no engine rebuilt; its mute and solo go the silences path, the members' `StripSilent` from `silentStrips`; a
member whose gain is a formula carries the VCA's dB in the engine's bind, so then the move rebuilds; so does a VCA's
mute in a song with keys — a member under a muted VCA keys nothing, as one muted itself (`core/Compile.cpp:345`). The
model: `strips[].vca` (whom it follows), `strips[].vcaGain` (the dB that adds) and, on a VCA, `strips[].members`
(`core/service/ServiceModel.cpp:295`, in processing order); `transport.swaps` — engines built and swapped in since
play (`ServiceTransport.cpp:180`, counted from the play's own, `:608`), not in the stable dump — so a script can tell
an edit carried live from one that rebuilt. Tests: L2 `test_a_vca_moves_its_members_live`
(`core/tests/serviceTests.cpp:5903`): the VCA to 0 dB while playing heard exactly as rendered (0.3189 both), swaps 0;
its mute silences its member, swaps 0; a formula member's VCA move: one rebuild; the model's fields.
`test_a_vca_muted_member_keys_nothing` (`:5784`): a ghost kick under a muted VCA renders sample-exact as the kick
muted (the key, unmuted, does duck).

Part 3 — the dock. "+ Line" offers "VCA" (`app/widgets/MixerDock.cpp:1338`, one `strip add --kind vca --mixer`). A VCA's
card says "VCA · moves N" and names its members where a rack would be (`:1865`): a fader and M and S — no meter
(`paintFader`'s meters off), no pan, sends or route, none of them pressable (`:1298`); its menu offers no Freeze,
Bounce or VCA (the service refuses freezing or bouncing one, `core/service/ServiceAudioEdit.cpp:446`), and it dims for its
own mute only (`core/service/ServiceModel.cpp:262`). A strip's menu offers "VCA ▸" — the song's VCAs, and None when it
follows one (`MixerDock.cpp:1732`) — one `set <ch>.vca=`. A member says "VCA <name> <dB>" over its fader (`:1987`):
the tag fades in and out (`kSelectMs`) and its dB eases as the VCA moves (`kCatchUpMs`, `:1017`); beside a bound
gain's tag it slides to the right half, by the bound tag's eased amount. UI test `test_a_vca_in_the_dock`
(`app/tests/ui/uiTests.cpp:5027`): "+ Line" › VCA one line; the strip's VCA ▸ one line; the tag caught fading in; the
VCA moved from a shell, the member's dB caught at −1.22 on its way to −6; a drag where a VCA's pan would be sends
nothing. Shot `mixer-vca` at both sizes.

### DR-MIX-20 Per-strip tools (R-MIX-20; docs/discussion.md Round 14) — part 1: the model, the engine, the grammar
A strip's phase and width are its own: `set <ch>.phase=on|off`, `set <ch>.width=0 … 200` (%) — on its `#atrack` line
when set (`phase=invert`, `width=150`; docs/project-format.md §4); a VCA has neither
(`core/service/SolarisService.cpp:1166`). The compile passes them (`core/Compile.cpp:340`) and the engine applies them to
the strip's own sound after its rack and BEFORE its pre-fader sends and its fader (`engine/Engine.cpp:926`): width by
mid and side (s × width — 0 one channel, 2 the side doubled; a change ramps across the block), then the polarity (both
channels × −1); at their defaults the stage is skipped — the samples untouched, to the bit. A bus may be solo-safe
(`set <bus>.soloSafe=true|false`, `soloSafe=true` on its line; refused on a line that is not a bus, by the service and
by the model, `model/Project.cpp:1287`): a solo never silences it, decided in `silentStrips` with the rest
(`core/Compile.cpp:195`); its mute still does. A solo already keeps what the soloed strip feeds, so solo-safe keeps a
bus OUTSIDE that graph running — begun mid-song, a solo cuts an unsafe bus's echoes at once and leaves a safe one's. The
mono check is the listener's: `transport mono on|off` (`core/service/ServiceTransport.cpp:713`; not an edit, not in the
song, kept across plays and stops, `transport.mono`) folds each output pair to its mean at the Player — the speakers'
and every follower's (`core/Player.cpp:601`, `foldMono` `:286`) — and never reaches a render.  Tests: L2 `test_per_strip_tools`
(`core/tests/serviceTests.cpp:5953`): a synth through a reverb (L and R 0.079 apart) — phase on: the render negated
exactly; width 0: L = R exactly; width 200: the side doubled and the mid kept to 6e−8; back at the defaults the render
identical to the bit; solo-safe keeps a bus audible under another's solo; the refusals; the mono check: the speakers'
L − R exactly 0 while the render of the same hard-left strip has nothing on the right. `test_solo_safe_keeps_a_tail`
(`:5828`): a solo begun while a bus's delay repeats — its output 0 at once; solo-safe, 0.237.

Part 2 — live, and seen. While playing, a width or a phase reaches the engine as a message
(`core/service/ServiceTransport.cpp:826`; `core/Player.cpp:254`) with no rebuild: the next block ramps the width and
crosses the polarity through silence — sign 1 + (−2)(i + 1)/n — so a flip never clicks (`engine/Engine.cpp:933`);
solo-safe takes the silences path. The model: `strips[].phase`, `width`, `soloSafe` (`core/service/ServiceModel.cpp:296`).
Tests: L2 `test_per_strip_tools_live` (`core/tests/serviceTests.cpp:6502`): while playing, width 0 — the speakers'
L − R exactly 0 a block on; a polarity flip whose largest step (0.081) is no larger than the sound's own (0.084);
solo-safe set; `transport.swaps` 0 throughout; the model's fields. Engine `test_width_and_polarity_ramp_across_a_block`
(`engine/tests/engineTests.cpp:357`): a constant source (L 0.6, R 0.2) — the polarity's block is the linear crossing,
the width's the ramp to the mid, then the mid; each to 5e−8.

Part 3 — the dock. A strip's menu (not a VCA's) offers "Invert Phase" (" · on" when on), "Width ▸" (0 % (mono), 50, 100,
150, 200 — " · now" on the current) and, on a bus, "Solo-safe" — each ONE line, `set <ch>.phase=…`, `.width=…`,
`.soloSafe=…` (`app/widgets/MixerDock.cpp:1745`). The card shows them: "Ø" in the accent at its sub line's right and,
off 100, "<w>%" beside it, the sub text ellipsized to leave room (`:1869`); a solo-safe bus's S takes a ring in the
solo amber (`:2007`). Each is an eased amount (`:1027`; read by `phaseAmount`, `widthAmount`, `safeAmount`, `:532`) —
a tag fades in and out, the number it shows kept while it fades. The master has no pan, so its pan row holds the mono
check: a "Mono" pill (`monoRect`, `:547`; painted at `:2071`) whose click is `transport mono on|off` (`:1567`), lit
by an amount eased from `transport.mono` (`:1108`). Tests: UI `test_per_strip_tools_in_the_dock`
(`app/tests/ui/uiTests.cpp:5090`): each menu item sends its one line and its mark is caught mid-fade, then at 1; a
line that is not a bus offers no Solo-safe; Mono sends `transport mono on` and lights eased, the model's
`transport.mono` true. Mutants seen red: the Ø, the width tag, the ring and Mono each set at once (the mid-fade
assertions fail), Solo-safe offered on any line, Mono's click sending nothing. Shot `mixer-tools`
(`app/tests/shots/renderShots.cpp:501`) at both sizes.

### DR-VER-1 Versions of a song (R-VER-1…4; docs/discussion.md Round 15) — part 1: the model; part 2: the service; part 3: the song bar
A version is a `Version` (`model/Project.h:356`): an id, a name, a base (an earlier version's id; none = main) and its
lines — its deltas, its own nodes, and when frozen `#vsnapshot` and the song — kept as text in its `#aversion` node
(read with the song, `model/Project.cpp:672`; written after it, `:970`, main's own text first — `songText`, `:984`, is
the song's nodes alone). The open version is `Project::version`; while one is open the nodes ARE its resolution and
`mainText` holds main's, which an edit on a version never touches. Validation (`:1243`): names one each, never `main`;
a base is main or an EARLIER version, so no chain loops; a frozen version holds its snapshot. `nextId` (`:217`) also
reads main's text and every version's lines, so an id a version holds is never given again — not even one only main
still has after the open version dropped it.

`model/Versions.cpp` reads a song's canonical text as nodes — the table it shares with the merge (`readTable`,
`model/NodeTable.cpp:93`; `keyOf`, `:62`):
the header (`project`), every node with an id, a rack (`rack:<track>`), a formula (`bind:<address>`); values kept as
written; a node's id-less lines are its body, a rack's devices its children; comments kept, never versioned. A
resolution (`resolveText`, `model/Versions.cpp:241`) is the base's, recursively, with the deltas applied (`applyVersion`, `:105`): drops,
field sets and unsets, bodies, then its own nodes, then the orders — a base child no order lists is not in it; a frozen
version reads its snapshot. A delta whose node the base lacks is dangling: ignored and listed. **The deltas are worked
out, never written by a command:** `recordVersion` (`:365`) compares the song now with the base's resolution
(`diffTables`, `:185`) — each node in the base's order: gone (`#vdrop`), fields changed or added (`#vset`), fields gone
(`#vunset`), body (`#vbody`), children (`#vorder`); then the nodes of its own in full — and keeps the dangling ones. So
an inherited node edited stores one `#vset`, never a copy, and a field set back to the base's value inherits again.
`openVersion` (`:332`) parses a resolution — one that does not stand as a song (the base broke it) is refused, naming
why; `versionFacts` (`:315`) counts deltas, own nodes and dangling ones and says why one does not stand. `newVersion`
(`:381`); `freezeVersion` (`:403`) keeps the resolution as a snapshot; `thawVersion` (`:417`) drops it; `rebaseVersion`
(`:427`) names the dangling deltas and prunes them, or advances a frozen version's snapshot to its base now with its
deltas on top; `deleteVersion` (`:461`) is refused while another is based on it; `revertVersion` (`:475`) drops the
open version's deltas on a node or a field, refused on main, a frozen version, a node of its own and where there are none.

Tests (`solaris_model`): `test_a_version_stores_what_it_changes_and_inherits_the_rest` (`model/tests/modelTests.cpp:758`):
a mute and a gain stored as ONE `#vset`; main's text untouched and first; the file a fixed point; main's later pan
arrives, its gain does not; a field edited back inherits again. `test_every_kind_of_change_resolves_to_the_song_it_was`
(`:793`): a drop, a field back to its default, a pattern's notes, a rack's order, its own device and clip, the header —
resolved, in memory and through the file, to exactly the song it was; untouched nodes store nothing.
`test_inherit_live_override_wins_dangling_kept_until_rebase` (`:836`), `test_freeze_thaw_and_rebase_a_frozen_version`
(`:871`), `test_versions_chain_are_checked_and_never_reuse_an_id` (`:899`): a version of a version, the refusals, an id
never reused, revert, a version its base broke named and not opened. Mutants seen red (9): copy-on-edit (every field
stored); a set replacing the node's fields; dangling deltas dropped by an edit; frozen ignored; ids reused across
versions (a new clip took the dropped clip's id); a field back to its default, a rack's order, not recorded; a base after
its version accepted; a rebase that prunes nothing.
Part 2 — the service. The grammar is Interstellar's verbs (`core/service/Command.cpp:272`): `version new <name> [--base]
[--open]`, `version list` (the tree, each with its changes, own nodes, frozen, dangling, and why one does not stand),
`version open <version|main>`, `version diff`, `version freeze|thaw`, `version rebase [--dry-run]`, `version delete`,
`revert <address|node>`, `set <vr>.name=` (`core/service/SolarisService.cpp:1018`), and `render --version`
(`core/service/ServiceRender.cpp:150`: the version's resolution rendered whichever is open). They live in
`core/service/ServiceVersions.cpp` (`versionCommand`; `version new`, `:31`; `version open`, `:66`; `revert`, `:116`, an
address's field being its key in the file). **One hook makes every command version-aware** (`core/service/SolarisService.cpp:371`):
once an edit has landed — validated — on an open version, `recordVersion` works its changes out again; what works on a
version itself (`versionOnly`, `:106`: the version verbs, a `set` of a version's name) is never recorded as its change.
A frozen open version refuses an edit and a recording, naming the way out (`:171`). `version open` is an edit — a step of
the history, all-or-nothing, heard while playing (an engine rebuilt, like any structural edit) — but not an unsaved
change (`:437`): the file never says which version is open, and a song opens on main. The model: `versions[]` (id,
name, base, frozen, changes, own, dangling, problem) and `version` (`core/service/ServiceModel.cpp:407`; `VersionModel`,
`core/service/AppModel.h:330`); the Addresses table's `<version>.name` (`core/service/ApiDoc.cpp:161`). Tests: L2
`test_versions_of_a_song` (`core/tests/serviceTests.cpp:6065`): made and opened in one line; muting there is ONE change;
main renders at peak 0.633 and Instrumental at exactly 0, each by name whichever is open; main's later gain arrives, the
mute stays; undo goes back to main, redo to the version; saved, then opened main: not unsaved; the file opens on main;
opened while playing, silent a quarter-second on; frozen: an edit and a recording refused, its name still set; thawed,
main's gain arrives; revert drops the mute; a dangling move named by a dry run and pruned; the tree; the names and the
refusals. Mutants seen red (8): an edit on a version not recorded; a frozen version taking an edit; opening marking the
song unsaved; opening not a history step; renaming a version counted as its change; render ignoring `--version`;
dangling not published; opened while playing, not heard.
Part 3 — the song bar. After the menus, the version chip (`app/widgets/SongBar.cpp:123`, at `chipX`, `:49`, so the song's
name follows it — the chip sits before the name, not after, so that its place never waits on the name's measured
width): "main", or the open version's name with " · frozen" when it is (`:75`), and a ▾. A click drops its menu from
under it (`:286`; the App hosts it, `app/App.cpp:79`), built by `versionMenu` (`app/widgets/SongBar.cpp:301`): the
versions as a tree under main, four spaces a level, the open one, a frozen one and one that does not stand each said —
a row is `version open <id|main>`; New Version… is cosmo's field, then ONE `version new "<name>" [--base <open>]
--open`; with a version open, Freeze / Thaw, Rebase, Rename… (`set <vr>.name=`) and Delete Version. Nothing snaps
(`:184`): the label cross-fades (the old one fading out under the new), the width eases to the new label's (measured in
the paint, `:378`, eased from the next frame), the song's name and the transport sliding with it, and an accent ring
eases in while a version, not main, is open — whoever opened it. Tests: UI `test_versions_from_the_song_bar`
(`app/tests/ui/uiTests.cpp:5156`): the chip says main; its menu is the tree and New Version…, Freeze absent on main;
"Radio edit" typed sends ONE `version new "Radio edit" --open`; the label's cross-fade, the ring and the chip's width
each caught mid-way, then at rest (wider than "main"); on the version, its menu offers Rebase, Rename…, Delete Version;
Freeze sends `version freeze vr_1`, said on the chip, mid-fade; main opened from a shell eases the ring out; the tree's
row opens the version. Mutants seen red (6): the label, the ring, the width each set at once; New Version not opened;
the tree flat; the chip's click doing nothing. Shots `song-versions` (the menu open on a version of a frozen one) and
`song-versions-mid` (main coming in), both sizes (`app/tests/shots/renderShots.cpp:511`).

### DR-COLLECT-1 Collect and save; relink (R-COLLECT-1; docs/discussion.md Round 16) — part 1: the service; part 2: the window
**What a song uses** is read from the whole document's text (`fileRefs`, `core/service/ServiceFiles.cpp:92`): every
`src`, `sample` and `frozen` value — an audio clip and its takes, a device's sound or `.sfz`, a frozen strip's render —
in main, in each version's changes and own nodes, and in each snapshot, by where it resolves, with what names it (a
version's ids prefixed `vr_n:`). `project files [--missing]` lists them (`:178`): in the song's folder, outside, or
missing. **Collect** (`:191`): each file outside the folder that can be read is copied into `<folder>/Samples/` — never
over a different file nor onto a name this collect already gives another ("kick 2.wav", `freeName`, `:77`), the same
bytes already there reused; an `.sfz` into `Samples/<its name>/` with every sound it names, their layout kept (`:208`;
one naming a sound outside its folder is left, said; a missing sound is said and not copied). The copies are made only
once all is decided; then every reference is rewritten in the whole document's text and the song read back, the
version that was open opened again (`rewriteFiles`, `:126`) — ONE edit, one undo (the copies stay). A missing file is
named and left. **Relink** (`:270`): `project relink <file> --to <new>` points every reference to it at the new file,
`project relink --folder <dir>` looks for every missing file by its name under the folder (the first by path order;
several found, said) — both across the whole document, ONE edit; `clip relink <ac> <file> [--take n]` (`:321`) one
clip of the song as open (on a version, its change). Files outside the song's folder stay absolute, as ever; inside,
relative. The grammar is `core/service/Command.cpp:297`. Collect and the song-wide relinks work on the whole document,
so they are allowed while a frozen version is open and are not that version's change (`versionOnly`,
`core/service/SolarisService.cpp:111`); `--save` saves once the collect has landed (`:427`) — the UI's one line. A
`--dry-run` is never an edit, whatever the verb (`:162`; D-23). The model's `missing[]` (`core/service/ServiceModel.cpp:426`;
`MissingModel`, `core/service/AppModel.h:394`): each file named that cannot be read, and what names it.
Tests: L2 `test_collect_and_relink` (`core/tests/serviceTests.cpp:6175`), with real files outside the song's folder —
listed (a version's own clip as `vr_1:ac_4`, the missing hat); a dry run copies nothing and leaves the song saved, as a
rebase's dry run does; collected: a stranger's `Samples/kick.wav` left alone, the two `kick.wav` as "kick 2" and
"kick 3" with their own bytes, the `.sfz` and its two sounds (the third said missing), the version's snare; every
reference rewritten, the version's too; the hat named and left; one undo takes it back, the copies stay; collecting
again: nothing to collect; a folder search finds the hat; a file relinked across versions; a clip relinked as the open
version's change; the refusals; Collect and Save — the same bytes reused, saved. Mutants seen red (9): a different
file written over; the same bytes not reused; a version's files not seen; its lines not rewritten; an `.sfz`'s sounds
not copied; a dry run an edit; `--save` not saving; the folder search finding nothing; `missing` not published.
Part 2 — the window. File › Collect and Save is ONE `project collect --save`, then a toast says it and what is still
missing (`app/App.cpp:170`). File › Relink… (`:178`) asks `project files --missing` first — a file may have gone since
the last command, and the model's `missing[]` is worked out when a command runs — then opens the Relink window
(`RelinkSheet`, `app/widgets/RelinkSheet.cpp:26`): Cosmo's modal look, a row per missing file — its name, the folder it
was in and what uses it, a version's ids prefixed `vr_n:` — and Locate…, the host's file picker then ONE `project relink
<file> --to <picked>`; Search a Folder…, the host's folder picker then ONE `project relink --folder <dir>`
(`app/App.cpp:104`; the host's dialogs, `onPickFile`/`onPickDirectory`, `linux_main.cpp:440`). The window draws the
model: rows keyed by path (`app/widgets/RelinkSheet.cpp:108`) — one found fades out where it is, the rest slide up, one
newly missing fades in, the rows present when it opens placed with it; the card grows and shrinks with them, eased
(`:140`); the list clips and scrolls with the wheel, its bar shown only when there is more (`:235`); with nothing
missing it says so. A song opened with files missing says so in a toast, once (`app/App.cpp:579`). Tests: UI
`test_relink_and_collect_from_the_file_menu` (`app/tests/ui/uiTests.cpp:5224`): opened with two files gone — the
toast; File › Relink… asks first, fades in with its rows already placed; Locate… on the kick: the host asked "Locate
kick.wav", ONE line sent, the row fading out, the hat sliding up and the card shrinking, each caught mid-way, then at
rest; Search a Folder…: ONE line, nothing missing; Done closes; Collect and Save: ONE line, said, saved. Mutants seen
red (9): a found row, the next row's slide, the rows on open and the card each set at once; Locate and Search sending
nothing; no toast on open; Collect and Save not saving; Relink… opening nothing. Shots `relink-window` (three moved, a
version's clip among them) and `relink-found-mid`, both sizes (`app/tests/shots/renderShots.cpp:819`).
### DR-LAUNCH-1 A clip launcher (R-LAUNCH-1; docs/discussion.md Round 17) — part 1: the grid; part 2: launching, heard; part 3: recorded; part 4: the session view
A scene is a row of the session grid and a slot a cell — what a strip plays when it is launched in that scene
(`Scene`, `Slot`, `model/Project.h:305`, `:292`): a pattern (an instrument strip's, shared as a clip's is) or a file (an
audio strip's, `in`/`out` as a clip's), with the beats a loop lasts (`length`; absent, the pattern's or the file
span's), a `name` and a `gain`. Both are the song's nodes, written after the arrangement (`#ascene`, `#aslot`,
`model/Project.cpp:768`, `:1023`); the header's `launchQuantize` (bar, beat or none; written only when not a bar,
`:693`) says where a launch lands. A slot is its own node, so nothing that reads the arrangement sees it; versions and
collect see it as any node. The validator (`:1253`) refuses a slot in no scene, for no strip, of a strip that cannot
play it (a pattern for an instrument strip, a file for an audio one), of both or neither, and a second slot in a cell.
The grammar (`core/service/Command.cpp:313`): `scene add [<name>] [--at <i>]`
(`core/service/ServiceSession.cpp:255`; the rows renumbered 0 … n−1, `renumber`, `:21`), `scene delete` (its slots with it) and `scene move --to`
(`:269`), `slot add <sc> <strip> (--pattern | --clip | --src) [--length]` (`:288`; `--clip` takes a clip's pattern or
file and its length; a file measured now, as `clip add` measures one), `slot delete`; `set <sc>.name=`,
`set <sl>.name|length|gain=` and `set project.launchQuantize=` (`core/service/SolarisService.cpp:1116`, `:983`). A
strip's slots count as its clips: `strip delete` is refused while they play through it, unless `--with-clips`
(`core/service/ServiceEdit.cpp:272`); a pattern a slot plays is not deleted (`core/service/ServiceCompose.cpp:641`).
The model: `scenes[]` by order, `slots[]` row by row in processing order with each loop's length resolved, and
`launchQuantize` (`core/service/ServiceModel.cpp:413`; `SceneModel`, `SlotModel`, `core/service/AppModel.h:340`).
Tests: `solaris_model` `test_scenes_and_slots_round_trip_and_are_checked` (`model/tests/modelTests.cpp:961`): a fixed
point, launchQuantize only when not a bar, the seven refusals; L2 `test_the_session_grid`
(`core/tests/serviceTests.cpp:6275`): scenes placed and moved, slots of a pattern, a clip and a file, their lengths
resolved, the refusals, names and lengths set, a strip's slots refused then deleted with it, a played pattern held, one
undo, launchQuantize, a version's own slot. Mutants seen red (6): a cell holding two; a strip's slots outliving it; a
slot's length not resolved; scene move not placing; launchQuantize not written; a slot's kind not checked. (Equivalent:
`pattern delete` not counting slots — the validator then refuses it, naming the slot, the same.)
Part 2 — launching, heard. **The same graph** (R-PLAY-1): the compile puts every slot of a strip in it
(`core/Compile.cpp:535`; `SlotPlay`, `engine/MixGraph.h:97`) — a pattern slot's notes from its own start (a pattern
shorter than its slot repeats in it, a note cut at the slot's end, chance rolled per repetition; at the song's first
tempo: a slot does not follow a tempo map's changes yet), an audio slot's file span and level. `Engine::launch`
(`engine/Engine.cpp:437`; by the slot's id, `launchById`, `:447`, as a message carries it, no allocation) and
`launchBack` (`:457`) leave ONE change pending a strip; it lands on its sample (`:695`): what the instrument holds ends
there (`allNotesOff` — a held note ends exactly as its own note-off on that sample would), then the slot plays from its
base, looped, or nothing (a stop), or the arrangement again. An instrument strip's piece merges, in time, the change,
the slot's notes and — only while it follows the arrangement — the arrangement's notes and wheels; while it is the
session's they pass unheard and are never replayed at a return (`:789`; a note held across a return is not restarted,
as after a seek). An audio strip's piece splits where the change lands: the arrangement's regions, or the slot's file
from its base, a file shorter than its loop leaving the rest silent (`:725`). A slot keeps its own phase through a jump
(`seek`, `:519`: a song loop does not restart it; a change waiting lands at once), and a swap carries the session into
the new engine — strips and slots matched by id, its phase kept (`adoptSession`, `:473`; `core/Player.cpp:273`). The
Player's `Launch` and `LaunchBack` messages (`:264`). **The service** quantizes (`launchBeat`,
`core/service/ServiceSession.cpp:39`): the next bar (or beat, or none) after what the engine has rendered plus two
blocks — never in the past. `launch <sl|sc>` (`:330`; a scene's slots row by row; stopped, the transport starts and
they play at once), `launch stop <strip|all>` and `launch back [<strip>]` (`:374`) — live, not edits, not in history
(`core/service/Command.cpp:322`). What was due lands in the model when it is heard (`advanceLaunches`, `core/service/ServiceSession.cpp:51`, called by
`pump`, `core/service/ServiceTransport.cpp:176`); `transport.launched[]` (`launchedModel`,
`core/service/ServiceSession.cpp:76`; `LaunchModel`, `core/service/AppModel.h:474`): per strip, whether its launch has
landed, the slot playing, what lands next and on which beat, and how far through its loop it is. A stop ends the
session (`core/service/ServiceTransport.cpp:53`); a seek lands what waits (`:687`). Tests: engine
`test_a_launch_lands_on_its_sample_and_loops` (`engine/tests/engineTests.cpp:881`): an audio slot from 2500, looped
every 1000 (its file, then silence), stopped at 3600, the arrangement again at 4000 — to the sample; its phase through a
seek; a swap carrying it; an instrument's launch and return equal, sample for sample, the same notes placed in the
arrangement (a held note ended on the launch's sample, the session's arrangement notes never replayed); nothing
allocated. L2 `test_launching_is_heard` (`core/tests/serviceTests.cpp:6332`): launched half a beat in, it lands on beat 4
— the speakers' A2 0.089 → 0.001 and F♯5 0.001 → 0.087 across the bar; through an engine swap (a device added) it plays
on; a stop silences it on its bar, back returns the arrangement on its bar; the model says what lands and when; not an
edit; a render is the arrangement; a stop ends the session; a scene launched while stopped starts the transport.
Mutants seen red (10): a launch landing at once; the arrangement's notes replayed at a return; a jump restarting the slot;
a swap dropping the session; what sounds going on past a launch; a launch not quantized; a stop playing the slot; nothing
landing in the model; a stop keeping the session; launching an edit.
Part 3 — recorded. `transport record` records the session too — with nothing armed, only it, when the song has a grid
(`core/service/ServiceTransport.cpp:458`). While recording, each landing closes the strip's span and a slot landing
opens the next (`core/service/ServiceSession.cpp:59`); the stop lands what was heard by then (`ServiceTransport.cpp:639`)
and commits (`:667`; `commitLaunches`, `core/service/ServiceSession.cpp:99`), all ONE edit with the take, "Record": for
each span the strip's own clips under it give way (`:133`) — one inside goes; one across the span's start ends there;
one across its end starts there, a note clip into its pattern by what it skipped (`offset`), an audio clip into its file
(`in`); a looped audio clip cut at its start restarts its loop (not yet otherwise) — and the span lands on the strip's
lane (an instrument's track, else where its clips are, a new lane when there is none) as clips (`:176`): ONE when the
slot's loop is its pattern's, else one a loop (a slot plays the start of a longer pattern, or a shorter one repeated);
an audio slot's file as a clip a loop, none over the silence after a short file. **A note clip may start into its
pattern**: `offset=` beats (`model/Project.h:230`; written only when set; refused on an audio clip, `model/Project.cpp:1382`;
`set <ac>.offset=`, `core/service/SolarisService.cpp:1278`) — the compile reads its notes and wheels in pattern time and
moves them back (`core/Compile.cpp:482`); the timeline draws its repetitions shifted (`app/widgets/Timeline.cpp:1463`).
Tests: L2 `test_launches_are_recorded_into_the_arrangement` (`core/tests/serviceTests.cpp:6430`): refused with nothing
armed and no grid; recorded with nothing armed — F♯5 launched on beat 4, the arrangement back on 8: the A2-A2-C3 clip
(0–32) becomes 0–4 and 8–32 starting 8 beats into its 3-beat pattern (on its C3), F♯5's clip on its lane between; the
song then renders, over the nine beats, exactly what the speakers got (largest difference 0); one undo takes it back
whole; a 1-beat slot of a 2-beat pattern lands a clip a loop, each its first beat. Mutants seen red (6): a clip's offset
not played; the arrangement not cut under a span; a span not closed when the next lands; one clip for a slot shorter
than its pattern; the tail starting at its pattern's start; nothing recorded with nothing armed.
Part 4 — the session view. View › Show Session (`app/App.cpp:255`) cross-fades the grid into the lanes' place — never
a cut (`app/widgets/ProjectScreen.cpp:236`; the one going takes no input, `:199`). `SessionView`
(`app/widgets/SessionView.cpp:31`) draws only the model: a column per strip that can hold a slot (instrument and audio,
in processing order — its colour and name), a row per scene (its name and ▶), a slot's name in its cell; it clips and
scrolls both ways (the wheel; Shift for sideways), eased, its bars only when there is more. Every gesture is ONE line
(`:185`): a slot → `launch <sl>` (stopped, the transport starts and it plays at once); an empty cell → a menu of the
song's patterns (an instrument's column) or the strip's clips (an audio column) → `slot add` (`:170`); a scene's ▶ →
`launch <sc>`; a column's ■ → `launch stop <ch>`; Back to Arrangement → `launch back`; "+ Scene" → `scene add`; a slot's
or a scene's right-click: Rename…, Delete. Nothing snaps (`:116`): a slot is keyed by id — playing, it fills with its
strip's colour, eased in and out, its loop's progress a bar along its foot (the model's, frame by frame, `:306`); queued,
its outline pulses until it lands (a timing, not a tween), the pulse itself fading in and out (`:287`). Tests: UI
`test_the_session_view` (`app/tests/ui/uiTests.cpp:6006`): View › Show Session caught mid cross-fade; an empty cell's
"Lead" → `slot add sc_1 ch_2 --pattern pt_2`; "+ Scene"; the slot, stopped → `launch sl_1`, the transport playing, its
fill caught mid-way; another, launched while playing → queued (its pulse mid-way, not filled), the first still playing;
■ → `launch stop ch_2`, the pulse fading out; the scene's ▶, Back to Arrangement, a slot's Delete; Show Arrangement.
Mutants seen red (7): the view cut in; a playing slot's fill and a queued slot's pulse each set at once; an empty cell
offering nothing; a slot's click and a column's ■ sending nothing; a queued slot drawn as playing. Shots `session-view`
(Verse playing, the bass's Drop queued), `session-view-mid`, `launch-recorded` (a recorded launch's arrangement: the
tail clip starting into its pattern), both sizes (`app/tests/shots/renderShots.cpp:532`).
### DR-VIDEO-1 A video lane (R-VIDEO-1; docs/discussion.md Round 18) — part 1: the service and the host; part 2: the window; part 3: the lane and the export
One picture a song (`Video`, `model/Project.h:329`): a file and an `offset` — seconds on the song's clock (the tempo
map's) at which its first frame shows; negative, it began before the song. It is a node like any (`#avideo`, read at
`model/Project.cpp:770`, written after the session grid at `:1163`), so versions and collect see it as one; the
validator (`:1384`) refuses a second picture ("one picture a song") and one naming no file.
The grammar (`core/service/Command.cpp:331`): `video set <file> [--offset <s>]` (`core/service/ServiceVideo.cpp:92`) —
the file named as a clip's is (in the song's folder by its name, else absolutely), read by the host's probe first: a
file that is no picture is refused, naming why ("cannot read song.wav as a video: it has no picture"); another picture
in its place keeps the id and, unless told, the offset; `--offset` −36000 … 36000. `video remove` (refused with none).
`set vd_1.offset=` and `get` (`core/service/SolarisService.cpp:1105`, `:1478`). Every one an edit — all-or-nothing,
undone as one.
The model says where the picture is (law 13), `video` (`core/service/AppModel.h:379`): its file, offset, length, size,
frame rate, `offline` when the host cannot read it (the song still opens) — the probe kept per resolved file
(`core/service/ServiceVideo.cpp:17`) — and `time`, the picture's seconds at the heard position through the tempo map
(`videoTime`, `:31`; −1 before it starts or after it ends). Filled with the model at the time bar when stopped
(`core/service/ServiceModel.cpp:109`), and moved by `pump` with the playhead while playing — each frame the window
pumps, no command between (`core/service/ServiceTransport.cpp:178`). With it, where the picture lies on the song's beats —
`at` and `end`, through the tempo map; begun before the song, at beat 0's tempo (`core/service/ServiceVideo.cpp:64`) —
and `path`, the file as the host opens it, so a front end never resolves a path or a tempo itself.
The host reads it (R-SVC-4 — the core carries no codec; `host/VideoFiles.cpp`, FFmpeg as the audio's): `probeVideo`
(`:77`) — the file's picture is its best video stream that is no cover image (`:29`); a still image is no video
(`:56`). `VideoReader` decodes the frame showing at a time, fitted to a box (its aspect kept) as RGBA through
libswscale (`:189`): forward from the frame showing, one frame ahead so where it ends is known (`:122`), as playing
does — never seeking; back, or more than two seconds ahead, from the keyframe before (`:153`). `muxVideo` (`:237`)
writes the export: the picture's own stream COPIED, never re-encoded, its first frame `startAt` seconds into the file
— begun before, it is cut at the first keyframe from there (`:364`) and an open GOP's lead-in dropped (`:370`), so
every frame kept decodes and shows at its time; cut where the mix ends in decode order (`:359`), so a B-frame's forward
reference stays; the mix encoded beside it (AAC in .mp4 / .mov, FLAC in .mkv), interleaved as it goes (`:318`).
`render --out <f.mp4|.mov|.mkv> --video` (`core/service/ServiceRender.cpp:79`): the mix rendered as any (undithered);
the picture's `startAt` is its offset less where the render starts (`:314`), from the version rendered (`:159`).
Refused without a picture, for an audio extension, and with --format, --stems, --ports, --bits or --kbps. The CLI, the
window and the web face's host give the service the probe and the mux (`cli/main.cpp:125`, `linux_main.cpp:70`,
`cli/NtwbMain.cpp:159`).
Tests: model `test_the_picture_round_trips_and_is_checked` (`model/tests/modelTests.cpp:1008`): a fixed point (a quoted
file, a negative offset, an unknown key kept), its id counted, a second or one naming no file refused. L2
`test_a_video_lane` (`core/tests/serviceTests.cpp:4702`; a fake host's probe and mux, `:464`): set (a sound refused, an
offset out of range refused), the .slp, the event; `video.time` −1 before it, 2 s at beat 8, 4 s through a 60-bpm
change, −1 past its ten seconds; the offset set, got and refused; another picture keeps id and offset, undone; playing,
the time moving with the playhead — pumped as the window pumps, the heard position's to 1e-9; `render --video` handing
the mux the mix and `startAt` 2 (from 0) and −2 (from beat 8); the refusals; a picture gone, offline; removed, undone.
L3 `solaris_video` (`tests/faces/videoTests.cpp`, registered where `ffmpeg` and `ffprobe` are found): clips FFmpeg makes
— a second of red then blue, 64×48 at 25 fps, a keyframe every fifth; a B-frame cut of it; 14 s of test pattern —
probed (a WAV, a PNG and a text file refused, `:109`); frames at times (`:127`): 0.5 s → frame 12, red, 32×24 in a
32×32 box; 0.999 → frame 24; 1.0 → frame 25, blue; played frame by frame with no seek, back with one, past the end the
last; muxed (`:157`): from 0.5 s, `ffprobe` reading the picture from 0.500 for 2.000 s beside a 3.0 s AAC; begun 0.9 s
before, 25 frames from 0.100, the first blue; a 1.5 s mix, 38 frames; the B-frame cut decoding every frame it keeps,
packets = frames, the first decoded the keyframe at the file's start; 14 s interleaved within 0.04 s; refusals; and
through `solaris-cc` (`:208`): `video set`, `render --video` an MP4 from 1.0 s for 2.0 s beside AAC, `--from 4` an MKV
of the blue half, nothing on stderr. Mutants seen red (21): the tempo map ignored; the pump not moving the picture;
past its end still shown; render ignoring --from; a replace forgetting the offset; no probe on set; offline never said;
--video with --stems; a second picture allowed; a picture naming no file allowed; its id not counted; a cut on any
frame, not a keyframe; no end cut; an end cut by presentation time (a B-frame's reference dropped); the offset not
applied; the frame after the time; a seek every frame; stretched, not fitted; a still image taken for a picture; no
interleave; an open GOP's lead-in kept.

Part 2 — the window. View › Show Video / Hide Video (`app/App.cpp:256`; the window's × relabels it) opens the Video
window (`app/widgets/FloatWindow.cpp:292`, titled by its file, `:462`): `VideoPanel` (`app/widgets/VideoPanel.h:39`)
draws the frame at the model's `video.time`, letterboxed on black, from the HOST's decoder — `App::onVideoFrame`
(`app/App.h:102`; the GTK host's `VideoReader` on the song's file, `linux_main.cpp:417`) — asked only when the time, the
file or its box changed, at the box's size (`app/widgets/VideoPanel.cpp:66`). Outside the picture's span it says where
the song is ("Before the picture" / "After the picture", from `at`), or "No picture in this song", or that the file
cannot be read. Nothing snaps: the picture fades in and out over the words (`:119`); a jump — more than half a second,
or another file — cross-dissolves from the frame that was, 160 ms linear (`:91`); while playing, frame follows frame
(the picture's own motion); the words cross-fade (`:74`); a frame of the same size is re-uploaded, not re-registered
(`:131`). The foot names the file, its size and rate, and the picture's time in mono. Tests: UI `test_the_video_window`
(`app/tests/ui/uiTests.cpp:5317`; the rig's decoder records every ask, `app/tests/Rig.h:331`): the menu, the window caught
mid fade-in; before the picture said and nothing asked; at beat 4 one ask, for second 1 at the box's size, the picture
caught mid fade-in, letterboxed 16:9 and centred; a jump caught mid-dissolve, a step not dissolving; nothing asked when
nothing changed; playing, asks in order; resized by its corner, an ask at the new size; past the end, the picture
fading out and the words cross-fading to "After"; removed, "No picture"; Hide Video, Show again, × relabelling. Mutants
seen red (11): the picture shown at once; no dissolve on a jump; a step dissolving too; an ask every frame; an ask at
the picture's size, not the box's; before and after swapped; the words snapping; × leaving the menu saying Hide; a
resize not re-asking; and in the service, a picture begun before the song placed at no tempo, its end without its
length. Shots `video-window` (the rig's picture at the time bar: a dusk sky whose amber band's edge is the frame's time),
`video-window-mid` (a jump mid-dissolve: both bands), both sizes (`app/tests/shots/renderShots.cpp:576`). Owed: a live
look at the GTK window's decoder on a real file.
Part 3 — the lane and the export. The model publishes a point of the picture every second of it (every length/240
for a long one) on the song's beats — `video.thumbs`, through the tempo map built once (`core/service/ServiceVideo.cpp:70`)
— and takes a beat for its start: `set <vd>.at=<beat>` moves the offset through the map, before the song at beat 0's
tempo (`core/service/SolarisService.cpp:1110`; `videoSecondsOf`, `core/service/ServiceVideo.cpp:83`), so the lane
neither keeps nor inverts a tempo map. The timeline gains the picture's row between the ruler and the lanes, pinned as
the ruler is (`app/widgets/TimelineVideo.cpp:74`): it grows in when the song gains a picture and out when it loses it
(`:79`), the lanes sliding with it — every row's top is `top()`, the ruler's height plus the row's LIVE height
(`app/widgets/Timeline.h:267`). Its span is the model's `at` … `end`, eased there when moved elsewhere (`app/widgets/TimelineVideo.cpp:96`); its
thumbnails are tiles at the model's points, each at least a tile past the last chosen from the span's start (the same
tiles wherever it scrolls, `TimelineVideo.cpp:51`), decoded by the host at the row's height a few a frame (`TimelineVideo.cpp:128`) — a decoder of their
own, `App::onVideoThumb` (`app/App.cpp:91`; the GTK host's second `VideoReader`, `linux_main.cpp:437`) — kept, each
fading in as it arrives; the file's name pinned at the span's left (`paintVideo`, `app/widgets/TimelineVideo.cpp:145`). Dragged, the span is the
pointer's on the grid you see, before the song too, and nothing is sent; let go, it stays where it was let go
(`TimelineVideo.cpp:240`) and ONE `set <vd>.at=<beat>` goes (`videoGesture`, `TimelineVideo.cpp:207`). File › Export… gains "With the video"
(`app/widgets/ExportSheet.cpp:115`): greyed and inert while the song has no picture (`:140` — the sheet never jumps); on,
the Format is MP4 or MKV, the bit depth, quality and stems grey out, and the line is `render --out <f> --video …`
(`:153`); off, the audio format it was left at. Tests: L2 grew `set vd_1.at=6` (3 s at 120 bpm; get 6.0; eleven points
from beat 6 to 26; −2 → −1 s; a beat too far refused) (`core/tests/serviceTests.cpp:4745`). UI
`test_the_video_lane` (`app/tests/ui/uiTests.cpp:5432`; the rig's thumbnail decoder logs apart, `app/tests/Rig.h:335`):
the row caught growing with the first lane at `row0 + a·kVideoH`, a thumbnail caught fading in, at most three asked a
frame; the span on beats 2 … 22; every ask on a model point, at the row's height, none twice; dragged two beats — the
pointer's, nothing sent — then ONE `set vd_1.at=4`, offset 2 s, nothing moving after; a shell's offset eased there;
removed, the row caught closing and the lanes back. UI `test_export_with_the_video` (`app/tests/ui/uiTests.cpp:5663`): greyed and inert with
no picture; on, MP4 / MKV, the rows greying caught mid-way, `--video`; MKV and −14 LUFS → `render --out "…/Scored
out.mkv" --video --normalize -14 --ceiling -1`; off, WAV again. Mutants seen red (16): the row shown at once; the lanes
not moving down; a drag sending while held; a jump back after the drop; a move made elsewhere snapping; thumbnails
asked again; thumbnails at the picture's size; thumbnails popping in; every thumbnail in one frame; the drag teleporting
to the pointer; stems still applying with the video; the line keeping --format; off, not the format it was left at; the
toggle without a picture; `at` taken as seconds; the points at seconds, not beats. Shots `video-lane` (the row's
tiles, each band's edge its own time), `video-lane-dragging` (the span held two beats on, ringed), `export-with-video`
(MKV, the greyed rows), both sizes (`app/tests/shots/renderShots.cpp:599`, `:1616`).

### DR-COLLAB-1 Sharing a song through a folder (R-COLLAB-1; docs/discussion.md Round 19) — part 1: the merge in the model; part 2: the service; part 3: the window
Two people's edits to one song are merged node by node, in the model, pure — text in, text out (`mergeSongs`,
`model/Merge.cpp:229`; `model/Merge.h:38`). The three songs — the BASE (what this window last read or wrote), MINE and
THEIRS — are read as the versions read a song: one table, shared (`model/NodeTable.h:18`): the header field by field
(`model/Merge.cpp:253`), every node by its id, a rack by its strip, a formula by its address, a node's id-less lines
(a pattern's notes, a clip's takes) its body — one unit — a rack's devices its children. Each node, and each header
field, is decided three ways (`decide`, `:168`): changed on one side only → that side; the same on both → once; changed
on both differently → a CONFLICT ("both changed"); changed on one and removed on the other → a conflict ("changed here,
removed there" / "removed here, changed there"); removed on one and untouched on the other → removed. A conflict keeps
mine in the text and is listed (`MergeConflict`, `model/Merge.h:22`): its key (`project:<field>` for the header), what
it is, said ("strip Bass", "pattern Line", "rack of ch_1", "the song's sig"), how, and both sides' lines. A part kept
whose whole is gone — a device turned here, its strip and rack deleted there — keeps its whole too, and the whole is
asked (`model/Merge.cpp:313`). The same id added on both sides as different nodes is no conflict (`:234`): mine is
given the next free id past every side's (`nextFree`, `:148`) and every reference to it among my nodes is rewritten —
its own key and a rack's or a formula's that carry it, a value, a list, an address in a formula, whole ids only (`ch_30`
is not `ch_3`, `renameIn`, `:81`), never a file's name or a name (`:97`; `renameNode`, `:118`). An order — the song's
nodes, a rack's devices — follows the side that changed it (theirs when both did), the other side's own after their
neighbours there (`mergeOrder`, `:204`). Two sides' new places at one `order` (two strips of one mixer, two lanes,
scenes, mixers or ports) are made 0…n−1 again, the earlier in the file first, only where two share one (`tidyOrders`,
`model/Project.cpp:1658`). A conflict answered "theirs" puts theirs' node — its children with it — where mine had it,
else after its neighbour in theirs, or takes it away where theirs has none; a header field likewise
(`takeTheirs`, `model/Merge.cpp:347`). The versions read the song through the same table (`model/Versions.cpp`, which
keeps its deltas, resolution and recording). Tests: model `test_two_peoples_edits_merge_node_by_node`
(`model/tests/modelTests.cpp:1030`): a base of Drums and Bass; mine — the drums down and their synth turned, a Pad with a
rack, lane, clip and two formulas (one reading `ch_30`), the bass's eq swapped for a compressor, the line's notes, 128
bpm, 3/4; theirs — the drums deleted, the bass down, its eq turned and a gate after it, a Lead under the same new ids,
the line's pattern and clip deleted, 128, 6/8: merged, one side's changes taken and 128 once; four of mine renamed
(`ac_3`, `ch_3`, `dv_4` → `dv_10` past their `dv_9`, `ln_3`) with the clip's strip and lane, the rack's key, both formulas
— `ch_30` and the name "ch_3 pad" untouched; the compressor after its neighbour beside their gate; six conflicts — the
meter, the line's pattern, the drums' strip, their synth and their rack (kept whole), the eq — each said; the two new
strips' orders tidied to 2 and 3, and again a no-op; answered theirs, the pattern goes, the meter is 6/8, the eq comes
back after its neighbour; nothing changed on one side gives the other exactly. Mutants seen red (12): both changed taken
as theirs; changed against removed — removed; removed against changed — removed; no renaming; part of an id renamed; a
name renamed; my additions at the end; a kept part's whole let go; a node answered "theirs" put at the end; a rack's key
not renamed; orders not tidied; the next free id not past theirs.
Part 2 — the service. The BASE is what this window last read or wrote: the file's text as it was (for knowing a change)
and its song, canonical (for the merge) — set on `project new`, `project open` (the song as opening reads it — old
songs' tracks adopted, lane groups gathered, `songOf`, `core/service/SolarisService.cpp:726`; `:833`), `project
recover` (the file the autosave was beside) and every save (`noteDisk`, `core/service/ServiceSync.cpp:32`). The song's
save is atomic — written beside, renamed over (`core/service/SolarisService.cpp:770`) — so a collaborator never reads
half a song. `pump` looks at the file at most once a second (`watchFile`, `core/service/ServiceSync.cpp:49`; called
from `core/service/ServiceTransport.cpp:140`): its size and time unchanged — nothing; read back as the base — this
window's own save, nothing (`core/service/ServiceSync.cpp:62`); not yet a song — a copy still arriving, Git's markers —
waited for, said once after three looks (`:80`); changed while conflicts are asked — it WAITS, said (`sync.waiting`,
`:69`); else ONE `project sync`, through the one way in. `project sync` (`core/service/Command.cpp:39`;
`syncCommand`, `core/service/ServiceSync.cpp:93`) is an edit only when it changes the song (`syncEdits`, `:40`;
`core/service/SolarisService.cpp:166`) — a look that finds nothing changed is no step: nothing unsaved here → the
song becomes the file's (`core/service/ServiceSync.cpp:159`), one step and not unsaved
(`core/service/SolarisService.cpp:438`); edits here → `mergeSongs(base, mine, theirs)` (`core/service/ServiceSync.cpp:163`),
orders tidied, validated, as ONE edit; either way the version open here stays open (`:168`), the base becomes theirs, and
`project.synced how=… theirs=… mine=… conflicts=… renamed=…` is said (`core/service/Event.cpp:36`) with the output naming
each renumbered id and each node asked. `--take theirs|mine [<node>]` answers one or all (`core/service/ServiceSync.cpp:101`;
theirs is an edit). The model publishes `sync` (`core/service/AppModel.h:364`): a count a front end says each by, the
last sentence, the conflicts (node, what, how, both sides' lines), waiting and unreadable. A save to the song's own file
is refused while a conflict is asked, and while the file has changed since this window read it — unless what is there
does not read as a song (nothing in it to merge: written whole, said) (`core/service/SolarisService.cpp:891`, `:909`).
Tests: L2 `test_a_song_shared_through_a_folder` (`core/tests/serviceTests.cpp:4810`) — two services on one file: b's
save reloads a (said, not unsaved, one undo away); a look that finds nothing changed adds no step; edits on both merged
— a's pan kept, b's tempo and bus arrived — undone and redone as one; a save over a file changed since read refused,
synced, saved, and b follows; a gain changed both ways asked (mine kept; its lines in the model), the save refused, b's
newer change WAITING through the window's look, --take refusals named, theirs taken, the waiting change then merged; a
bus both added as `ch_4` — theirs keeps it, mine `ch_5` with its send; the window's look after a second (not before) syncs
with no command; Git's markers: refused, three looks then unreadable, a save writes over them whole; the version open
here still open after a sync. Mutants seen red (12): a reload left unsaved; the sync no edit; a save over a changed
file; a save with conflicts asked; a newer change merged over the asked; theirs not taken; the file never looked at;
looked at every pump; the base not moved to theirs; the open version closed; a merge with nothing unsaved; the waiting
not said. Not caught by a test: the save's atomicity (by construction — a temp file renamed over).
Part 3 — the window. Each time the song follows its file the App says so in a toast — the model's sentence — and, when
the sync asked, opens the Conflicts sheet on its own (`app/App.cpp:573`); File › Conflicts… opens it any time
(`app/App.cpp:198`). `ConflictsSheet` (`app/widgets/ConflictsSheet.h:29`; the Relink sheet's look) draws only the model:
a row per node asked, keyed by node — what it is and how it was changed, then what tells the two sides apart, side by
side under HERE and IN THE FILE: a header field's line whole, a node's own line reduced to the fields that differ
("gain=-3.0" against "gain=-9.0"), under it the first line that differs and how many more (`tellApart`,
`app/widgets/ConflictsSheet.cpp:61`). Keep Mine / Take Theirs send ONE `project sync --take mine|theirs <node>` each;
Keep All Mine / Take All Theirs ONE `project sync --take mine|theirs`, inert with nothing asked; Done, Escape and Enter
close it (`:290`, `:319`, `:314`). Nothing snaps (`:213`): an answered row fades out where it is while the rest slide up
into its place, one asked while open fades in (`:232`); the card grows and shrinks with its rows; the "all" buttons dim
to 0.35, eased, when nothing is asked (`:280`); the list clips and scrolls. Tests: UI `test_conflicts_are_asked_in_a_sheet`
(`app/tests/ui/uiTests.cpp:5728`; a second service on the same file stands for the other window): its save reloads this
one, "Reloaded" in a toast; the strip and the tempo changed both ways — merged, "2 to answer" in a toast, the sheet
caught fading in, its rows the tempo then the strip; what tells them apart — the tempo's lines whole, the strip's
`gain=-3.0` / `gain=-9.0`; Keep Mine on the tempo — ONE line, the tempo 128, its row caught fading while the strip's
slides up; Take Theirs on the strip — ONE line, the gain −9, the "all" buttons caught dimming; with nothing asked Keep
All Mine sends nothing; Done; another round reopened from File › Conflicts…, Keep All Mine ONE line; Escape. Mutants
seen red (10): the sheet shown at once; an answered row cut; the next row jumping up; Keep Mine sending theirs; not
opened on its own; no toast; the "all" buttons dimming at once; the "all" buttons live with nothing asked; File ›
Conflicts… opening nothing; the sides shown whole, not told apart. Shots `conflicts-sheet` (the bass and the tempo
asked; the toast), `conflicts-answered-mid` (the tempo answered: its row fading, the bass's sliding up), both sizes
(`app/tests/shots/renderShots.cpp:643`).


### DR-ACCESS-1 Accessibility (R-ACCESS-1; docs/discussion.md Round 20) — part 1: the UI scale and high contrast; part 2: the accessible list and the keyboard; part 3: the list to AT-SPI
Two machine settings (`core/Settings.h:33`): `uiScale` — 75, 90, 100, 125, 150, 175 or 200 (cosmo's steps; another
number is refused, never rounded quietly, `core/service/ServiceMachine.cpp:129`) — and `highContrast` on | off; kept in
the settings file (`core/Settings.cpp:25`), in the model (`core/service/AppModel.h:295`), on Settings › Interface first:
a UI scale combo and a High contrast toggle, ONE `settings set` line each (`app/widgets/SettingsSheet.h:87`;
`app/widgets/SettingsSheet.cpp:48`). (Round 20 said `contrast=normal|high`; a toggle beside Reduced motion made it
`highContrast=on|off`.) The App eases both from the model (`app/App.cpp:984`): the scale over 260 ms (cosmo's
kScaleAnimMs), and every frame of it the logical size is the window's divided by the scale DRAWN (`applyLogicalSize`,
`:289`), every root drawn through that scaling (`:1037`, the modals `:1040`) and every pointer and wheel divided by it at
the entry point (`:610`, `:633`) — the shell zooms, never a cut then a reflow. High contrast is one eased amount handed to
cosmo's tokens each frame (`:996`): `palette::setContrast` (`../cosmo/Theme.h:50`), every neutral reading through
`neutral(normal, high)` (`../cosmo/Theme.h:58`) — black grounds, white text, quiet text 0xC0C0C0, borders 45 % white, the
accent and the semantic colours kept — and cosmo's `sharedTheme()` rebuilt when it moves (`../cosmo/Theme.cpp:78`); cosmo
never sets it (its 41 shots byte-identical, cosmo DR-G-5). A zoom or a fade keeps the window redrawing (`app/App.cpp:601`).
Tests: L2 `test_the_ui_scale_and_contrast_are_settings` (`core/tests/serviceTests.cpp:4935`): set, in the model,
printed, read back by another process; 110 refused, a typo named. UI `test_the_ui_scale_and_high_contrast`
(`app/tests/ui/uiTests.cpp:5827`): Settings › Interface → `settings set uiScale=150`, the drawn scale caught mid-way with
the logical width the window over it; at rest 853 × 533; the red playhead DRAWN at 1.5 × its logical place and not at
it; High contrast → `settings set highContrast=on`, the foreground caught mid-blend; at rest white on black, borders
solid, the shared theme's tab label light; the File menu's title clicked at its place in the window opens it; back from a
shell's line, eased the other way. Mutants seen red (9): the scale snapping; laid out at the target, not the drawn scale;
the pointer not divided; the contrast snapping; the palette not told; the border not blended; the shared theme not
rebuilt (red once the test read it before the switch); a scale off the steps accepted; the scale not drawn (red once the
test read the playhead's pixels). Shots `ui-scale-150`, `ui-scale-mid`, `high-contrast`, both sizes
(`app/tests/shots/renderShots.cpp:678`).
Part 2 — the accessible list and the keyboard. Every covered widget lists its controls as nodes (`A11yNode`,
`app/A11y.h:22`: a stable key, a role, a name, a value, its bounds in the window, enabled, checked, and what it does): the
song bar — Home, Settings, the version, Record, Play / Stop, the tempo (a slider: ±1 bpm, Shift 0.1, the drag's own line),
Save (`app/widgets/SongBar.cpp:83`); Home — its three actions, Settings, each recent song in view
(`app/widgets/HomeScreen.cpp:72`); the browser — its tabs, each row in view (`app/widgets/Browser.cpp:280`); the lanes'
headers and an instrument track's arm (`app/widgets/Timeline.cpp:330`); the dock — its pages' tabs, each strip's fader
(±0.5 dB, Shift 0.1) and pan (±5 %, Shift 1 %) as sliders, its mute, solo and arm, the master's fader and Mono
(`app/widgets/MixerDock.cpp:177`); the open windows' close buttons and a device window's parameters, each stepped by its
registry description (`app/widgets/FloatWindow.cpp:404`; `app/widgets/DevicePanel.cpp:392`); and the modals — Settings'
sections and rows (a combo stepped through its choices, Enter its next; `app/widgets/SettingsSheet.cpp:459`), Export's
rows (`app/widgets/ExportSheet.cpp:187`), Relink's and Conflicts' buttons (`app/widgets/RelinkSheet.cpp:66`;
`app/widgets/ConflictsSheet.cpp:177`), a confirm dialog's buttons (cosmo's `ConfirmDialog` gained `buttonLabel` and
`buttonRect`), a menu's items. The App gathers them in reading order — the song bar with its menus' titles after
Settings, the browser, the lanes, the dock, the windows — and a modal's ALONE while it is open (`App::accessibles`,
`app/App.cpp:649`). Activating a node is the click it stands for (`activateNode`, `:702`): a press and a release at its
centre fed through a recogniser of its own that shares the App's gesture routes (`:146`) and runs on a clock a second a
click, so two keyboard clicks never pair as a double-click (`clickAt`, `:727`) — unless the node names another action
(a combo's next choice). The keyboard (`focusKeyPress`, `:743`, first in `App::key`, `:818`): Tab / Shift+Tab put the
focus on the next / previous enabled node (wrapping); while it is held, Enter and Space activate it, the arrows step it
(Left / Down −, Right / Up +, Shift a tenth) and Escape lets go — when it is not held those keys are the keymap's (Space
plays). Not while Settings captures a key, nor while a field is typed in. The focus ring (`advanceFocus`, `:772`; drawn
over everything but the toast, `:1048`) sits over the focused node, travelling between nodes (220 ms) and fading in
(120 ms) and out; a node gone lets the focus go. Tests: UI `test_the_keyboard_reaches_every_control`
(`app/tests/ui/uiTests.cpp:5908`): the list's head (Home, Settings, the File menu), the Basic Synth fader "0.0 dB", the
tempo "120 bpm", the first tab shown, a mute a toggle; Tab — Home, the ring caught fading in, then on its bounds; Tab —
Settings, the ring caught travelling; Shift+Tab back; to Play — Space ONE line `transport play`, Space again stops it;
to the fader — Right `set ch_2.gain=0.5`, Shift+Left `set ch_2.gain=0.4` ("+0.4 dB"); Escape — the ring caught fading
out, Space plays again; Settings open — every node the sheet's; Tab into it, Enter on Interface, Right on UI scale
`settings set uiScale=125`, Left back; Escape lets go, Escape closes. Mutants seen red (11): Shift+Tab forward; the ring
jumping; the ring at once; Enter / Space not the click; the arrows no step; Escape keeping the focus; the keys taken
while not held; a modal's controls not alone; a fader's step a whole dB; a combo's step sending nothing; two keyboard
clicks pairing as a double. Shots `keyboard-focus` (the ring on the Bass fader), `keyboard-focus-mid` (travelling to
its pan), both sizes (`app/tests/shots/renderShots.cpp:697`). Not yet: the lanes' clips, the piano roll's notes, the
session grid, the history, the matrix's cells, the other windows' contents.
Part 3 — the list to AT-SPI. The host's bridge (`host/Accessibility.h`; plain ATK, no GTK, built where `atk` is found,
`host/CMakeLists.txt:52`) turns a list of items — the App's nodes as plain data — into ATK objects: a root (a filler named
Solaris) whose children are the controls in the list's order, each with its role (button → push button, toggle → toggle
button, slider, combo → combo box, tab → page tab, menu, menu item, list item; `host/Accessibility.cpp:68`), its name,
its value as its description, its states — visible, showing, focusable; enabled and sensitive unless it is not; a
toggle checkable and checked when on; a tab SELECTED when shown; focused (`:94`) — where it is (AtkComponent: the
logical bounds times the drawn UI scale in the window, and through the window's origin on the screen, `:144`) and its
actions (AtkAction: "click"; a value's "increment" and "decrement" too), which come back to the App through
`onActivate` / `onStep` by key (`:179`). Only a value — a slider, or a node that steps — is an object that answers
AtkValue (`:82`): its words, and the number they begin with, the minus the UI draws (U+2212) read as one (`:207`).
`update` (`:292`) keeps a control that stays as the SAME object (by key; its name, value and states follow, a checked or
enabled change said as a state change); one gone is said gone where it was — the new list in place first, the last
place first, so the places mean what they say — and is defunct to whoever still holds it (its click does nothing); one
new is said added (and, when the focus moved onto it before it was listed, said focused, `:347`). `focus` (`:355`): the
FOCUSED state and a focus event when the keyboard's focus moves, nothing when it does not. The bridge gone, every held
control is defunct (`:264`). The window (`linux_main.cpp`): its drawing area is a subclass whose accessible (a panel
named Solaris) has the bridge's root as its one child (`:340`, the type set `:358`, made `:458`); when a screen reader
first asks for it, the list is read (`readAccessibles`, `:157`: the App's nodes, the drawn scale, the window's origin,
the focus when held) and then every 200 ms (`onA11y`, `:186`) and when the keyboard's focus moves (`:408`) — nothing is
listed for a window no screen reader reads; a screen reader's click is `App::activateNode`, its steps
`App::stepNode` (`:398`) — the keyboard's, which are the pointer's. Tests: `solaris_a11y_tests`
(`tests/faces/a11yTests.cpp:105`), the bridge read back only through ATK's own calls with no display: the children in
order with names, roles and values; extents at scale 1.5 and 2 and on the screen; a slider's value −4.5 from "−4.5 dB", a
button no value; the states; the actions coming back by key (the App's refusal a failed action); identity across lists,
removals said last first and defunct, an addition said; the focus said once when it moves and for a control listed
after it; a tab SELECTED, a toggle's CHECKED said; held past the bridge, defunct. Mutants seen red (9): a fresh object
every list; removals said first to last; extents not scaled; focus said when it did not move; a defunct control still
acting (a crash); the UI's minus not read; a control listed already focused not said; every control a value; a tab's
shown state as CHECKED. Live (L5, once): the real window on a private D-Bus session read by libatspi — Solaris → panel →
filler → the home screen's four buttons with their extents and `click`; AT-SPI's click on Settings reached the home
screen's Settings at its centre (the desktop was locked, so no frame ran to open the sheet). Owed: Orca on an unlocked
desktop.


### DR-ACCESS-2 Localisation (R-ACCESS-2; docs/discussion.md Round 21) — part 1: the table and the setting; part 2: the shell; part 3: the song view (3a the lanes' side, 3b the dock and the windows)
`solaris_strings` (`strings/Strings.h`; no dependencies — the service links it to validate and list the languages, the
window will draw through it): English is the key. `tr(en)` is the words for an English string in the language drawn
now, `trc(context, en)` the same under a context (the key `context|en`), `trn(n, one, other)` a count's form by the
language's plural rule (`strings/Strings.cpp:247`; a plural a table lacks: English's form for that count), and `fill` puts `{1}` … `{9}` in
by position (`:272`). A look-up is cached per language and returns a reference that stays put (`lookUp`, `:213`).
English is the keys themselves; a language's table is a text file, `lang/<code>.strings` — `@language`, `@name`,
`@plural` (one-other: English's; other: one form for every count) then a line per string, `"English" = "words";`, a
plural's two English keys side by side with the rule's number of forms — parsed by `parseTable` (`strings/Strings.cpp:115`),
which refuses, naming the line, a key twice, a translation that drops or adds a placeholder, the wrong number of
plural forms, a line that is not one, a header it does not know, a string before the header. The tables are compiled
INTO the binary (`strings/CMakeLists.txt:7`: `embed_strings.cmake`, cosmo's font trick — the file's bytes as an array;
`load`, `strings/Strings.cpp:87`). The languages: English, each table (today `lang/vi.strings`: Tiếng Việt, one form —
part 2 made it the shell's every word, part 3 the song view's), and `en-XA`, generated (`pseudo`, `:319`): every letter
accented, a third longer in middle dots, in brackets, its placeholders kept. `resolve` (`:186`): `auto` → the OS's
language (`vi_VN.UTF-8` → `vi`) when Solaris has it, else English; an unknown code → English; `setLanguage` (`:200`)
likewise. The setting: `settings set language=auto|en|vi|en-XA` (`core/service/ServiceMachine.cpp:114`; another code
refused naming the nearest, or listing them), the machine's (`core/Settings.h:35`), kept in the settings file and read
back only when Solaris has it (`core/Settings.cpp:71`); in the model `settings.language` and `settings.languages[]`
— code and the language's name in itself (`core/service/AppModel.h:297`; `core/service/ServiceModel.cpp:139`;
documented `core/service/AppModelCodec.cpp:337`). Tests: `solaris_strings_tests` (`strings/tests/stringsTests.cpp:45`):
every table compiled in parses, the three languages; refusals by line (a key twice, a placeholder dropped or added in a
plain, a context's and a plural's line, plural forms under each rule, not quoted, no `;`, no `=`, unclosed, before the
header, an unknown rule or header, no `@language`); English the keys; Vietnamese from its table — a context, one form
for every count, what it lacks in English, the words staying put across a thousand look-ups; `en-XA` bracketed, accented,
a third longer, placeholders kept; `auto` from the OS. L2 `test_the_language_is_a_setting`
(`core/tests/serviceTests.cpp:4958`): auto by default; the list in itself; vi set; `klingon` refused listing them,
`vie` → vi, `langauge` → language; printed; en-XA and auto; another process reads vi back and ignores a code the file
names that Solaris lacks. Mutants seen red (9): a dropped placeholder accepted; a key twice; a plural's forms not
counted; the OS's region not dropped; the plural form by English's rule; the pseudo-language not longer (red once the
test measured a long sentence — on one word the brackets alone are a third); one cache for every language; any
language accepted by `settings set`; the file's language read unchecked.
Part 2 — the shell. The window's words (`app/Words.h`, included by every widget through `app/Theme.h`): a widget that
draws its own text draws `tr` / `trc` / `trn` at paint; text a widget STORES to show later — a menu's labels, a dialog's
lines, a toast, a combo's choices, a cross-fading note — is kept as words, `say(en, args…)` (`app/Words.h:51`) or
`sayn(n, one, other, args…)` (`:60`), pieces that join with data ("Copy Clip" + its key), and `shown` (`:90`) gives them
in the language drawn now; anything not said so is DATA, shown as it is — a song or strip named "Settings" is never
translated; `english` (`:121`) is the words in English whatever is drawn (an identity: a node's key, "menu.File"). `N_`
(`:48`, gettext's) marks an English literal translated where it is drawn (`tr(kLabels[r])`). The App: the language is the
model's setting resolved with the OS's (`setOsLanguage`, `app/App.h:108`; the window host reads GLib's language names,
`linux_main.cpp:414`); a switch DISSOLVES the whole window — 180 ms, linear (`kLanguageDissolveMs`, `app/App.h:111`;
`advanceLanguage`, `app/App.cpp:395`: placed on the first frame, a switch back mid-way turning round where it is, the
language that lays out and answers between frames the new one). Render advances everything ONCE, then paints once, or —
mid-dissolve — twice (`:1019`): the old language whole, then the new one inside a layer at the eased amount, LAST, so every
widget's hit rectangles are the new language's (`paintPass`, `:1029`); `needsRedraw` while it runs (`:603`). Each pass
`relabel`s (`:363`) what cosmo's widgets store: the song bar's menus kept as words (`setMenuWords`, `:340` — their titles
and items, the Edit menu's "Undo {1}" with the service's label as data), the open menu (`openMenu`, `:314` — every widget's
menu passes through it), the confirm dialog (`showConfirm`, `:326`: its title and message composed for the language drawn,
the recovery's age a count's words), cosmo's "HISTORY" and "Rename", and the sheets' combos (`SettingsSheet::relabel`,
`app/widgets/SettingsSheet.cpp:444`; `ExportSheet::relabel`, `app/widgets/ExportSheet.cpp:80`) — cosmo gained opt-in setters
that change words and nothing else (`MenuStrip::relabel`, `../cosmo/widgets/MenuStrip.h:46`; `ContextMenu::relabel`,
`../cosmo/widgets/ContextMenu.h:28`, and its `renameTitle`, `:47`; `ConfirmDialog::relabel`,
`../cosmo/widgets/ConfirmDialog.h:34`; `HistoryView::title`, `../cosmo/widgets/HistoryView.h:43`) — cosmo never calls them, its
shots byte-identical. Through the table: Home, the song bar (fixed buttons ellipsize their words; the CPU label has the room
its readout leaves, `app/widgets/SongBar.cpp:464`), its four menus and the version menu, every Settings page (the micro-labels
measured in their language, uppercased by `solaris_strings::upper`, `strings/Strings.cpp:289` — Vietnamese's letters too),
Export, Relink, Conflicts (what tells the sides apart, words where it is the sheet's), the confirm dialogs, the toasts the App
composes (the service's refusals and sync lines stay its words), the host pickers' titles, and every one of these widgets'
accessible names. Settings › Interface › Language — Automatic, then each language by its own name — ONE `settings set
language=` line (`app/widgets/SettingsSheet.cpp:383`). Not translated (Round 21): the service's lines, the keymap's action
names (the service's), the registry's words, the user's data; the song view's own words are part 3. `lang/vi.strings`: 248
lines, the shell's every word. Tests: UI `test_the_window_speaks_the_language` (`app/tests/ui/uiTests.cpp:6489`): Settings ›
Interface › Language → `settings set language=vi`; a frame mid-dissolve (the amount between 0 and ½) painted "Settings" at full
alpha AND "Cài đặt" under a layer at exactly the live amount (a spy target, `app/tests/Rig.h:226`, records every string and its
layer alpha); the menus' rectangles already the Vietnamese titles'; at rest no English; a combo's choices Vietnamese with
nothing in the model changed since; the node "menu.File" named "Menu Tệp"; back to English from a shell's line the other way;
a switch back mid-way the same mixture that frame and on at the same rate. `test_every_word_the_shell_draws_is_from_the_table`
(`app/tests/ui/uiTests.cpp:6670`): in `en-XA`, each covered widget painted alone through the spy — Home, the song bar and each menu open, the version
menu, all seven Settings pages and a combo's list, Export, Relink, Conflicts, the history's card, a confirm dialog — every
string with a letter is bracketed (the table's), the model's data (uppercased too), or a name no language translates (units,
PulseAudio, Mackie Control, file kinds, a key's name); each widget drew the table's words at all; a composed toast and every
accessible name likewise. `test_the_table_has_every_word_the_window_says` (`:6762`): the window's source scanned
(`app/tests/ui/WordsScan.h:97`: comments skipped, literals joined, escapes decoded) — `vi.strings` has every key the code hands
the table and none it does not. Mutants seen red (11): the old language never painted; the new one not in a layer; the old one
painted last; a switch snapping; a switch back jumping; the menus not relabeled; a menu's key in the language drawn; the
combos not relabeled (survived — a later bind repainted them — red once the test read a translated choice with nothing
changed since); a word left out of the table; the scan blind to `N_`; a stored word shown as stored. English pixels unchanged:
the 216 deterministic shots byte-identical before and after (the 16 playing ones differ between two runs of the same
binary). Shots `home-vi`, `menus-vi`, `settings-vi` (the Language list open), `home-pseudo`, `settings-pseudo`,
`language-dissolve-mid`, both sizes (`app/tests/shots/renderShots.cpp:715`). Found: D-24 (the pseudo-language's song bar at
1024 px).
Part 3a — the lanes' side (part 3 in two commits; the dock and the windows are 3b). The browser: its tabs (`N_`, drawn
through `tr`, fitted to their tabs), its rows' words kept as words — headers ("Instruments", "Effects", "MIDI", "Samples";
a preset type's header the registry's word), the empty rows, Search · …, Favourites, New MIDI — and a pattern's note
counted (`lengthText`, `app/widgets/Browser.cpp:38`: "{1} bar", "{1} beat", "{1} beats" for a fraction; "{1} note", the
strips data); headers drawn in capitals in any language (`:555`); a row's accessible key by its English (`:293`) and
`row(i)` returning the row as shown (`app/widgets/Browser.h:68`). The lanes: the ruler's Snap and its step's name kept as
words so the step cross-fades and the window dissolves apart (`Timeline::stepName`, `app/widgets/Timeline.cpp:88`;
`snapLabel` returns it shown, `:97`), a take's row, the badges (takes counted, linked ×N, offline), a folded group's band in
capitals (`:1722`), the empty hints, a new group's and a new marker's name in the user's words (`:1306`;
`app/widgets/TimelineMarkers.cpp:195` — a name, the user's data once made; never a command word), the markers' header, the
video row; every menu the lanes, the ruler, a marker, a tempo, a clip's edits and the session grid open said as words
(the automation shapes' names said, their grammar words sent, `app/widgets/TimelineAuto.cpp:610`); the drop hint's "new
lane" (`app/widgets/ProjectScreen.cpp:108`); the session grid's + Scene, Back to Arrangement and hint
(`app/widgets/SessionView.cpp:361`); the lanes' accessible names. `lang/vi.strings`: 356 lines. Tests: UI
`test_every_word_the_lanes_draw_is_from_the_table` (`app/tests/ui/uiTests.cpp:6770`): in en-XA, a song with an instrument's
track, a sample's lane, a section marker, a tempo change, an automation and a scene — the browser's four tabs, the lanes,
every lane's and clip's accessible name and value, the menus of a lane, each clip, the marker, the tempo, the ruler, and the
session grid — each painted alone, every string the table's, the model's data (now the clips', patterns', markers',
automations', groups', scenes', tempos', the registry's words and the ids too) or data ellipsized to its room; the source
scan holds. Mutants seen red (5): a browser header drawn as stored; the ruler's Snap untranslated; a clip's menu item, a
marker's, the session's + Scene left English. English pixels: the 216 deterministic shots byte-identical to part 2's (a
first try moved the shown tab's label a sub-pixel — it was centred by the regular face's width and drawn in the medium —
kept as it was). Shots `lanes-vi`, `lanes-pseudo`, both sizes (`app/tests/shots/renderShots.cpp:758`).
Part 3b — the dock and the windows. The mixer dock (`app/widgets/MixerDock.cpp`): a strip's kind said through literal
`tr` calls (`kindWord`, `:134` — "instrument", "audio", "bus" are the model's words, shown as the window's), its card's
counted lines ("{1} clip", "fed by {1}", "VCA · moves {1}"), "+ Effect", "+N more", the FROZEN pill in capitals in any
language, the master's devices counted, Mono, "+ Line" and the empty page's hint, every menu (a strip's, its route's, a
chip's, "+ Line"'s, a mixer's), the tabs' "Matrix" kept as words with their widths measured in the language each pass
draws; `labelOf` (`:283`) returns words — "Master", "{1} port" — and the drawn arrow shows them (`arrowText`, `:144`); the
accessible names composed by the table ("{1} fader", "{1} pan", "{1} % left", `:202`). A number's menu (ParamMenu): every
item said, a binding's tag "auto {1}" (`app/widgets/ParamMenu.cpp:36`). The windows: a window's title kept as words and
shown where drawn (`FloatWindow::title`, `app/widgets/FloatWindow.h:57`; the roll's, `app/widgets/PianoRoll.cpp:70`; the
automation's, the loudness window's, the video's; a device's the registry's label and the strip's name), its close button
"Close {1}"; the piano roll's toolbar (Snap in capitals, Notes, Steps, Quantize…, Length {1}, Scale, Chord, Tools — chips
ellipsized), its lanes' tabs (Vel, Bend, Mod, Chance), its menus; the automation window's rows kept as words
(`app/widgets/AutomationPanel.cpp:94`: Name, Id, Range, Made from, Length, Rest, Clips, the points' shapes as the rows drew them —
"linear", "bezier in …" — Read by), its Length combo's choices re-shown in its own `advance` when the language changes
(`:161` — a window has no relabel from the App; the combo cross-fades the change); a device window's buttons (Editor,
Presets, On / Bypassed, Piano Roll, Remove, Reload — ellipsized in their rooms, measured with the face they always were)
and lines (a sampler's, "no pitch", "Offline — {1}" with the service's reason, the owner "Master",
`app/widgets/DevicePanel.cpp:266`); an instrument's face's default module "Main" (`app/widgets/AnalogFace.cpp:140` — the
others are the registry's titles); the loudness window's labels in capitals (`app/widgets/LoudnessPanel.cpp:21`); the video
window's words, kept as words so they cross-fade as they change (`app/widgets/VideoPanel.cpp:71`). Kept as they are: the
registry's words (a parameter's label and choices, a module's title, a pad's name), note names, units, the standard's name
(ITU-R BS.1770-4), the song's names. `lang/vi.strings`: 499 lines — the window's every word. Tests: UI
`test_every_word_the_dock_and_windows_draw_is_from_the_table` (`app/tests/ui/uiTests.cpp:6835`): in en-XA, a song with an
instrument, a sample's strip routed to the master, a bus fed by a send, a VCA, the master's compressor and an automation —
the dock and its accessible names and values, the menus of a strip, its route, a chip, "+ Line", a fader's number, the
matrix; an instrument's face, an effect's list, the piano roll, the automation, loudness and video windows and their names —
every string the table's or the model's (the registry's words now too: parameters, choices, groups, their prefixes; the
mixers'), with nested brackets, a number's unit and a note's name understood. The source scan holds. Mutants seen red (5):
"+ Effect" left English; the roll's title not said; the loudness labels untranslated; an automation row drawn as stored; a
route arrow drawn as stored (survived — no arrow said "Master" — red once the song routed a strip to the master). English
pixels: the 216 deterministic shots byte-identical to part 2's (a first try said the loop's "every 1 beat" as "every beat":
put back). Shots `dock-vi` (the mixer, a drum machine's face and the roll over it), `dock-pseudo` (the roll and the loudness
window in en-XA), both sizes (`app/tests/shots/renderShots.cpp:775`). Not yet (Round 21): the registry's words and the
service's lines translated; right-to-left; plural rules beyond one / other and one form; the web face's text; and D-24.

### DR-ASSIST-1 Analysis tools (R-ASSIST-1; docs/discussion.md Round 22) — part 1: the DSP library; part 2: the service; part 3: the window
**Part 1 — the listening is the DSP library's** (DSP f0d6fc1, `core/DigitalSignalProcessing/src/analysis/README.md`,
REQ-analysis-1…4): `stft` / `istft` (periodic Hann, hop N/4, an exact inverse), `separateHarmonicPercussive` (median
filters 17 × 17, Wiener masks from the channels' sum — the halves sum back to the sound), `transcribe` (YIN, lifted out
of the tuner into `meter/Yin`, framed W/8, a −50 dBFS gate, a five-frame median, 60 ms notes split at the library's
onsets), `detectChords` (a chroma from 110 Hz to 4 kHz, each beat scored against the 24 major and minor triads, "N"
under 0.6 or silent, Viterbi with a 0.1 stay); offline, allocating, deterministic. Solaris has no copy of any of it.
**Part 2 — the service** (`core/service/ServiceAnalysis.cpp`; rows `core/service/Command.cpp:549`–`:561`, dispatched
`core/service/SolarisService.cpp:243` to `analysisCommand`, `core/service/ServiceAnalysis.cpp:62`). What a clip plays is
`renderClipsRaw` (`core/service/ServiceAudioEdit.cpp:320`) — `clip consolidate`'s render, lifted out so both use one
copy: the clip alone through its strip with no devices, at unity, centred, to a master at unity, its output latency
trimmed (a note clip, or a clip whose file is offline, is refused before it). **`clip separate <ac>`**
(`core/service/ServiceAnalysis.cpp:77`): that sound separated; each half written beside the song through `writeBounce`
with a folder (`Separated/<name> harmonic <n>.wav`, `… percussive <n>.wav`, 32-bit float); each a `clip add --src <it>
--at <the clip's> --lane new` — a new file, so its own strip (R-MIX-2) — the strip, lane and clip named `<name>
harmonic` / `<name> percussive`; the two new lanes put right under the clip's own, renumbered as `lane move` does and
in its group (`placeUnder`, `:29`); the clip deleted — one edit, the harmonic clip printed, `made:` the rest. The two
clips at unity on their strips sum to what the clip played, so a song whose strip was at unity sounds as it did.
**`clip to-midi <ac> [--instrument <type>]`** (`:126`): the sound's mono sum transcribed; none heard is refused
(`:136`); a `clip add --instrument <type, synth by default> --lane new --at <the clip's> --length <its length>`, its
pattern named `<name> notes` and as long as the clip, each note's start and end from seconds in the clip to beats in it
THROUGH THE TEMPO MAP (`beatIn`, `:150`: `beatOfSample(sampleOfBeat(at) + t·rate) − at`), its velocity the
transcription's; the new track right under the clip's lane; the audio clip stays. **`chords detect [<ac>]`** (`:174`):
the clip's sound over its span, or with no clip the whole song's mix (`renderSongMix`, `:54` — every strip, its
devices and its plugins' own instances, the master, from 0 to the compiled end, no tail; an empty song refused); the
beats handed to the library are the span's start, every whole beat of the tempo map inside it and its end, in seconds
(`:195`); what was in the span goes and a chord across an edge keeps what lies outside it — the part before keeps its
id, a part after a new one (`:201`); each heard chord but "N" (`:233`) added at its beats, sorted; a detection that
neither finds nor changes anything is refused (`:245`). Each chord made or changed is said (`chord.added`,
`chord.changed`, `chord.deleted`), then `chords.detected n= from= to=` (`:247`, `core/service/Event.cpp:69`: held
until the command lands). **`chords clear`** (`core/service/ServiceAnalysis.cpp:147`) takes every chord out, refused with none. **The song** —
`Chord` (`model/Project.h:296`: id, name, at, length in beats), `#achord id= name= at= length=` written after the
markers (`model/Project.cpp:1144`; a sharp's name quoted), `cd_` ids in the id space (`:177`, `made:` names them —
`core/service/SolarisService.cpp:381`), validated (`model/Project.cpp:1404`: a name, a length, at ≥ 0, in time order,
none overlapping); a three-way merge names one "chord …" (DR-COLLAB-1's table needs no row). The time edits move
them as sections (`core/service/ServiceArrange.cpp:121`: after an insert moved, across it grown; inside a delete
gone, across it shrunk, a head in it starting at `--from`). **The model** — `songChords[]` {id, name, at, length}
(`core/service/AppModel.h:595`, filled `core/service/ServiceModel.cpp:406`, documented
`core/service/AppModelCodec.cpp:254`, in the stable dump `:653` as `<id> <name>@<at>+<length>`): NOT `chords`, which
was already `--chord`'s vocabulary (R-ROLL-11) and a model field is never renamed — Round 22's `chords[]`, amended.
L2 `test_separate_splits_a_clip_in_two` (a held 440 Hz tone with a click every quarter second: the halves sum back to
3e-8, the tone within −33 dB, the clicks −12 dB; the lanes in order under its own; the song after within 3e-8 of
before; one edit, undone whole), `test_to_midi_transcribes_a_melody` (A4, C5 twice, E5 softer, the tempo halving a
beat in: four notes within 0.011 beats of where the map puts them; `--instrument sampler`; an unknown type, silence and
a note clip refused), `test_chords_detect_and_clear` (C Am F G then a held G: the song's mix gives C@4 Am@6 F@8 G@10+6;
a short C triad at 13 detected alone cuts the G into 10+3 and 14+2, a new id after; insert across the G grows it; a
delete moves, takes out, shrinks and starts a head at `--from`; saved and read back; clear; refusals); model
`test_chords_round_trip_and_are_checked`. Mutants: the halves swapped, seconds as beats at one tempo, the lanes left
last, a crossing chord deleted, a chord's head in a deleted span left where it was, "N" kept as a chord — each red.
**Part 3 — the window** (`app/widgets/TimelineChords.cpp`). **The band:** the song's chords at the ruler's foot, above
the picture's row — a band of their own (`kChordH`, 19.5, `app/widgets/Timeline.h:278`), there only while the model has
`songChords`: its height eased 220 ms in and out (`advanceChords`, `app/widgets/TimelineChords.cpp:62`; a song opened
places it where it is), and everything under it reads it LIVE — the picture's row starts at `videoTop()`
(`app/widgets/Timeline.h:268`, `app/widgets/TimelineVideo.cpp:158`), the lanes at `top()` (`app/widgets/Timeline.h:267`),
so both slide with it. Each chord keyed by id (`bindChords`, `app/widgets/TimelineChords.cpp:47`): placed where the model
puts it, fading in (200 ms), easing to a new `at` or `length` (a time edit, a new detection), fading out when gone and
kept until faded. Drawn (`paintChords`, `:103`) on the ruler's ground with "CHORDS" in the header as the markers' band
names itself; each span a card with a mark where it begins and its name at its start, ellipsized against the span (a
chord's name is the song's — never translated); a foot line. Each chord on screen is an accessible list item, "Chord
Am", its value "beats 6 to 8" (`app/widgets/Timeline.cpp:350`). **The menus:** an audio clip's gains "Separate harmonic
/ percussive", "To MIDI" and "Detect chords" — `clip separate|to-midi <ac>`, `chords detect <ac>`
(`app/widgets/TimelineClipEdit.cpp:361`); the ruler's — on the band too, which is the ruler's
(`app/widgets/TimelineMarkers.cpp:177`) — "Detect chords" (`chords detect`, the whole song) and, while it has
chords, "Clear chords" (`:212`). Sentence case, as the context menus are (Round 22 named them in title case). UI
`test_the_chords_band` (`app/tests/ui/uiTests.cpp:5546`; the rig now plays back a sound a test made, by its file name —
`rigSounds`, `app/tests/Rig.h:100`): a clip's Detect chords is ONE line; the band caught mid-grow with the lanes at
`row0 + amount·kChordH`, a chord mid-fade; C@4 spans beats 4 … 6; the names and "CHORDS" drawn; the accessible; a
picture's row under the band and the lanes under both; the band's Insert a bar slides the chords (caught between);
Clear chords fades them and closes the band (caught mid both), the lanes back; the ruler's Detect chords with no Clear
offered; To MIDI on a melody lands "melody notes", Separate two clips. `test_every_word_the_lanes_draw_is_from_the_table`
now draws the band, its menu and its accessibles in en-XA. Shots `chords-band`, `chords-band-growing` (mid-grow),
`chords-menu` (`app/tests/shots/renderShots.cpp:618`). Mutants: the band snapping, the lanes not under it, chords
popping in, chords jumping on a time edit, the picture's row under the ruler, the band not the ruler's for a
right-click, Clear offered with none, a clip's Detect hearing the whole song, a gone chord vanishing — each red.

### DR-DEV-9 A PipeWire driver; xruns counted (R-DEV-9, R-DEV-5)
`settings set driver=pulse|pipewire` (default pulse; saved with the machine's settings; Settings › Audio › Driver).
Every stream the service opens — the player, the browser's audition, the roll's note preview — goes through
`openOutput` (`core/service/ServiceTransport.cpp:109`): with `pipewire`, the host's `audioOutFor("pipewire")`; when the
build has none, or it will not open, PulseAudio (`audioOut`) — and an Info line says why, ONCE: a PipeWire that failed
is not tried again (a note preview would wait each time) until the driver is set again. `transport.driver` says what
plays, `transport.xruns` the device's underruns since play (−1: Pulse's simple API cannot count them). The host's
`AudioOutPipewire` (`host/AudioOutPipewire.cpp:68`): a `pw_stream` on a thread loop, F32 interleaved at the song's
rate, `node.latency` = the buffer setting (64 frames and up), `target.object` = the output setting; `open` waits up to
2 s for STREAMING and fails naming PipeWire's reason. The player writes into an `AudioRing` (`host/AudioRing.h`: one
writer that waits for room — the back-pressure that is the clock, as Pulse's blocking write is — and one realtime
reader that never waits, locks or allocates: a short read is silence and ONE xrun, counted once the writer has begun
and not after a flush); PipeWire's callback takes from it (`onProcess`, `:45`). Latency = what waits in the ring +
the server's delay (`pw_stream_get_time`). `solaris_ring_tests` (order; a short read silence and one xrun, none
before the first write or after a flush; 12768 frames across two threads, each once and in order; a dead stream lets
the writer go); L2 `test_the_driver_and_its_fallback` (the setting saved and refused; no driver, a PipeWire that
links nowhere, one that opens with three xruns, Pulse asked — the transport says which; said once, tried again when
set); UI: the Driver combo, one line, its label cross-fading. Live (no device reached, a silent song): PipeWire
answered "no node available" for a missing target at once, the service fell back and said so, Pulse refused the
missing sink. Mutants: an xrun counted before the first write; a failed PipeWire tried again — both red.
**Owed live:** playback through PipeWire on a real device at 64 frames (the user's machine runs a PulseAudio server).

### DR-DEV-1 Several devices: each follower held to the clock, every port heard together (R-DEV-1, R-DEV-2, R-DEV-4, R-DEV-5)
**Which.** At play (`core/service/ServiceTransport.cpp:535`) each output port goes where this machine's map puts it:
the clock (`settings.output`), or another device — a FOLLOWER, opened through the clock's driver with the channels
its ports need; one that will not open (`:569`) is silent, said by an Info line ("the device … did not open (why) —
its ports are silent") and shown in the model; the song plays on. **Together** (R-DEV-4; `:579`): each path's delay
from a frame rendered to its being heard is its device's latency as opened — plus, for a follower, its fill target
over the rate (the least: `Follower::minTarget`, `core/Follower.h:73`, the lookahead and two of each block). The
slowest path sets when; the clock is delayed to it by a delay line on its channels (`Player::delayClock`,
`core/Player.cpp:202`; `setClockDelay`, `core/service/ServiceTransport.cpp:597` — the heard position counts it), and
each follower's target is the rest (`:593`), its drift as last measured kept for the next play. **Feeding** (the audio
thread): after each render `feedFollowers` (`core/Player.cpp:178`, called `:615`; silence through a count-in, `:574`)
mixes each follower's ports at their channels and `push`es them (`core/Follower.cpp:60`): an SPSC ring sized at
start, no lock, no allocation; a full ring drops the block, counted; each push time-stamped under a seqlock. **Held**
(`pullOnce`, `:82`, the follower's own thread, paced by its device's blocking write): the fill is CONTINUOUS — up to
the last push's start, plus the frames the clock has made since by its time stamp, less what was taken, plus what the
resampler holds (REQ-resample-2 (A5)) — and steers the DSP library's `FillController` (REQ-resample-3), whose step
the `AdaptiveResampler` reads at. Primed (`:107`): silence until the fill reaches the target, what came past it
skipped to the nearest frame; starved (`:143`, the clock stalled) or far ahead (`:124`, its device stalled): silence
and primed again. Its state for the model: drift, smoothed fill, the target as ramped (`:138`, an atomic — the
controller is its thread's), device latency read after each write. **Seen** (R-DEV-5; `pump`,
`core/service/ServiceTransport.cpp:202`): `transport.devices[]` while playing — the clock, each follower (latency,
drift, xruns, starved, dropped), each that would not open (`:227`, its problem); none stopped
(`core/service/AppModel.h:460`). Tests: L2 `test_a_follower_alone` (`core/tests/serviceTests.cpp:4991`: in simulated
time, a clock 200 ppm slow and unlike blocks — an impulse out exactly its target after it was made, within a frame;
a stalled clock: starved, primed again, on time; a stalled device: pushes dropped, on time again at once);
`test_a_port_on_another_device_follows_the_clock` (`:5089`: devices paced in REAL time, a follower 300 ppm fast — its
1 kHz continuous and at its level, an impulse heard on both within 1 ms (measured 0.02–0.12 ms) whichever path is
slower; a device that will not open said and silent; the devices published; the drift kept). Mutants (each red): no
clock delay; no priming skip; the target not aligned; the time since a push ignored; the resampler's hold uncounted;
a starve not priming again; a push never full; followers fed silence; the drift forgotten; far ahead left alone; a
follower that would not open unsaid; the step the wrong way; followers unpublished. Found while building it: primed
on the RAW ring count, a follower started up to a block late (it looks once a block) — 4.4 ms, measured; cut back to
the target after its device stalled, it was a block off again (the last push stale, where the next lands unknown) —
it is emptied and primed instead; a ThreadSanitizer run found `state` reading the controller's target off its thread
(fixed: an atomic) and five races inside the test fakes (D-17). Not yet: a follower at a rate unlike the song's;
re-aligning when a device's latency changes after play; an input on another device (N32 part 3).

### DR-DEV-1a An input on another device, held to the clock (R-DEV-1, R-DEV-2, R-DEV-4)
**Which** (`openInput`, `core/service/ServiceInput.cpp:114`): an input port unmapped (the default input) or mapped to
`settings.input` — the input chosen beside the clock in Settings — shares the clock's crystal and is read as it is
(`:115`); one on any other device is wrapped in a `DriftIn`, its drift as last measured kept for the next play. **The
seam** (`core/AudioIn.h:33`): `held` says what a capture holds now and when its last block landed and its size;
`CaptureStamp` (`:42`) is the seqlock a host marks after each push (`host/AudioInPulse.cpp:53`,
`host/AudioInPipewire.cpp:54`); a capture that cannot say is read as it is. **Held** (`DriftIn::read`,
`core/DriftIn.cpp:47`, the audio thread — it IS an input, the player reads it as any): the output follower's loop the
other way round — the fill continuous (`:63`: what the capture holds less its last block, plus what its device made
since that block landed, plus what the resampler holds), steering the FillController whose step the
AdaptiveResampler reads at; primed (`:70`) to its target, the excess skipped; far ahead (`:79`, the clock stalled):
emptied, primed again; starved (`:91`, its device stalled): silence, counted, primed again. Its drift is published
the input's way round (`:31`: the controller's consumer is the clock). Its latency (`:100`) is its device's — what it
reports less its ring, read beside the ring (`:61`) — plus the target: the player's input latency, so a recording's
alignment counts it (R-DEV-4). **Seen**: `transport.devices[]` gains it, role `input`
(`core/service/ServiceTransport.cpp:215`; `Player::driftInput`, `core/Player.h:149`). Tests: L2
`test_an_input_held_alone` (`core/tests/serviceTests.cpp:5226`: in simulated time, a capture 200 ppm fast landing
256-frame blocks read 128 at a time — an impulse read exactly its target after it landed, within a frame; its device
stalled: starved, primed again, on time; the clock stalled: emptied, primed again, on time);
`test_an_input_on_another_device_follows_the_clock` (`:4840`: a capture paced in REAL time 500 ppm fast, monitored
through an armed line — the tone on the clock continuous and at its level; its latency its device's and its target's;
its drift read the right way; the Settings input read as it is). Mutants (each red): never held; the Settings input
held too; no priming skip; the time since a landing ignored; the resampler's hold uncounted; a starve not priming
again; far ahead left alone; the step the wrong way; the latency without the target; the drift the wrong sign; the
start drift ignored. Found while building it: the device's latency was first taken AFTER the read had emptied the
ring it subtracted — 3 ms short, measured. Not yet: a recording through a held input measured against a cable (its
alignment is by its latency, as a default input's is).

### DR-DEV-5 Each port on its device, seen in Settings › Audio (R-DEV-5, R-DEV-1, R-DEV-3)
**The rows** (`bind`, `app/widgets/SettingsSheet.cpp:295`): under the Audio page's settings, the open song's PORTS,
keyed by name — each with the device this machine's map puts it on (`settings.ports`; "Clock device" / "Default input"
when none) and a line under its name: stopped, where it plays (on the clock, or on a device from a channel, held to
the clock; an input beside the clock or held to it); playing (`:330`), its device as `transport.devices[]` has it —
"the clock · 21.7 ms · 0 xruns", "held to the clock · 41.6 ms · +62.50 ppm · …", or "silent — why". **Eased**
(`advance`, `:754`): a row grows in and shrinks out; a line that changes cross-fades, the old out, the new in.
**Drawn** (`paintPorts`, `:1169`): a "PORTS" micro-label, each row's name and direction, its line, and a field drawn as
a combo's (SettingCombo) naming the device; the page's content grows by them (it scrolls to them). **A pick**
(`:922`, `openPortMenu` `:815`): a click on the field opens the app's context menu over the sheet
(`app/App.cpp:102`) — the clock (or the default input), then every device of the port's direction, the current one
marked — and a pick is ONE line, `settings set port.<name>=<device>:<channel>` (or empty: back on the clock). Test:
UI `test_settings_puts_a_port_on_a_device` (`app/tests/ui/uiTests.cpp:6386`: Phones' field lists the clock and the
rig's three outputs; a pick sends its one line, the field names the device, its line cross-fading — caught
mid-ease; playing, Main's line says how the clock is; a port added grows in, undone shrinks out — each caught
mid-ease). Shots: settings-ports, settings-ports-arriving. Mutants (each red): the line snapping; rows snapping in;
every device listed whatever its direction; a pick without its channel; a removed row gone at once; the playing line
never said.

### DR-MIDI-2 A MIDI keyboard: ports, arm, heard at once, recorded (R-MIDI-2)
The host listens through the ALSA sequencer (`host/MidiInAlsa.cpp:56`): one client "Solaris" with one input port,
subscribed to the ports `settings midiInputs` names (all — the default — none, or names, comma-separated: names, as
ALSA's numbers move between boots), a thread polling every 100 ms handing notes, controllers and pitch bend to the
sink as their MIDI bytes. The sink pushes them into ONE lock-free queue the service owns (`core/MidiIn.h`) — never
a lock, a full queue drops — and the audio thread that runs drains it at each block (`Player::midi`,
`core/Player.cpp:453`): a note-on or -off into the armed strip's instrument through the engine's live ring
(`Engine::liveNote`, `engine/Engine.cpp:557`: 63 waiting, applied at the next piece's start), the sustain pedal
(CC 64) holding a released note until it comes up (`SustainPedal`, a bitset; a held pitch struck again is let go
first), all-notes-off obeyed, the notes still held let go when the armed strip changes. `strip arm <ch>|none`
(`core/service/ServiceInput.cpp`) is the session's, never the song's — not an edit, `strip.armed strip=` — and
refuses a bus or an audio line. While the song plays its player drains the queue; while it is stopped an INPUT
MONITOR does (`startMonitor`, `ServiceInput.cpp:286`): a player of its own over the song with no clips and no automation, so the
strip sounds through its rack, its sends and the master as it will in the song; rebuilt after an edit, stopped
before the song plays and started after it stops (one consumer at a time; the queue drained in between). `midi
devices` lists the inputs (Settings' Audio page: MIDI input), `midiMessages` counts what came. The window: an
instrument track's header has an arm button, a ring lit red as it is armed, eased (`armRect`,
`app/widgets/Timeline.cpp:361`). Live: `solaris_midi_tests` plays into the kernel's Midi Through from a second
ALSA client — a note on, the pedal, its off and a centred bend arrive as their bytes. L2
`test_a_keyboard_plays_the_armed_strip` (silent until struck; struck, it sounds through the monitor and through the
song from beat 4; let go, it dies away; under the pedal it rings on until the pedal comes up; no allocation on the
audio thread; refusals; not an edit; eight messages counted); UI `test_an_instrument_track_is_armed_from_its_header`.
Mutants: the pedal ignored, the song's player not handed the keyboard (first SURVIVED: the test played from beat 0
and the tone clip masked the key — it plays from beat 4 now, silent until struck), the arm light snapping — red.
**Recording.** `transport record [--from b] [--count-in bars] [--overdub]` (`core/service/ServiceTransport.cpp:453`) is a
play that keeps the armed strip's notes: the player stamps each where it was HEARD (`Player::heard` — rendered minus
the device's and the engine's latency) into a lock-free queue the service empties as it pumps. A count-in (the flag,
else `settings countIn`, 0…4 bars) is a pre-roll of silence clicked on the song's beats with the metronome forced on,
the engine waiting (`core/Player.cpp:568`), aligned as playing is; while it runs `heard` counts back to the start, so
a key struck during it lands before the take and is dropped (half a beat early is the downbeat). `transport stop`
lands it — an EDIT, so dispatch snapshots, validates and records one undo, labelled "Record" (`commitRecording`,
`core/service/ServiceInput.cpp:334`): ons paired with offs (an on again ends the last; still held, to the stop),
starts on `settings recordQuantize` (off, 1/4 … 1/32), then a new note clip on the strip's track from the bar the
take began in, whole bars long (pattern "<strip> take"), or with `--overdub` notes added into the strip's clip under
the start (wrapped where it repeats; a pitch struck where one already starts keeps the longer). Nothing played lands
nothing (`nothing was played`, `record.finished clip= notes=0`). While recording, play, seek and record again are
refused; `transport.recording`, `.countingIn`, `.recorded` (notes that will land). The window: Record beside Play —
`transport record`, lit red while recording, eased; again, `transport stop`; Settings › Playback: Record quantize,
Count-in. L2 `test_record_the_keyboard_into_a_clip` (a bar of count-in clicking with the song waiting, a key struck in
it dropped; two notes at beats 5 and 6 on the 1/4 grid, velocities kept, the clip at bar 2, one bar long; one undo;
overdub adds a third; nothing played; refusals — stable over three runs); UI: Record refused with nothing armed
(toasted), lit, stopped; shot `recording`. Mutants: no quantize; the count-in not counted back by `heard` — red.

### DR-INST-6 Glide and the wheels in the instruments (R-INST-6; R-MIDI-3's DSP half)
The DSP library's Basic Synth gained `voice.mode` (poly · mono · legato), `voice.glide` (ms), `voice.bend` (the
pitch wheel's range, 2 semitones) and `vibrato.depth` / `vibrato.rate` (the mod wheel's vibrato), and every
instrument device the `pitchBend(−1…+1)` / `modWheel(0…1)` seam — the Sampler bending its rate, a kit and the piano
ignoring them (DSP `9d99565`: REQ-inst-expr-1, REQ-synth2-7, REQ-synth2-8; pitch moved on an absolute 32-sample grid,
so a render is the same however time is chopped). Solaris has them with NO code of its own (law 2): the registry
gives the `.slp` keys, the addresses (`set dv_1.voice.mode=legato`), the API lines and the device window — the
synth's analog face grew from six modules to eight, Voice (the mode a switch, Glide and Bend Range knobs) and
Vibrato, in three rows at its 980-wide size with no scroll, wrapping without spilling when narrow. The wheels are
reached by the engine through the device seam; the keyboard's own wheels and their lanes are N11 (R-MIDI-3). Guarded
by the DSP suites and the face tests (`test_an_instruments_face_is_an_analog_panel`, the plugin editor's
`test_the_synth_editor_is_the_registry_in_the_arstro_look`: eight titled modules; a parameter found by NAME, since
the face orders its controls by module, not by the registry).

### DR-INST-7 The wavetable (R-INST-7)
**The sound** is the DSP library's (`core/DigitalSignalProcessing`, REQ-wavetable-1…3, its commit ad3f2a3): frames of
2048-point cycles band-limited at nine levels by its FFT and read between two levels both under Nyquist; a `Table`
waveform of its `Oscillator`; BasicSynth's two oscillators each on a table — Basic, Pulse, Harmonics, Formant, Sync,
Fold, or a sound given — a position through it, moved by the filter envelope. **Here** it is the registry's
"wavetable" and nothing more: `clip add --instrument wavetable` and `strip add --instrument wavetable` take it, its
parameters are addresses (`<dv>.osc1.table`, `.position`, `.posenv`, …), its `takesSample` makes `set <dv>.sample=`
its table's source as a sampler's sound is, and its window is the generated face (two oscillator modules with their
table switches, the filter, the envelopes). Test: L2 `test_the_wavetable_plays_with_no_code_of_its_own`
(`core/tests/serviceTests.cpp:5483`: dropped as one line, `get` its table, a render that sounds, a square cycle given as
its sample changing that render, the song keeping `osc1.table=sample`). Shot: wavetable-window. Found while building
it: two tests here held the registry's size and its instruments' names as constants — the device-type count now
reads the registry; the refusal's list names the five instruments.

### DR-INST-8 The FM synth (R-INST-8)
**The sound** is the DSP library's (REQ-fm-1…3, its commit 3cb818d): four sine operators, each a ratio of the note, its
phase moved by the operators above it as one of eight algorithms has them, operator 4 fed back through the mean of its
last two outputs, an envelope, level and velocity sense each; pitch on the 32-sample grid. **Here** it is the
registry's "fm" and nothing more: dropped as one line, its parameters addresses (`<dv>.algorithm`, `<dv>.op2.ratio`, …),
its window the generated face (four operator modules with their envelopes, the algorithm and feedback). Test: L2
`test_every_instrument_drops_as_one_line_and_sounds` (`core/tests/serviceTests.cpp:5526`: every instrument the
registry has — the FM synth among them — dropped as one line, a note in its pattern, a render that sounds; one that
takes a sound given one first — written once, it holds every instrument to come). Shot: fm-window.

### DR-INST-9 The multi-sample instrument, its SFZ read here (R-INST-9)
**The sound** is the DSP library's (REQ-multisample-1…3, its commit 2cf1457): an SFZ's regions by key, velocity,
round robin and chance, each its own sound, root, tuning, loop and envelope, choking by group; its parser turns text
into regions and says what it did not read. **Here**: the registry's "multisample" takes a DEFINITION
(`takesDefinition`): its `sample=` is an .sfz. `set <dv>.sample=` checks it reads (`resolveSample`,
`core/service/ServiceEdit.cpp:124` — a text, not audio; an unreadable one refused). At every build Compile reads it
through the service's text provider (`textFor`, `core/service/SolarisService.cpp:628`; `core/Compile.cpp:255`),
parses it with the DSP library's `parseSfz` — each problem a warning (an opcode not read, a bad value) — and asks the
decoder for each sound it names, beside the .sfz; one that cannot be read is said and leaves its regions silent. The
engine hands the text and the sounds to the device at build (`engine/Engine.cpp:102`), never on the audio thread. The
model: `devices[].takesDefinition` / `deviceTypes[].takesDefinition`; the panel asks for "an .sfz"
(`app/widgets/DevicePanel.cpp:584`); `audit` says one with none. Tests: L2 `test_a_multisample_reads_its_sfz`
(`core/tests/serviceTests.cpp:5563`: an .sfz of a soft layer and a loud one in turn — beat by beat 220, 330, 440 Hz,
each alone; a missing sound and an opcode not read said; a file that is no instrument refused; none, said by the audit);
`test_every_instrument_drops_as_one_line_and_sounds` (`:4798`, now giving a definition-taker its .sfz). Mutants (each
red): the definition read as a sound; its sounds looked for beside the song; never handed to the device; an .sfz
refused; its problems unsaid. Not yet: a browser drop of an .sfz onto the device (by `set`, or `--sample` on
`strip add`).

### DR-INST-10 The granular sampler (R-INST-10)
**The sound** is the DSP library's (REQ-granular-1/2, its commit eb1d014): a voice starts a grain every fs/density
samples from its note, each read from a place in its sound (scanned, sprayed) at the note's pitch against its root,
Hann-windowed, panned, the cloud divided by its overlap. **Here** it is the registry's "granular" and nothing more: a
`takesSample` instrument, given its sound as a sampler is (`set <dv>.sample=`), its parameters addresses
(`<dv>.grain.size`, `.grain.position`, …). Test: L2 `test_every_instrument_drops_as_one_line_and_sounds`
(`core/tests/serviceTests.cpp:5526`: the granular given a sound, a note, a render that sounds).

### DR-INST-11 The 808 (R-INST-11)
**The sound** is the DSP library's (REQ-808-1/2, its commit 190ce5f): one voice, a sine whose pitch is punched down to
the note at each attack and glides between held notes, a damped decay, a seeded click, a normalised drive, a tone.
**Here** it is the registry's "808" and nothing more: dropped as one line, its parameters addresses (`<dv>.punch`,
`.glide`, `.amp.decay`, …). Test: L2 `test_every_instrument_drops_as_one_line_and_sounds`
(`core/tests/serviceTests.cpp:5526`: the 808 on a note, a render that sounds).

### DR-INST-12 The guitar (R-INST-12)
**The sound** is the DSP library's (REQ-guitar-1/2, its commit 83b0d60): the piano's modal string PLUCKED — released
from rest at a pluck position by `StringPartialBank::pluck`, each partial from its peak at |sin(nπβ)|/n, the pick's
release a one-pole roll-off — its ring by its pitch, a palm mute and a fret release on its damper, a three-mode body,
eight strings, a bend at once. **Here** it is the registry's "guitar" and nothing more: dropped as one line (`clip add
--instrument guitar`), its parameters addresses (`<dv>.pluck.position`, `.pluck.pick`, `.string.sustain`, `.body`,
`.mute`, …), its window the generated panel. Test: L2 `test_the_guitar_plays_with_no_code_of_its_own`
(`core/tests/serviceTests.cpp:5435`: an A2 at 110 Hz, ten times its neighbours; `set dv_1.mute=0.6` over 20 dB shorter;
the pattern's bend lane two semitones up). Mutant, seen red: the guitar device not forwarding the wheel.

### DR-FX-18 A shaper follows the song's beat (R-FX-18; REQ-fx-shaper-1, REQ-device-12)
The DSP library's `shaper` (DSP `0176d08`): a volume turned down and up in a shape — sine, triangle, saw down, saw up,
square, pump — a note value long, `depth`, `phase`, `stereo`, `smooth`; a strip's effect like any other, its parameters
addresses (`set <dv>.rate=1/8 <dv>.shape=square`) and a generated panel, with no Solaris code for the device itself.
What Solaris adds is where the song is: before every piece the engine hands every device on every rack — the master's
too — a `Device::Transport` (`Engine::renderPiece`, `engine/Engine.cpp:999`; `transportAt`, `:67`): the piece's absolute
sample and, on a steady tempo, the song's samples per beat with no offset (the beat at sample n is n / samplesPerBeat,
whole-sample arithmetic — a render stays byte-identical however time is cut); under a tempo map the line through the
map's beat there. A device that does not follow the beat ignores it. L2 `test_a_shaper_follows_the_songs_beat`
(`core/tests/serviceTests.cpp:5367`: a square eighth on a tone, rendered from beat 2.25 — open at once, closed under
1e−6 on every eighth after, open on every other, and sample for sample the same piece of a render from the start).
Mutant, seen red: the engine never telling the devices (a render from beat 2.25 then starts closed).

### DR-FX-19 A vowel filter: a synth that reads as a voice (R-FX-19; REQ-fx-vowel-1)
The DSP library's `vowel` (DSP `65fbebf`): three formant band-passes at a voice's A, E, I, O, U (Peterson & Barney),
weighted 1, 0.63, 0.32, morphing in log frequency as `vowel` moves 0 … 4, `shift` in semitones, `resonance` narrowing
the bands, `glide` easing the vowel, `mix`, `output`; a strip's effect like any other — its parameters addresses and
automatable (`auto create <dv>.vowel`), its window the generated panel, with no Solaris code for the device. L2
`test_a_vowel_filter_speaks_on_a_strip` (`core/tests/serviceTests.cpp:5403`: a held 110 Hz saw through it, its vowel
automated A for a bar then I, held — by A the 7th harmonic, 770 Hz, 19 dB over the 21st, 2310 Hz; by I the 21st 15 dB
over the 7th, the saw's own 1/k tilt taken out). Mutant, seen red: the registry's `vowel` binding setting nothing
(both bars then measure A).

### DR-FX-6 N34's effects, each the DSP library's (R-FX-6…17)
Each is the registry's and nothing more here: `device add <ch> --type <name>`, its parameters addresses, its window the
generated panel. Test: L2 `test_every_effect_drops_as_one_line_and_passes_sound`
(`core/tests/serviceTests.cpp:5620`: every effect the registry has, added to a synth's strip as one line, a render
finite and sounding — written once, it holds every effect to come). **The effects:** "multiband" (R-FX-6;
REQ-fx-multiband-1, DSP 122d264) — three Linkwitz-Riley bands summing flat, each a Compressor; "gate" (R-FX-7;
REQ-fx-gate-1, DSP f0cb867) — a gate and downward expander with hold and range, keyable (`takesKey`: its key is a
strip's, as the compressor's; unkeyed and quiet it may render silence, which the test allows a keyed type);
"transient" (R-FX-8; REQ-fx-transient-1, DSP bb091f1) — attack and sustain by a fast envelope against two slow ones,
the input to the bit at 0 %; "deesser" (R-FX-9; REQ-fx-deesser-1, DSP ed63bbd) — a high-pass hears the sibilance,
held at the threshold within a range, a dynamic high shelf (split) or the whole sound (wide) turned down;
"saturator" and "bitcrusher" (R-FX-10; REQ-fx-saturator-1 / -bitcrusher-1, DSP cfe7808) — four curves 4× oversampled,
15 samples late (its `latency()`, compensated by DR-MIX-17 with no code here); bits and a sample-and-hold rate;
"phaser" (R-FX-11; REQ-fx-phaser-1, DSP 3569086) — first-order all-passes swept in log frequency, fed back;
"flanger" (R-FX-12; REQ-fx-flanger-1, DSP 0b08063) — a library delay line swept and fed back (built on D-18's fix);
"imager" (R-FX-13; REQ-fx-imager-1, DSP 04ddda6) — mid/side width, mono below a frequency through a Linkwitz-Riley split;
"convolution" (R-FX-14; REQ-fx-convolution-1, DSP ac7aa74) — an impulse response by partitioned FFT convolution. It
`takesSample`, so its file travels exactly as a sampler's does: `sample=` on its `#aeffect` line, `<dv>.sample` to
change it, decoded by the host at the song's rate and handed in at build (DR-EDM-8) — and `device add <ch> --type
convolution --sample <file>` drops it with its file in one line (`core/service/ServiceEdit.cpp:485`: any registry
type that takes a sample; refused for one that does not, for a plugin, and for a file that is not audio). Its 256
samples of latency are compensated by DR-MIX-17 with no code here. Test: L2 `test_a_convolution_reverb_takes_its_
response` (`core/tests/serviceTests.cpp:5648`): an impulse response leaves the song as it sounded at mix 0.6, to
3e−17 — the latency compensated; its file read back; the refusals. "pitch" (R-FX-15; REQ-fx-pitch-1, DSP 8ceaa31) —
two crossfaded taps sweeping a delay line; its latency (1 + half its window) compensated, no code here. "vocoder"
(R-FX-16; REQ-fx-vocoder-1, DSP 051007a) — a channel vocoder whose carrier is its key: routed by a sidechain send as a
compressor's (DR-MIX-15), with no code here but the audit's (`core/service/ServiceModel.cpp:679`: a key-taking type
with no Sidechain switch always listens). Test: L2 `test_a_vocoder_speaks_through_its_key`
(`core/tests/serviceTests.cpp:5684`): a voice's strip through a synth on another strip, RMS 0.0127; the carrier muted,
exactly silent; the audit quiet about its key. "tuner" (R-FX-17; REQ-tuner-1, REQ-device-11, DSP 6c07198) — YIN,
the audio untouched; its readings (frequency, note, cents, level) carried to the model and `get` by DR-FX-17.

### DR-FX-17 A device's readings, carried up (R-FX-17; docs/discussion.md Round 12, part 2)
A device that MEASURES (the DSP library's `DeviceType::readings`, REQ-device-11 — today the "tuner") has its readings
in the model and at `get` while playing, with the audio thread's rules kept. At build the engine lists the devices
that measure — strips in processing order, each rack in order, then the master's rack — and sizes `readingValues`
(`engine/Engine.cpp:224`); after each render it stores each device's readings there (`:1107`, stores only). The Player
copies them into the live-value ring's slot beside the bindings' values (`keepLive`, `core/Player.cpp:309`), so a
reading is stamped with the same frames and engine generation, and `liveReadings` (`:347`) gives the slot the
listener hears now through `pickSlot`. `buildLive` lists, from the same graph in the same order, which device each
slot is (`core/service/ServiceTransport.cpp:71`); the live refresh writes the values into `devices[].readings[].value`
(`:283`). The model's `devices[].readings` (name, label, unit — `RegistryModel.cpp`) are the registry's; the value is
a measurement, so the stable dump omits it (`AppModelCodec.cpp:462`). Every command rebuilds the model from the
registry, so stopped, a reading is 0. `get <dv>.<reading>` answers the model's value (`core/service/SolarisService.cpp
:1269`); `set` on one is refused naming it a reading (`:1268`). The API document lists each type's readings and the
read-only address. At most 64 readings report (`Player::kMaxReadings`). Test: L2 `test_a_tuner_reads_while_playing`
(`core/tests/serviceTests.cpp:5715`): the default synth's A4 read as A3 — its sub-octave square sets the period, as
YIN must say — and with that oscillator off, 440.004 Hz (+0.02 cents), note 69, through `get` and the model while
playing; 0 stopped; `set` refused; the stable dump names the readings but not their values.

The window (Round 12, part 3): a device that measures shows a band under its header (`DevicePanel::readingsH`,
`app/widgets/DevicePanel.cpp:511`; drawn at `:837`) — the note large (26 px: a display, off the editor's ramp on
purpose), a needle across −50 … +50 cents easing to each reading at `kCatchUpMs` — a value catching up to a source it
does not control — and greener the nearer the centre, from the EASED value so the colour never jumps; the pitched
light (with no pitch a dash, the needle home and faded) eased alike (`:363`); the frequency, the cents (a rounded
zero is "0", never "−0") and the level small. Drawn from the model only. The rows begin below the band. UI test
`test_a_tuners_window` (`app/tests/ui/uiTests.cpp:6099`): the band in the window (stopped: no pitch); a panel bound to
the service's model with A4 +23.45 ct put in — the needle caught at 12.63 ct mid-tween and the light at 0.54, then
exactly there; the pitch gone — a dash at once, the needle and light caught on their way home; a synth has no band.
Shot `tuner-window` at both sizes (a synth holding A4: "A4", the needle centred and green, 440.0 Hz · 0 ct).

### DR-MIDI-3 Expression lanes (R-MIDI-3)
A pattern carries expression lanes beside its notes (`model/Project.h` `ExprPoint`; `#expr lane= at= value=` in the
`.slp`, docs/project-format.md §8): `bend` −1 … +1, `cc0` … `cc127` 0 … 1 (`mod` is `cc1`), linear between points,
held at the ends (`exprAt`). `expr add <pt> --lane --at --value` (a point already at that beat moved; the pattern
grows to hold it), `expr delete`, `expr clear [--lane]` — edits like any (`core/service/ServiceMidi.cpp:112`),
`patterns[].expr` in the model. The compile turns a clip's `bend` and `cc1` lanes into control events on its strip
(`core/Compile.cpp:463`): each repetition's value from its start, each point on its sample, a ramp a step every
64 samples (the engine's control period, `kControlSamples`), at rest where the clip ends; the engine applies them
in time order with the notes — a wheel before a note at the same sample — through the device seam
(`pitchBend` / `modWheel`), and a seek restores the last value of each before it (`engine/Engine.cpp:542`). Other
controllers are kept and go out in MIDI files, but no instrument plays them. MIDI files carry them both ways:
`parseMidi` reads pitch bend and controllers per track, `pattern import` makes them lanes (a repeated value
dropped), `pattern export` / `project export-midi` write them (each point, a ramp stepped every 1/32 beat; a song's
clip back at rest at its end). The keyboard's own wheels play the armed strip at once (`Player::midi`,
`core/Player.cpp:490`, through the engine's live ring) and a recording keeps them: the take's lanes thinned to where
the wheel moved by more than 0.005 or a sixteenth on, its last move kept; an overdub writes a moved lane over the
take's span. L2 `test_expression_lanes_play_the_wheels` (at rest 440 Hz, half way up a two-beat ramp a semitone,
held two after its last point, the next clip at rest, a seek landing bent; refusals; the .slp; undoable),
`test_expression_lanes_go_through_midi_files` (the curve to 1 %), `test_the_keyboards_wheels_play_and_record`; model
`test_expression_lanes_round_trip_and_are_checked`. Mutants: no ramp steps, a seek forgetting the wheel, no rest at a
clip's end — each red. **The roll's lane.** Vel | Bend | Mod beside the piano roll's lower lane choose what it shows
(the view's, cross-faded; `laneTabRect`, `app/widgets/PianoRoll.cpp:174`): the velocities, or the pattern's `bend` or
`cc1` lane as a curve through its points (linear between, held to the pattern's start and end; bend's centre line
drawn), each point keyed by (lane, beat), fading in and out, its value easing to a change made elsewhere. On a
wheel's lane: a click away from a point adds one at the snapped beat and the height's value (to a hundredth), a
point dragged up or down follows the pointer and lands as ONE `expr add` at its beat, a double-click (or a
right-click) removes it (`expr delete`). UI `test_the_rolls_lane_draws_the_wheels`; shot `piano-roll-bend`. Mutants:
the lane switch snapping, a shell's value snapping — red.

### DR-ARR-1 Markers and sections (R-ARR-1)
A marker is Interstellar's (law 16 — its `mk_` ids, `name`, `at`, `note` and `marker add <name> --at`) in the song's
beats, written `#amarker id=mk_1 name=Chorus at=16.0 length=8.0 note="…"` (`model/Project.cpp:763`; `length` and
`note` only when set; in the id space, so `nextId("mk")`; docs/project-format.md §9a, docs/audio-format.md §2.10). With
a `length` it is a SECTION; without, a point whose section runs to the next marker after it, else the song's end
(`Project::sectionEnd`, `model/Project.cpp:96`) — published as `markers[].end` beside `at`, `length`, `name`, `note`, by
time (`core/service/ServiceModel.cpp:401`). `marker add <name> --at [--length] [--note]` (prints its id), `marker rename
<mk> <name> [--note]`, `marker move <mk> [--at] [--length]`, `marker delete <mk>` — edits like any, kept sorted by time
(`core/service/ServiceArrange.cpp:376`); `ls` lists them with their sections. `transport seek <mk>` goes to its beat and
`transport loop <mk>` loops its section (`markerSpan`, `ServiceArrange.cpp:31`; `core/service/ServiceTransport.cpp:680`,
`:730`), an empty section — the last point at or past the song's end — refused. L2
`test_markers_name_places_and_sections`; model `test_markers_round_trip_and_name_their_sections`. Mutants: a point's
section ignoring the next marker, a loop taking only its length, markers outside the id space — each red. **The ruler.**
The ruler grows a band at its top for them (`Timeline::kMarkH` 13 of `kRulerH` 35.75, `app/widgets/Timeline.h:100`; the
bars' labels and the brace's grip — the lower half of the bars' band — below it): a flag per marker at its beat, its
name MEASURED (the width a press hits is the one last drawn), a section a band to its `end`, a hairline down through
the bars (`paintMarkers`, `app/widgets/TimelineMarkers.cpp:224`). Keyed by id: a marker fades in and out, a move made
elsewhere eases there, a song opened places them (`advanceMarkers`, `:87`). On a flag (`markerUnder`, `:65` — the last
drawn on top), each ONE line (`markerGesture`, `:121`): a click → `transport seek <mk>`; a double-click → `transport
loop <mk>`; a drag moves it on the grid you see, its section with it, the pointer's while held, and `marker move <mk>
--at <b>` on release, where it was let go; a right-click → Loop its section · Rename… (cosmo's field → `marker rename
<mk> "<name>"`) · Delete marker · Copy ID. A right-click on the ruler away from a flag → Add marker here (`marker add
"Marker <n>" --at <b>`). Elsewhere on the band the ruler is the ruler (a seek, a scrub). UI `test_markers_on_the_ruler`;
shots `markers`, `markers-arriving`. Mutants: a shell's move snapping, a dropped flag not where it was let go, a
double-click seeking, markers bound before a song change clears them (a bug found while building it) — each red.

### DR-ARR-2 Time edits across the song (R-ARR-2)
`time insert --at <b> --length <b>` and `time delete --from <b> --to <b>` — ONE edit each across the whole song
(`SolarisService::timeCommand`, `core/service/ServiceArrange.cpp:43`). What crosses an edge and what lies inside are
decided before anything moves (`:60`). **Insert:** every clip starting at or after `--at` moves `--length` later; one
starting before it and running over it stays and is named in the output (no `clip split` yet, R-EDIT-1) — an
automation's clip among them; an automation's points are its own beats and never move (`:100`, R-AUTO-14); markers at or after it move, a section holding it grows (`:101`); chords as sections (`:121`,
R-ASSIST-1). **Delete:** clips (an automation's too)
and markers wholly inside the span go; what follows moves earlier by its length; a note clip, an automation clip, or an
audio clip set to loop, that starts before `--from` and runs into the span is shortened by the overlap (what repeats
keeps repeating);
any other clip across `--from` or `--to` is REFUSED, naming it — nothing changes; a section or a looping span across an
edge shrinks, one whose head is in the span starts at `--from`. **The loop** (the session's, `:147`) moves, grows or
shrinks the same, ends when taken out entirely, and stays where it is on undo. The output says what moved, what went,
and what crossed. **The ruler's menu** (right-click away from a flag, `app/widgets/TimelineMarkers.cpp:201`): Insert a
bar here (`time insert --at <the bar's start> --length <a bar>`), Delete this bar, and — with a loop — Delete the loop's
time. L2 `test_time_insert_and_delete_move_the_whole_song`; UI `test_time_edits_from_the_ruler` (a clip moved eases
there); shot `ruler-menu`. Mutants: the inside clips kept, insert leaving automation points, a plain audio clip let
across an edge, the ruler inserting at the pointer and not the bar — each red. Found while building it: a clip moved
INTO the span by the shift was then taken as inside and deleted — the inside is decided first now.

### DR-ARR-3 Lane groups (R-ARR-3)
A group is organisation only, as a lane is: `#alanegroup id=lg_1 name=Drums [folded=on]` written before the lanes, each
of its lanes naming it (`#alane … group=lg_1`; `model/Project.cpp:709`; a lane naming a group the song lacks is refused).
Its lanes are side by side in the lane order — `gatherLaneGroups` (`model/Project.cpp:1634`) gathers each group at its
first lane's place and drops a group with no lane; a song opened scattered is gathered and said so. `lane group <ln> …
[--name]` (`core/service/ServiceArrange.cpp:184`) makes a NEW group of the lanes, gathered at the first one's place in
their order (a lane leaves any group it was in; prints the id); `lane ungroup <lg>` lets them go; `lane move <lg> --to
<i>` moves the group whole, never into another group's run (`:227`); a LANE moved (`core/service/ServiceEdit.cpp:584`)
joins the group whose run it lands inside and leaves its own when it lands outside it; a lane deleted can leave its
group empty, and the group goes. `set lg_1.name=` / `set lg_1.folded=on|off` (`core/service/SolarisService.cpp:1332`), `get`
(and a lane's `.group`). `clips move "<ac> …" --by <beats>` (`:337`) moves clips in time as ONE edit, refused whole
when a clip is not the song's or would start before beat 0 — a folded group's clips dragged together, or any
selection. The model: `lanes[].group`, `groups[]` (id, name, folded, its lanes — in the order of their first lanes);
`ls` prints a group before its first lane. L2 `test_lane_groups_gather_fold_and_move_together`; model
`test_lane_groups_round_trip_and_are_checked`. Mutants: groups not gathered, a lane landing in a run not joining it, an
emptied group kept — each red (the last only once the test read the song itself: the model lists no empty group).
A lane's `.group` (`core/service/SolarisService.cpp:1303`): `set <ln>.group=<lg>` joins it (gathered with it),
`=none` leaves its group, placed right after the run it left. **The timeline.** An open group's lanes carry a bar down
their headers' edge (`app/widgets/Timeline.cpp:1766`, from where the rows ARE) and, on the first, a chevron and the
group's name in the header's bottom band. Folded, the group is ONE row (`:148`): its lanes fade out, the rows below
ease up, its clips travel into it — each lane's clips in their own band of the row (`clipBox`, `:408`; band and height
eased), so they never lie on one another — and its header says the group's name, "a group, folded", "▸ N LANES". The
chevron turns as it folds and opens (`:781`), and a click on it or on the name (`groupToggleRect`, `:420`) is ONE `set
<lg>.folded=on|off`. A clip dragged on a folded row carries the group's clips (`:1080`; never before beat 0), the
pointer's while held, then ONE `clips move "<ac> …" --by <beats>` where they were let go (`:1156`). The folded row's
menu (`:1265`): Open group · Rename group… · Ungroup · Copy ID; a lane's (`:1297`): New group · Into group… · Leave
group · Fold group · Ungroup. UI `test_lane_groups_on_the_timeline`; shots `lane-groups`, `lane-group-folded`,
`lane-group-folding`. Mutants: the chevron snapping, a folded row dragging one clip, folding keeping the lanes — each
red.

### DR-REC-1 Record audio (R-REC-1)
**The seam.** `IAudioIn` (`core/AudioIn.h`) beside `IAudioOut`: `open(device, rate, channels, buffer)`, `read(frames)` that
never blocks (what has not arrived is silence, counted by `missed`), `latency()`; a host gives one through
`Host::audioIn` — none, and arming an audio line is refused by name. **The song.** A strip's `input` (an input port —
`port add <name> --dir in [--channels 1|2]`, the machine's settings mapping it to a device as any port, `port.<name>=
<device>:<channel>`) and `monitor` (`auto` | `on` | `off`) are the song's (`#atrack … input=prt_2 monitor=off`, written
when set; checked: the input an input port, `model/Project.cpp:1357`), set by `set <ch>.input=<port>|none` /
`set <ch>.monitor=` (`core/service/SolarisService.cpp:1138`). **Arming** (`strip arm <ch> [--input <port>]`,
`core/service/ServiceInput.cpp:552`): an audio line arms for its input — `--input` sets it first, and only then is the
line an edit; with no input it is refused naming the song's input ports (or `port add`); one strip armed at a time, as
the keyboard's. **Monitoring** (`monitorHears`, `:134`): `on` heard whenever armed, `off` never, `auto` while stopped or
recording — not while the song plays back. Stopped, the input monitor (the song's mix without its clips, R-MIDI-2's)
plays it; playing, the song's player does — the input opened beside the output (`attachInput`, `:141`; `openInput`,
`:102`, on the port's device). A change of `.monitor` on the armed strip is heard at once (`SolarisService.cpp:1193`). **The path.** Each
cycle the player reads a block of the input before it renders (`Player::input`, `core/Player.cpp:75`) and the engine
hears it through the armed strip's chain — its rack, its fader, its meter — as a clip of it (`Engine::liveInput`,
`engine/Engine.cpp:567`; mixed at `:1029` by where each piece sits in the block; sized at build: no allocation). A
count-in reads it and neither hears nor keeps it. **Recording** (`transport record` with an audio line armed,
`core/service/ServiceTransport.cpp:461`): `startTake` (`core/Player.cpp:33`) — the audio thread pushes what it reads into a
lock-free ring (never waiting; a frame the ring cannot hold is counted and said at the stop) and a recorder thread of
the player's drains it into the take every 2 ms (`:45`), so a long take needs nobody to pump it. At the stop the audio
thread joins first, so nothing more can come (`ServiceTransport.cpp:497`), and `commitAudioTake`
(`ServiceInput.cpp:117`) lands it — ONE undo, "Record": frame i of the take is what the song played at sample
`from + i − shift`, `shift` = (the output's latency + the input's) × rate + the engine's own output lag; the clip is
put where recording began on the tick grid and the take cut from that very sample (silence before a first frame read
late); `transport punch <from> <to>` (`:513`, the session's, in the model as `transport.punchFrom/To`) keeps only
`[from, to)` and starts the clip at `from`. The file: `<song folder>/Recordings/<strip> <n>.wav`, 32-bit float, the next
number no file (and no clip) has, through `Host::writeWav` (which now makes the folder); `record.finished` carries
`seconds` (a field added at the end); a song with no file yet is refused, `--overdub` (the keyboard's) too. L2
`test_record_audio_lands_where_it_was_played` (a cable from the output back into the input, the two reporting 20 + 10
ms: the burst the render puts on sample 48 000 is on sample 48 000 of the take; punched 1 … 3, a clip at 1 two beats
long with the burst a beat in; one undo), `test_an_armed_audio_line_is_monitored_as_set` (auto heard stopped, on the
port's device; off stops it at once; auto not heard while the song plays back; no capture refused); engine
`test_live_input_is_heard_through_its_strip`. Mutants: no latency taken out, only the output's, punch ignored, auto heard
during playback, the engine reading the input from each piece's start — each red. **The host's capture.**
`host/AudioInPulse` (the PulseAudio simple API, which PipeWire serves): a thread of its own reads a buffer at a time
(blocking) and pushes into an `AudioRing` without waiting; the latency is read on that thread and handed over by an
atomic (pa_simple is one thread's). `host/AudioInPipewire`: PipeWire's realtime callback pushes what it captured, small
buffers as the output's, linked within two seconds or refused naming why. Both: half a second of ring; the player
reads it and never waits — missing frames are silence and counted (`missed`). `AudioRing::push` (`host/AudioRing.h:94`)
is the capture's writer: what fits goes in, the rest dropped and counted. The service tries the setting's driver
(`Host::audioInFor`, as the output's) and falls back to PulseAudio, saying so once
(`core/service/ServiceInput.cpp:121`); the CLI, the web face and the window wire both. Ring
`AudioRing: a capture's push never waits …`; L2 the fallback said in `test_an_armed_audio_line_is_monitored_as_set`.
Mutants: the overflow not counted, the driver setting ignored — red. Not run on this machine's real input (the user's
microphone): a live recording is owed. **The window.** An audio or instrument strip's M | S row is M | S | R in the mixer
(`armRow`, `app/widgets/MixerDock.cpp:116`; a bus and the master keep M | S), R lit red when the strip is armed, eased.
R (`:1574`) disarms an armed strip (`strip arm none`); on an audio line with no input it offers the song's input
ports — "Arm with Mic" is ONE `strip arm <ch> --input <port>` — or, with none, "Add an input port (In)" (`port add In
--dir in`); else it arms (`strip arm <ch>`). An audio line's menu (`:1713`): Input… (the input ports, the current one
marked, No input, Add an input port… — a name typed in cosmo's field) and Monitor… (Auto · On · Off — `set
<ch>.monitor=`). The ruler's menu adds Punch the loop's span (`transport punch <loop>`) and Punch off
(`app/widgets/TimelineMarkers.cpp:209`); the punch is a red line atop the bars (`Timeline::punchRect`,
`app/widgets/Timeline.cpp:412`), eased in and out, its ends travelling when it moves (`:757`). The song bar's Record
records whichever strip is armed. UI `test_recording_audio_from_the_window` (the Rig's host now has a capture that
hears nothing); shot `recording-audio`. Mutants: the R light snapping, R arming with no input asked, the punch
snapping in — each red. Loop takes are R-REC-2's (N17).

### DR-REC-2 Takes and comping (R-REC-2)
A clip's TAKES (`Clip::takes`, `#take src=…` under its `#aclip`) and its COMP (`#comp take= from= to=`, song beats) are
read and written in place (`model/Project.cpp:711`); `Clip::takeAt` (`:70`) is the take that plays at a beat — the comp
range's (the latest written where they meet), else the LAST; a comp naming a take the clip lacks is refused. **Made by
recording while the song loops**: the recorder thread notes each jump of the song's position — a loop turning — with
the take's frame there (`Player::Take::marks`, `core/Player.cpp:54`); at the stop (`core/service/ServiceInput.cpp:162`)
every frame is placed where it was HEARD (its read position minus the latency, as DR-REC-1 says), and a frame heard
before the loop's start just after it turned is the END of the pass before (`:176`) — so each pass is exactly what was
heard over the loop. A pass that heard less than half the loop is dropped; two or more make ONE clip spanning the loop,
every pass a take — its own file, `Recordings/<strip> <n>.wav`, the next free numbers — the clip's own source the last;
one whole pass is a plain clip; none, as before. `record.finished` says `takes`. `clip comp <ac> --take <n> [--from
--to]` (`core/service/ServiceArrange.cpp:383`, the whole clip without a range): ONE edit; the range overwrites what it
covers — a range it cuts keeps its head and its tail — neighbours of one take are merged; refused on a clip with no
takes, a take it lacks, an empty range. **Played** (C5, `core/Compile.cpp:385`): the clip's span cut at its comp's
edges, runs of one take merged, each a region of its take — overlapping 5 ms either side of a seam with linear fades
across it, so a cut never clicks; the clip's own fades at its ends. The model: `clips[].takes`, `clips[].comp`.
**Found while building it (D-12):** a recording's clip was given to `clip add` as `Recordings/<name>` — read against the
WORKING folder, not the song's — so it pointed elsewhere unless the two were one; the full path is given now and kept
relative to the song. L2 `test_a_looped_recording_makes_takes_and_a_comp_chooses` (a capture whose level rises 0.1 a
pass as heard: each take one level throughout — 0.1, 0.2, 0.3 — a short fourth pass dropped; take 1 over 4.5 … 5 heard
there, 0.2 at the seam, done 5 ms either side; a range over part of another keeps the rest; undo; refusals); model
`test_takes_and_their_comp_round_trip`; the D-12 guard in `test_record_audio_lands_where_it_was_played`. Mutants: the
turn's tail given to the next pass, the compile ignoring the comp, no crossfade, an overlapped range losing its rest,
D-12's path — each red. **The window.** The model publishes the comp RESOLVED — `clips[].plays`, runs covering the clip,
each the take heard there (`core/service/ServiceModel.cpp:354`) — so a front end only draws it. A clip with takes says how
many at its top right where there is room beside its name (`app/widgets/Timeline.cpp:1422`); its menu offers Show takes
· Hide takes (`:1383`; the view's — `showTakes`, `app/widgets/TimelineTakes.cpp:29`, re-binds at once): a row per take
under the clip's lane (`Timeline.cpp:182`; keyed rows, so the rows below slide down and back, eased), its span drawn
faint and lit where `plays` gives it the take (`paintTakes`, `:145`) — cross-faded from what was lit when the comp
changes (`bindTakes`, `:51`). On a take's row (`takeGesture`, `:80`): a drag across it, on the grid you see and
inside the clip, is ONE `clip comp <ac> --take <k> --from --to` (the pointer's range shown while held); a
double-click, that take for the whole clip (`clip comp <ac> --take <k>`). UI `test_takes_under_their_clip`; shot
`takes`. Mutants: the lit takes snapping over, a drag taking the whole clip, Show takes changing nothing until the next
edit — each red.

### DR-EDIT-1 Audio editing on the lanes (R-EDIT-1)
**Reverse.** `Clip::reverse` (`#aclip … reverse=true`, written only when set — `model/Project.cpp:490`), `set <ac>.reverse=
true|false` (`core/service/SolarisService.cpp:1256`; a note clip refused) and `get`; compiled to `Region::reverse`, which the
engine reads from its span's end (`engine/Engine.cpp:751`) — a loop's every repetition backwards; a clip with takes
mirrored stretch by stretch (`core/Compile.cpp:438`). **Crossfades** (C6, `heardFades`, `core/Compile.cpp:123`): an audio
clip's fade in grows to cover a clip of its strip on its lane that began before it and ends inside it, its fade out one
that begins inside it and ends after — a clip wholly inside another, or one on another lane, is mixed; the compile plays
the grown fades, the model publishes them (`clips[].fadeInHeard`, `.fadeOutHeard`, `core/service/ServiceModel.cpp:347`)
and the clips keep their own. **Split** (`splitClip`, `core/service/ServiceAudioEdit.cpp:94`): `clip split <ac> --at <b>`,
`b` on the tick grid strictly inside the clip; the second half a new clip after the first in the list, its id printed —
the halves' `in`/`out` meet where the cut is (a reversed clip's first half its span's end), the fade in the first's and
the fade out the second's (each no longer than its half), the comp cut in two, every take kept by both. A looping clip
(`:108`) is cut only within half a tick of where a repetition begins — each half loops from its start — and the refusal
names the nearest such beats inside it; a note clip is refused (R-CLIP-3). **Consolidate** (`consolidateClips`, `:280`):
audio clips of one strip; a copy of the song holding only them and their strip — no devices, no sends, no bindings, the
strip at 0 dB, centred, to a master at 0 dB — compiled and rendered by an engine of its own from the first one's start
to the last one's end (its output latency trimmed; `renderClipsRaw`, `:320`, shared with the analysis tools, DR-ASSIST-1),
written as `Bounces/<strip> <n>.wav` (`writeBounce`, `:360`, shared with a strip's bounce, DR-PERF-3: 32-bit float,
the next free number, a clip's name counted as taken so an undone bounce is never overwritten); the clips deleted and one `clip add`
in their place on the first one's lane — one edit. **Slice** (`audioEditCommand`, `:165`): `--every <b>` cuts every `b`
from the clip's start; `--transients` decodes what the clip plays (its span, mixed to mono, reversed when the clip is)
and cuts at the DSP library's `detectOnsets` (`:213`, REQ-onset-1; `--sensitivity`), dropping a cut within half a tick
of either end; each cut a `splitClip`. It prints the first piece; `made:` names the others in order. `--every --kit`
(`sliceToKit`, `:235`): N = the clip's length over `b`, rounded (1 … 64); a `clip add --instrument sampler --sample <the
clip's file> --lane new` at the clip's place and length, its Sampler set `mode=slices slices=N start=in/T end=out/T`
(T the file's length) and `reverse` as the clip, its pattern N notes — note k at k·len/N, root + k (root the registry's
default; a reversed clip root + N−1−k), velocity 127; the clip itself stays. Refused: a looping clip; both or neither of
`--every`/`--transients`; `--kit` without `--every`; `--sensitivity` without `--transients`; a step not shorter than the
clip; more than 64 slices; transients of a comped clip. L2 `test_a_clip_is_split_and_plays_the_same` (the halves' render
equal to the whole's, to the sample, with gain and fades; a reversed clip the file backwards, split equal too; a loop cut
at 12 not 10, its render equal), `test_overlapping_clips_on_a_lane_crossfade` (a level of 0.5 held through the overlap;
another lane, or inside, 1.0), `test_consolidate_renders_clips_into_one` (the file raw — the tone, the gap silent, the
reversed second at −6 dB; the song after within 1.5e-8 of before, through a filter, fader and pan), `test_slice_cuts_a_
clip_or_makes_a_kit` (every beat: equal render; transients: 5, 6, 6.5, 7 — reversed, each swell's start; the kit alone
equal to the clip alone to the sample, forwards and reversed). Mutants: reverse ignored by the engine, no crossfade, the
second half reading from the start, a reversed split keeping the start, consolidate keeping the fader, a reversed kit in
the span's order, a loop cut anywhere, transients found in the file's order — each red. **The window**
(`app/widgets/TimelineClipEdit.cpp`). An audio clip wide enough (four grips) and not in a folded band takes, under its
name, a fade handle at each end — at its OWN fade — and a gain line across it: the amplitude, 0 dB a quarter of the way
up its body, +12 dB at the top (`gainLineY`, `:142`), so a press at the clip's middle still takes the clip. Pressed
(`clipEditZoneAt`, `:162` — a handle within a grip across and 1.5 units down; the line within one unit, between the
handles), the pointer is the value: a handle's fade from the clip as DRAWN, to the tick, never past the other fade; the
line's level to 0.1 dB (`clipEditGesture`, `:182`); let go, ONE `set <ac>.fadeIn|fadeOut|gain=…`, the drawing left
where the pointer was — a refusal eases it back. The pointer says ↔ on a handle and ↕ on the line, as the press reads
them. Each clip's fades, its fades as HEARD (`clips[].fadeInHeard/OutHeard`, so an overlap's crossfade is drawn as an X),
its gain and its reversed mark are eased per clip (`advanceClipEdit`, `:88`); drawn (`paintClipEdit`, `:266`): the gain
line, what each heard fade takes away shaded with a line along it, the handles, "reversed" by the line at its right.
Its menu (`clipEditItems`, `:332`, `app/widgets/Timeline.cpp:1388`): Split at the time bar (where the time bar is inside
it) · Reverse / Play forwards · Consolidate — the clips it runs into on its lane through its strip, touching or
overlapping, "Consolidate N clips" when more than itself · Slice ▸ Every bar / beat / half beat · At its hits (not a
comped clip) · To a kit, a slice a beat (no Slice on a loop). Ctrl+E — the keymap's `lanes.split`
(`core/service/Keymap.cpp:23`) — and Edit › Split Clip at the Time Bar split the clip clicked last at the time bar
(`app/App.cpp:929`). UI `test_a_clips_fades_gain_and_edits`; shots `clip-edit`, `clip-edit-dragging`. Mutants: a dragged
fade drawn from the model, the fades snapping, the line sent while still held, Consolidate only the clip itself, the
heard fades drawn as the clip's own, Ctrl+E doing nothing — each red.

### DR-WARP-1 Warp and pitch on audio clips (R-WARP-1)
An audio clip's `warp`, `bpm` and `pitch` (`#aclip … warp=true bpm=100.0 pitch=-3.0`, each written only when set —
`model/Project.cpp:491`). `clipSecondsPerBeat` (`core/Compile.cpp:31`): a warped clip's beat is 60/bpm seconds of its
file, else the song's — so `clipLengthBeats` gives a warped clip its span's own beats, and `clip split` / `clip slice`
cut it at them. **Played** (C7): `stretchKey` (`:33`) names what a warped or pitched clip plays —
`stretch:<bpm/song bpm>:<semitones>:<in>:<out>:<src>`, exact numbers; empty for a plain clip or one with takes — and the
compile asks the sound provider for that key in place of the file (`:454`), the region reading the whole stretched
sound. The service makes a key's sound once (`pcmFor`, `core/service/SolarisService.cpp:645`): the file's span through
the DSP library's `timeStretch` at the song's rate (`stretched`, `core/service/ServiceAudioEdit.cpp:54`), kept beside the
decoded files — sixteen at most before those no clip plays go — and a render at another rate stretches the file decoded
at that rate (`core/service/ServiceRender.cpp:163`). A song tempo change changes the key, so the next build stretches
again. **Set** (`core/service/SolarisService.cpp:1231`): `warp=true` with no `bpm` listens first — `tempoOf`
(`core/service/ServiceAudioEdit.cpp:82`, the DSP library's `estimateTempo` on the span's channel mean, to a thousandth)
— in the same edit, and nothing heard refuses naming `.bpm`; `bpm` 20 … 999, `pitch` −24 … +24; a note clip and a clip
with takes refuse all three. `clip add --src … --warp` (`core/service/ServiceEdit.cpp:802`) listens on import; `--warp`
without `--src` is refused. A kit (`clip slice --kit`) of a warped or pitched clip is refused. Model `clips[].warp`,
`.bpm`, `.pitch`. L2 `test_a_warped_clip_follows_the_tempo` (a 100 bpm groove heard at 100.000 and its 8 beats its own;
a 2 s sine at its own 60 bpm warped into a 120 song — its 2 beats in one second at 440.000 Hz, then nothing; +12
semitones 880.000 Hz as long, twice the same bytes; the song at 60 — two seconds; split at its own beat; refusals).
Not covered at L2: a render at another rate (the fake decoder hands back what it was given at any rate). Mutants: the
compile playing the file, the song's beats for a warped clip, the ratio upside down, no listening (120 always), the
pitch never moved — each red. **The window** (`app/widgets/TimelineClipEdit.cpp`): an audio clip's menu
(`clipEditItems`, `:376`) offers Warp to the song's tempo / Unwarp (`set <ac>.warp=true|false`), Tempo… (the rename field
with its tempo; what is typed is ONE `set <ac>.bpm=…`) and Pitch ▸ (+12, +7, +5, +1, 0 as recorded, −1, −5, −7, −12; the
current one marked "· now"; ONE `set <ac>.pitch=…`). The clip says its tempo when warped and its pitch when moved —
`clipTag` (`:67`), "60 bpm · +7 st" — at its left past its fade in, above its gain line, cross-faded from what was shown
when it changes (`advanceClipEdit`, `:88`; LIVE `clipTagAlpha`, `:80`). UI `test_a_clip_warped_from_its_menu`; shot
`clip-warp`. Mutants: the tag snapping, Tempo… setting the pitch, a pitch item sending the wrong number, Warp always
on — each red.

### DR-TEMPO-1 A tempo map (R-TEMPO-1)
`#atempo id= at= bpm= [ramp=true]` (`model/Project.cpp:766`; validated after beat 0, rising, 20 … 999). **The map**
(`engine/Tempo.h:23`): plain data in the engine — segments of a constant tempo (T1) or one gliding linearly in beats to
the next (T2), each with its first beat and sample; `add` (`:45`) appends a change (a glide bends the segment before
it); `sampleAt` (`:74`) and `beatAt` (`:81`) are closed-form both ways (a glide's time is (60/k)·ln(bpm(b)/bpm0)). One
constant segment computes exactly the old `b · (60/bpm · rate)`. `tempoMapOf` (`core/Compile.cpp:16`) builds the song's.
**The compile** (C8): every clip start, take run, fade, loop span, note, wheel step and the song's end through `S(b)`
(`:332`) — b·spb, as ever, for one tempo; the map in the clock (`:595`). **The engine's clock** (`engine/Expr.h:251`): a
formula's `beat` and `bpm` are the map's; curves stay compiled in beats at the start's samples-per-beat and are read at
the song's beat (`curveSample`, `:259`). **The service**: `sampleOfBeat` / `beatOfSample`
(`core/service/SolarisService.cpp:607`) for playback's start, seek, the loop, the time bar heard and stopped, a
recording's takes and notes; the count-in is its bars at the tempo where it starts; the metronome reads the playing
engine's map (`core/Player.cpp:521`). `tempo add|move|delete` (`core/service/ServiceArrange.cpp:312`): after beat 0 (the
start's is `project.bpm`), one to a beat, sorted; refusals name the way. `time insert|delete` carry them (`:141`).
`project.bpm` is refused as a formula's target naming `tempo add` (`core/Bindings.cpp:27`). A warped clip under a map
asks for `stretch:map:<its bpm>:<semis>:<in>:<out>:<at>:<song bpm>:<changes>:<src>`; `parseStretchKey`
(`core/Compile.cpp:49`) rebuilds the map at the stretch's rate and gives the DSP its own beats every sixteenth → where
the song hears them. The MIDI file (`core/service/ServiceMidi.cpp:306`): each change at its tick, a glide as a step every
sixteenth at its middle's tempo. Model `tempos[]` (with `seconds`), `transport.tempo`. L2
`test_a_tempo_map_moves_time` (clicks at beats 2, 6, 8 under 120 → 60 at 4 land at 48000, 192000, 288000; a glide to
120 by 8 at its closed form — 173849 and 229084 — to the sample; the tempo at the time bar 90 mid-glide; the MIDI file's
19 tempos; a warped sine across the change at 440.000 Hz both sides; a time insert carries the change; refusals),
`test_the_metronome_follows_the_tempo_map` (clicks at 0, 0.5, 1, 2, 3 s, none at 1.5 s; played from beat 3, a click at
once and a second on). Mutants: the compile ignoring the map, a glide held, the metronome on the start's grid, the
transport's sample from the start's tempo, a time insert leaving the tempo, the MIDI file keeping one tempo, a warped
clip on the start's ratio — each red. **The window** (`app/widgets/TimelineTempo.cpp`): each change a flag on the
ruler's band at its beat saying its tempo ("140 bpm"), a glide into it a slope from the change before (`paintTempos`,
`:178`); keyed — a change fades in and out, one moved or retimed elsewhere eases or counts there (`advanceTempos`, `:87`;
LIVE `tempoAlpha`, `tempoBeat`, `tempoShown`; `tempoRect`, `:38`). Pressed before the markers (`tempoGesture`, `:113`):
dragged, its flag is the pointer's on the grid and the release ONE `tempo move <tp> --at`; clicked, the song seeks
there; right-clicked — Tempo… (the rename field; ONE `tempo move --bpm`), Glide into it / Step to it (`--ramp`), Delete
tempo change, Copy ID. The ruler's menu offers Tempo change here… after beat 0 (typed: ONE `tempo add --at --bpm`;
`app/widgets/TimelineMarkers.cpp:198`). The song bar shows the tempo where the time bar is (`app/widgets/SongBar.cpp:67`,
eased as ever) and its drag or typed value sets the change in effect there — `project.bpm` before the first — from
`transport.tempoId` (`tempoAt`, `core/service/SolarisService.cpp:619`). UI `test_tempo_changes_on_the_ruler`; shot
`tempo-map`. Mutants: a change appearing at once, the drag sent while held, the song bar at the start's tempo, the song
bar always setting the song's tempo, a retimed change snapping — each red.

### DR-PRESET-1 Device presets (R-PRESET-1)
Two folders, named by the host (`presetPaths`, `host/Machine.cpp:55`): the user's (`SOLARIS_PRESETS`, else
`$XDG_DATA_HOME/solaris/presets`) and the factory's (`SOLARIS_FACTORY_PRESETS`, else `apps/solaris/presets` of this build
— five ship: three synths, a reverb, a delay). A preset is `<folder>/<type>/<name>.slpreset`: `arstro-preset = 1`,
`type`, `name`, then `<param> = <text>` lines as the `.slp` keeps a device's set values (`sample = <path>` for a
sampler); `;` comments. `scanPresets` (`core/service/ServicePresets.cpp:71`) lists every registry type's — the user's,
then the factory's of a name the user has not — into the model (`presets[]`, refreshed once and after a save or a
delete, `core/service/ServiceModel.cpp:116`). `applyPreset` (`:107`): the user's file of the name, else the factory's; its
type must be the device's; the device's set values and sound cleared (every value back to its default), then each of
the preset's through `setAddress` — so a value `set` would refuse refuses the load, and `dispatch` puts the song back:
nothing changes. `presetCommand` (`:155`): `preset list [type]`; `preset delete <type> <name>` (the user's only; a
factory's refused by name); `preset save <dv> <name> [--replace]` (`:204`; a name of 1 … 64 characters, no slash, not
starting with a dot; an existing one refused without --replace; the folder made) — save, list and delete are in
`mutates`' list (the machine's: no undo, no unsaved mark); `preset load` is an edit. A drop: `device add … --preset`
(`core/service/ServiceEdit.cpp:494`) and `clip add --instrument … --preset` apply it in the same command; `--preset`
without `--instrument` refused. The presets folders are read with `std::filesystem` at the host's paths (the test host
lists nothing real). L2 `test_presets_are_saved_loaded_and_dropped`, `test_the_shipped_presets_load` (every shipped one
onto a device of its type). Mutants: a load keeping the values it does not name, a bad value skipped and the rest
loaded, a save an edit of the song, the factory's before the user's, a drop's preset ignored — each red. `preset list`
reads the folders afresh (`core/service/ServicePresets.cpp:137`), so presets written since show. **The window** (N22
part 2, 2026-10-17): the browser's fourth tab, Presets (`app/widgets/Browser.cpp:166`) — each type that has presets, its
label in capitals as a header, then its presets, the factory's marked "factory" at the right; keyed rows, so a saved one
grows in. The four tabs are as wide as their names need (`tabRect`, `:260`) and the highlight eases its width with its
place. A preset row drags like an instrument: an instrument's dropped on the lanes is ONE `clip add --instrument <t>
--preset <name> --at … --length 4 [--lane …]` (`app/widgets/ProjectScreen.cpp:159`); an effect's says it loads onto its
device. A device window's header has Presets (`app/widgets/DevicePanel.cpp:698`): its type's presets (the factory's
marked), each ONE `preset load`, then Save as preset… (the rename field; ONE `preset save`). UI
`test_presets_in_the_window`; shots `browser-presets`, `device-presets-menu`. Mutants: the drop forgetting its preset, a
preset clicked saving over it, the highlight's width jumping, a factory preset unmarked — each red.

### DR-PRESET-2 Song templates (R-PRESET-2)
Two folders, named by the host (`templatePaths`, `host/Machine.cpp:67`): the user's (`SOLARIS_TEMPLATES`, else
`$XDG_DATA_HOME/solaris/templates`) and the factory's (`SOLARIS_FACTORY_TEMPLATES`, else `apps/solaris/templates` — two
ship, "Beat" and "Piano Ballad", made with `solaris-cc` itself). A template is a song, `<folder>/<name>.slp`.
`scanTemplates` (`core/service/ServicePresets.cpp:195`) lists the user's, then the factory's of a name the user has not,
into `templates[]`. `templateSong` (`:217`): the user's file of the name, else the factory's, parsed; missing, refused
naming those there are. `templateCommand` (`:241`): `template list` (reads the folders afresh), `template delete <name>`
(the user's; a factory's refused), `template save <name> [--replace]` (`:268`) — a copy of the open song with every clip's
and take's file and every sampler's sound made absolute, serialized into the user's folder; save, list and delete are
the machine's (no undo, no unsaved mark). `project new <path> --template <name>` (`core/service/SolarisService.cpp:805`):
the template's song with the new song's id (from its path) and name (--name or the path's stem), its tempo, meter and
rate unless --bpm, --sig or --rate say; written and opened as any new song. L2 `test_templates_start_songs`. Mutants: a
template keeping the song's relative files, a song from a template keeping the template's name, --bpm ignored, the
factory's first, a save as an edit — each red. **The window**: Home's third action, From template…
(`app/widgets/HomeScreen.cpp:153`), and File › New from Template… open a menu of the templates, the factory's marked
(`pickTemplate`, `app/App.cpp:456`); one picked, the host is asked where the song goes and the answer is ONE `project new
<path> --template <name>` (`newSongPicked`, `:423` — the template forgotten once used, so a plain new song after carries
none). File › Save as Template… (`:182`; at the menu's end, the others where they were) is the rename field: ONE `template
save "<name>"`. UI `test_templates_in_the_window`; shot `home-templates`. Mutants: the picked template forgotten, a
template carried to the next new song, From template… opening the settings, Save as Template saving nothing — each red.

### DR-PRESET-3 A sample library (R-PRESET-3)
The library is the machine's: a text file the host names (`libraryPath`, `host/Machine.cpp:79` — `SOLARIS_LIBRARY`, else
`$XDG_DATA_HOME/solaris/library`), a line per tagged file: its absolute path, a tab, its tags comma-separated.
`loadLibrary` (`core/service/ServiceLibrary.cpp:49`) reads it once (lower case, sorted, no repeats); the model publishes it
(`library[]`: path, tags without `favourite`, favourite). `libraryCommand` (`:87`): `sample tag <file> <tag>` / `untag` /
`favourite <file> [on|off]` — the file made absolute, a tag of 1 … 32 letters, digits or dashes kept in lower case, a
file left with no tags taken out; the file rewritten whole; `library.changed path tags` said; in `mutates`' list (the
machine's: no undo, works on Home). `searchSamples` (`:120`, from `browse --search <text> [folder]` or `browse
--favourites`, `core/service/ServiceMachine.cpp:346`): the sample folders (or the folder given) walked through the host's
`listDir` — eight levels down, 2000 folders at most, the first 200 found — an audio file found when its name holds the
text in any case or it carries a tag starting with it; then the library's own files wherever they are; sorted by name;
the browser's entries, `browser.search` the text (`favourites`), `browse.changed path=search:<text>`. A plain `browse
<folder>` clears the search. L2 `test_the_sample_library_tags_and_finds`. Mutants: a tag kept as typed, a search blind
to tags, the library's own files not searched, the favourites listing everything tagged, an untagged file left with
nothing — each red. **The window** (`app/widgets/Browser.cpp`): under the sample folders, Search samples… (the rename
field; ONE `browse --search <text>`, `:404`) and Favourites (ONE `browse --favourites`); the tab then shows what was
found under a first row naming the search, which takes it back to the folders (no line); nothing found says so in words.
Every sound row carries its star at the right (`starRect`, `:242`) — lit as the library says, eased whoever changed it
(LIVE `starAmount`) — a click ONE `sample favourite <file> on|off`; its tags beside it, muted mono; right-clicked
(`:459`): Tag… (typed: ONE `sample tag`), Remove tag · <tag> (ONE `sample untag`), Favourite / Not a favourite. The
folders keep their rows' places (Search and Favourites come after them). UI `test_the_sample_library_in_the_browser`;
shot `browser-library`. Mutants: the star sending the opposite, the star snapping, a search not shown, back from a search
staying in it, Remove tag tagging again — each red.


### DR-VST-6 Hosting other makers' VST3 (R-VST-6) — part 1, found; part 2, played; N26, its own editor and its window
**The helper** (`apps/solaris/plughost/`, built where the DSP library found the SDK — `vst3_hosting`): `solaris-plughost
--scan <bundle>` (`plughost/main.cpp:68`) loads the module with the SDK's `VST3::Hosting::Module`, prints a line per audio
processor class — `class`, its UID, `instrument` (an "Instrument" sub-category) or `effect`, its name, its vendor — and
exits 0; a bundle it cannot read prints `error <why>` and exits 1. **The host** (`host/Plugins.cpp`): `pluginSearchPath`
(`:22`; `SOLARIS_VST3_PATH`, colon-separated, else `~/.vst3`, `/usr/lib/vst3`, `/usr/local/lib/vst3`), `pluginsListPath`
(`SOLARIS_PLUGINS`, else the XDG data folder's), `plughostPath` (`SOLARIS_PLUGHOST`, else the one this build made);
`scanPlugins` (`:122`) finds every `*.vst3` three folders down (never inside a bundle) and reads each with the helper in
a process of its own (`readBundle`, `:64` — posix_spawn, its stdout piped, its stderr to /dev/null, 10 s then killed);
a bundle whose reader died says the signal ("the plugin crashed while being read (Aborted)"), one that hung or refused
says so — and the scan goes on. **The service** (`core/service/ServicePlugins.cpp`): `plugins scan` (`:44`) asks the
host, keeps the list in the machine's file (a line per class: id, kind, name, vendor, bundle), prints each found and
each problem, says `plugins.changed count problems`; `plugins list`; the list read back once (`loadPlugins`, `:27`) into
`plugins[]`; both in `mutates`' list. The control test's two services take the same folders, as the window does. Tests:
L2 `test_plugins_are_scanned_and_listed` (a fake host's two and a problem); faces `solaris_plugins` (this build's Basic
Synth and Drum Machine found by their class ids as instruments, a module that aborts while loaded costing only itself,
`solaris-cc plugins scan` the same). Mutants: a crashed reading taken for an empty bundle, every class called an
effect, the list not kept — each red. Found while building it: the faces test's asserts were compiled out in this
Release build (no `#undef NDEBUG`), and the scan itself sat inside one — a test that could not fail; both fixed.

**Part 2 — a plugin played, out of process.** *The helper* `solaris-plughost --serve <bundle> <uid> <rate> <maxBlock>`
(`plughost/main.cpp:370`) is ONE instance: fds 3 a control socket, 4 the shared block (`plughost/PlugShared.h` —
frames, reset, note events, parameter changes, stereo in and out, a status), 5 and 6 the "go" and "done" eventfds. It
says `ready <kind> <latency> <count>`, a `param <id> <steps> <default> <title> <units>` line each, `end`; then a "go" is
a block — the reset first (`plughost/main.cpp:317`, processing set up again), the changes (each also told to its controller) and the
notes at their offsets, the inputs, `process`, the outputs back; a block of no frames carries only the changes
(`:320`, VST3's flush). Control lines between blocks: `state` → base64 (`:480`), `setstate` (`:487`, component then
controller), `latency`, `quit`; the socket closing ends it. *The host* (`host/PluginInstance.cpp`): `loadPlugin`
(`:332`) starts it with `posix_spawn`, the four fds dup2'd to 3–6, a memfd mapped, its hello read within 15 s
(`start`, `:48`); `process` (`:115`) writes the block, signals "go" and waits in `ppoll` (`:138`) for "done" at most the
budget — the control socket's hang-up ends the wait at once (watched for POLLHUP only, so a `state` answer is never
taken for one). Past the budget, a hang-up, or a refused block: OFFLINE for good (`offlineFor`, `:218`) — the helper is
killed then and there (a frozen or spinning one must not stay), with no allocation, as the audio thread may be the one
calling; `why()` reaps it off that thread (`reap`, `:225`) and says "its process ended (Killed)", "it overran its
budget" or "it refused a block". Socket writes use `MSG_NOSIGNAL` (a dead helper is no SIGPIPE of ours).
*The core* (`core/PluginHost.h` — `IPluginInstance`; `core/PluginDevice.cpp`): a `PluginConnection` is one instance and
its `DeviceType` (`setPluginType`, `:18` — `vst3`, the plugin's name, instrument or effect, a `p<id>` number 0 … 1 per
parameter, titled; offline from the start, the song's stored names); a `PluginDevice` (`:51`) is what ONE engine plays
it through: notes, changes and a reset are queued in fixed arrays and go with the next `process` (`:67`) at its start
(the engine splits at every note, so that is the note's sample); a value is sent only once SET (`apply`, `:126`) — the
plugin's own state holds the rest; an instrument adds, an effect replaces; live, a block may wait its own duration (an
engine block's, 128 frames, at least), a render's 60 s; offline, an instrument is silent and an effect leaves its
input dry — delayed by the latency it had when its engine was built (`latency()`, `:123`, fixed for that engine's
life), since the engine compensated for it. *The engine* plays it as any device: a `DeviceDesc::make`
(`engine/MixGraph.h:55`) is used instead of the registry (`Engine.cpp:90` — a parameter it lacks is skipped), and a
made device is NOT warmed (`:267`): what it plays may be the engine the swap replaces. A binding to a plugin's
parameter is resolved by name at build (`:236`; `Bindings.cpp:65`, `Compile.cpp` sets `paramName`). *Compile*
(`Compile.cpp:193`) turns a `vst3` node into its numbers and an offline stand-in the service replaces; an instrument
strip whose first is a plugin instrument stays one (`:346`). *The model* (`model/Project.cpp:350`): a `vst3` device
keeps `plugin` (the UID), `name`, `kind`, `bundle` after its type, its `p<id>` values, and `state` (base64) last.
*The service* (`core/service/ServicePlugins.cpp`): `findPlugin` (`:74`, by id or name in any case, the nearest named);
`device add <ch> --plugin` (an effect; an instrument refused, pointing at `strip add --kind instrument --plugin`),
`strip add --kind instrument --plugin`, `clip add --plugin` (`core/service/ServiceEdit.cpp:450`, `:206`, `:726`); `startPlugin` (`ServicePlugins.cpp:104`) loads it at the
song's rate, gives it the song's state, then the song's set values (a no-frame block), and its type; `livePlugin`
(`:140`) is the device's connection, made on first need (the model asks first), kept across engine swaps, made again
for another plugin, bundle or rate, and says `plugin.offline device why` once if it cannot start; `typeOf` (`:160`) is
how `set`/`get` (`SolarisService.cpp:1281`), live updates and the model read a plugin's parameters; `attachPlugins`
(`ServicePlugins.cpp:166`) gives each compiled `vst3` node its player — the live connection (`buildLive`, `ServiceTransport.cpp:62`), a new
one with a budget for another stream (a note preview `:341`, the input monitor `ServiceInput.cpp:309`), or a new one that
is waited for (a render, `ServiceRender.cpp:176`, which says afterwards which were offline and so silent or dry in the
file); `pluginFacts` (`ServicePlugins.cpp:198`) puts `plugin`, `offline`, `why` and the plugin's name in `devices[]`; `prunePlugins`
(`:314`, every refresh) ends the connections of devices no longer in the song; `capturePluginStates` (`:324`) asks each
playing plugin for its state before the song or its autosave is written; `device reload <dv>` (`pluginReload`, `:337` —
not an edit) starts a new process from the last state and swaps the engine to it, printing `playing` or
`offline: <why>`. `pump` (`ServiceTransport.cpp:149`) says a plugin that went offline while playing, once, and refreshes
the model; `audit` lists it; `preset save|load` on one is refused (its presets are its own). Open and close end the
song's plugins. Tests: L2 `test_a_plugin_plays_in_a_song` (an in-process fake behind the same seam: an instrument strip
of one and its refusals, `p<id>` set/read/refused/bound by a formula, a render on its own instance, an effect at unity
equal to none with 64 samples of latency compensated, ×2 at Gain 1, crashed in a render dry and said, crashed live said
once and shown, never processed on the service's thread, reloaded, state saved with the reloaded one's values and given
back on open, a missing bundle offline with the song open); faces `solaris_plugins` with the real helper (Basic Synth
played: sound, its state out and in, killed → offline "its process ended (Killed)", stopped with SIGSTOP → "it
overran its budget", the frozen one killed by its end, a missing bundle refused; and through `solaris-cc` a song with
Basic Synth as a plugin renders what the native synth renders — the largest difference 0 at 32-bit float — and a
value set on it is saved in its state and played again after the song is reopened). Mutants (each red): an offline
effect zeroed instead of dry, its dry not delayed by its latency, notes never sent, a made device warmed, the state not captured at save, a bound plugin
parameter never found, a render sharing the live instance, the offline never said, a new instance not given the
song's values, a plugin instrument strip compiled as a bus; with the real helper, an overrun not taken offline and an
offline helper left running (the test hangs) — and the first of them SURVIVED at first: the fake crashed on the
zero-frame flush, so the render's instance was offline from birth and the mid-block path never ran; the fake now
crashes on a block of sound and the test compares the dry file sample by sample. Found while building it: a write to a dead helper's socket
killed the host with SIGPIPE; the signal a helper died of was read on the audio thread, racing its reap; the reloaded
plugin's state lagged the song's values until something played it; an effect with latency that went offline passed
its input dry but EARLY (the engine had compensated its latency) — each fixed above. And D-15 (a test of the shipped
presets read a model its own commands freed) — closed. Not yet: the pitch and mod
wheels are not sent to a plugin (VST3 maps them to its parameters through its own MIDI mapping), and its editor is N26.

**N26 part 1 — its own editor.** *The helper* now has two threads: the audio thread (`plughost/main.cpp:419`) waits
for "go", plays a block (`block`, `:315`) and says "done"; the UI thread (`:434`) answers Solaris's lines, gives the
controller what a block set (a queue and an eventfd: the controller's calls are the UI thread's, as VST3 has them) and
runs the editor — so an editor never delays a block. `editor-open <title>` (`:504`; `openEditor`, `:247`) asks the
controller for its view, checks it is made for X11, opens a top-level window of the view's size (fixed unless the view
resizes; named in UTF-8; its close asked of it by `WM_DELETE_WINDOW`), hands the view the plug frame that is also the
run loop it asks for (`Frame`, `:189` — its fds and timers served by the UI thread's poll) and attaches it;
`editor-close` and the window's own close (`:519`, said as `editor closed`) detach it (`closeEditor`, `:299`). The
component handler (`Handler`, `:154`) sends each `performEdit` (`:159`) to the processor at its next block — before
Solaris's own changes, so Solaris's win a tie — and to Solaris as `edit perform <id> <value>`; `restartComponent`
(`:169`) says every value when the plugin changed its own, and a new latency. *The host*
(`host/PluginInstance.cpp`): `openEditor` / `closeEditor` (`:152`) over the control socket; a line the helper says
on its own is kept wherever it arrives — inside an answer's wait too (`:292`, `asyncLine`, `:235`) — and handed over
by `takeEvents` (`:173`), which reads without waiting. *The seam* gains `PluginEvent` (`core/PluginHost.h:38`) and
the four calls (defaults: no editor). *The service* (`core/service/ServicePlugins.cpp`): `device editor <dv>
[--close]` (`pluginEditor`, `:210`; not an edit) titles the window "<plugin> — <strip>", refuses a registry device,
an offline plugin (pointing at `device reload`) and whatever the host says (no display); `plugin.editor device open`;
`devices[].editor`. `pump` gathers what the plugins said (`gatherPluginEdits`, `:240`; `ServiceTransport.cpp:145`):
an editor's values become ONE `set <dv>.p<id>=…` line per plugin per pump (the last of each), a plugin's own new values
move those the song already sets, a closed window is said, a new latency rebuilds the playing engine. The lines land
(`applyPendingEdits`, `ServicePlugins.cpp:285`) when no command is running — `dispatch` counts them
(`SolarisService.cpp:135`), since `wait` pumps from inside one — else first thing in the next `dispatchText` (`:51`);
consecutive ones are one undo step (R-EDM-1's coalescing). What a plugin set is not sent back to it
(`ServiceTransport.cpp:677`): an echo landing mid-drag would pull its knob back. Tests: L2 `test_a_plugins_own_editor`
(the fake's editor: titled, not an edit, said; values turned while playing land after the `wait` that gathered them,
or at once from a `pump` with no command running; never echoed; a drag one undo step; a preset of its own; the window's
close said; refusals); faces `solaris_plugins` (DISPLAY cleared before any helper starts — a test never opens a window
on a display: the real helper refuses "there is no display to open it on" and plays on; the threaded helper still
renders the native synth's samples to the bit). Live (L5, 2026-10-17, on the desktop's display, my own window only):
Basic Synth's own editor opened through the helper — its face drawn whole at 980 × 663 inside the helper's window, the
run loop driving it — and a drag on its Cutoff knob by XSendEvent came back as 11 `edit perform` lines (p18 → 0.939);
closed, no process left. Mutants (each red): pump never landing an edit, an edit echoed back, a plugin's own preset
ignored, the window's close not said, a running command not counted, the editor taken for an edit. Found while
building it: the first faces run cleared DISPLAY only after its helper had started (a helper takes the environment it
is started with) — it opened the editor on the desktop for a moment; DISPLAY is now cleared first thing.

**N26 part 2 — in its device window.** `DevicePanel` (`app/widgets/DevicePanel.cpp`): a plugin device is built as its
parameter list even when it is an instrument (`:194` — its face is its own editor); the list reads a plugin's 0 … 1
values as percentages (`setPluginType` gives them no unit — a plugin's units are its plain value's,
`core/PluginDevice.cpp:31`). Its header names the plugin where a registry device names its type, and where Presets
stands (a plugin's presets are its own) is **Editor** (`editorRect`, `DevicePanel.cpp:521`; drawn `:687`): a click is
`device editor <dv>`, or `--close` while its window is open (`:606`), and its fill eases to the accent while that window
is open (`editorAmount`). Offline — never started, crashed, overran — a band slides open under the header
(`kOfflineH` = 29.25, `app/widgets/DevicePanel.h:51`; drawn `DevicePanel.cpp:724`, its content riding the band's
lower edge): "Offline — <why>" in the destructive colour, and **Reload** (`reloadRect`, `:522`) — `device reload <dv>`
(`:602`); the rows below are pushed down with it, because `bodyTop()` (`DevicePanel.h:104`) includes the eased band.
Both ease from the model wherever the change came from (`DevicePanel.cpp:347`; the first placement is set, not
animated). The rig's host plays two in-process plugins (`RigPlugin`, `app/tests/Rig.h:60`; one named by
`rigPluginCrash()` is offline at once). UI `test_a_plugins_device_window` (`app/tests/ui/uiTests.cpp:6165`): an
instrument plugin's list (no face); Editor's line and its light caught mid-tween, again `--close`; a crash — the band
caught mid-slide, the rows pushed by exactly the band at rest, the model's why; Reload's line, the band caught
mid-close; an effect plugin's list. Shots: plugin-device, plugin-offline, plugin-offline-mid at both sizes. Mutants
(each red): the band snapping, the Editor light snapping, Reload sending nothing, the band not pushing the rows, a
plugin instrument given a face, Editor never closing. Found while looking at the shots: a plugin's "%" unit hung on
its normalised value read "0.45 %" beside a unitless one's "60 %" — the units dropped.

### DR-CTRL-1 MIDI learn and the transport keys (R-CTRL-1) — part 1, the service; part 2, the window
**A control** is `<n>` (any channel) or `<ch>/<n>` (`controlKey`, `core/service/ServiceControl.cpp:23`, refusing
anything else by its form). `map add <cc> <address>` (`mapCommand`, `:135`) takes any address `describeAddress` takes
— a number a formula can drive, in the open song — replaces the control's old mapping, keeps it in the machine's
settings (`map.<cc> = <address>`, `core/Settings.cpp:38`, read back `:82`) and says `settings.changed what=map.<cc>`;
`map remove`, `map list` (`(inert)` beside what the open song lacks); `map learn <address>` waits (`mapLearning` in the
model), `--cancel` stops. All four are in `mutates`' list — never an edit, never in the song. **Listening**: the MIDI
input's sink (`ServiceInput.cpp:89`) puts every controller and every MIDI Machine Control message into a second queue,
the service's; the input opens when a mapping is added or a learn starts, and once at `pump` while there are mappings
(`controlMidi`, `ServiceControl.cpp:70`). **What a control does** (`pump`, `ServiceTransport.cpp:156`): during a learn the
first controller moved is bound on ITS channel (`:119` — its value not taken; `map.learned cc address`); otherwise its
mapping — its channel's first (`:116`), then any channel's — moves the address (`controlValue`, `:55`): a strip's,
a send's or the master's gain by the fader law (`faderDb`, `core/service/FaderLaw.h:14` — moved out of the dock, which
now calls it, `app/widgets/MixerDock.cpp:167`), a registry device's parameter by `valueFromNormalized` (its taper, as
its knob), a plugin's `p<id>` as its own 0 … 1, anything else linearly; clamped, whole where the number is. A pump's
moves are ONE `set` line (the last value of each address), and MIDI Machine Control (`:130` — Play / Deferred Play,
Stop / Pause, Record Strobe, Fast Forward / Rewind to the next / last bar) `transport` lines; they land as a plugin
editor's do (`applyPendingEdits`, `ServicePlugins.cpp:287`: when no command runs, else first in the next one) — but
heard, as any edit (only a plugin's own are kept from echoing back). **The host** (`host/MidiInAlsa.cpp:99`) parses a
SysEx `F0 7F <dev> 06 <cmd> F7` into `{0xF0, cmd, 0x7F}` (`MidiEvent::mmc`, `core/MidiIn.h:28`); other SysEx does not
arrive. The model: `settings.mappings[]` (cc, address, inert) and `mapLearning`; `audit` says a mapping the song lacks
(`ServiceModel.cpp:633`). L2 `test_midi_learn_and_the_transport_keys` (`core/tests/serviceTests.cpp:4329`): not an edit,
in the settings file; the top of a control is +6 dB, a sweep lands its last value and undoes in one step; a channel's
mapping deaf to another channel; a cutoff by its taper; learned on its channel with its value not taken; cancelled;
removed; MMC plays and stops; refusals (128, 17/1, a name, a missing strip, no mapping, no address); inert in another
song and said; read back by a new service. Mutants (each red): a gain moved linearly, a parameter moved linearly, a
channel ignored, the learned value taken, the transport keys ignored, a mapping taken for an edit, an inert mapping
not said, the mappings not kept. Found while building it: the first "not an edit" check could not fail (the song was
already unsaved) — it now saves first; and one mutant did not compile, so its run tested the old binary — rewritten
and run again.

**Part 2 — the window.** The shared parameter menu (`app/widgets/ParamMenu.cpp`) offers every number **Learn MIDI CC**
(`:83` — `map learn <address>`) and, for each control that moves it, **Forget CC <n>** (`map remove <n>`); the
widget fills `ParamTarget::controls` from `settings.mappings[]` (`controlsFor`, `:43`; the dock,
`app/widgets/MixerDock.cpp:1396`; a device's rows and knobs, `app/widgets/DevicePanel.cpp:647`). While a learn waits, the
song bar shows a pill after the tempo (`app/widgets/SongBar.cpp:443`; its rect `:124`, up to the meter, 260 at most) —
"MIDI learn · <address> — move a control" — easing in and out (`:175`; the address kept while it fades), and a click on
it is `map learn --cancel` (`:285`). Settings has a **MIDI** page (`SettingsSheet::kMidi`): each mapping a row —
`CC <n>`, the address, dimmed and "not in this song" when inert — with × forgetting it (`map remove`,
`app/widgets/SettingsSheet.cpp:974`); the rows are keyed by control, a new one growing in and a forgotten one shrinking
out (`:277`, `:782`; drawn `:1157`), the first placement set. UI `test_midi_learn_in_the_window`
(`app/tests/ui/uiTests.cpp:6224`): Learn from a fader's menu — the line, the pill caught mid-fade, a click on it
cancelling (caught mid-fade out); Forget CC 74's line; a knob's menu offering Learn; the MIDI page with one inert row,
× sending its line and the row caught mid-shrink. Shots: midi-learn (a fader's menu with Learn and Forget, the pill),
settings-midi (three mappings, one inert) at both sizes. Mutants (each red): the pill snapping, no Learn item, no
Forget item, the pill not cancelling, a forgotten row snapping out, × sending nothing. Found while building it: two
UI tests (this one and N26's device-window test) reported with a bare `printf`, so the suite's count missed them —
both go through `pass` now (65).

### DR-CTRL-2 Control surfaces (R-CTRL-2) — part 1, the codecs and the service; part 2, Settings
**One model, two codecs** (`core/Surface.h`; discussion.md Round 10): eight channel strips — a fader, a pot, mute,
solo, arm, select, a touch — a master fader, the transport keys, bank / channel shift; back: each fader's travel, the
lights, each pot's ring (`ringPosition`, `core/Surface.cpp:9`: eleven lights, 6 the centre), each strip's name. A codec
is pure. **Mackie Control** (`Mcu`, `:41`): in (`decode`, `:44`) a fader is a 14-bit pitch bend on channel 1 … 8 (the
master's on 9), a pot a relative CC 16 … 23 (bit 6 counter-clockwise), a button a note pressed (127) — arm 0 … 7, solo
8 … 15, mute 16 … 23, select 24 … 31, bank 46/47, channel 48/49, rewind 91 … record 95, touch 104 … 112 (released at
0); out (`encode`, `:88`) the same pitch bends and notes, the rings as CC 48 … 55 (boost/cut), the scribble strip's top
line as one SysEx (six characters and a gap a strip). **HUI** (`Hui`, `:140`): a fader is a CC pair (0 … 7 the high
bits, 32 … 39 the low, completing it), a switch a zone (CC 15) and a port (CC 47, bit 6 pressed) — the strips' zones 0
… 7 (touch 0, select 1, mute 2, solo 3, arm 7), zone 10 bank/channel, zone 14 the transport — a pot a relative CC 64
… 71 (bit 6 clockwise); out the CC pairs, a light as zone + port, the rings CC 16 … 23, a strip's four-character
name as SysEx, and the ping it answers (`keepAlive`, `:237`) — as the protocol is documented from the wire; not yet
checked against hardware. Both encode a DIFF of what the surface shows against what it should: nothing when nothing
changed; a slot past the last strip down, dark and blank; a fader a hand is on left to the hand. **The seam**: MIDI
output (`core/MidiOut.h`), the ALSA sequencer's in the host (`MidiOutAlsa`, `host/MidiInAlsa.cpp:135` — connected to
the writable port of that name; bytes encoded to sequencer events, SysEx too, `:173`). **Settings**: `surface =
none|mcu|hui`, `surfacePort = <port>` (`core/Settings.cpp:76`; `settings set`, `core/service/ServiceMachine.cpp:179`,
refusing another protocol). The keyboard never listens on the surface's port (`ServiceInput.cpp:70` — every port but
it when it listens to all). **The service** (`core/service/ServiceSurface.cpp`): `surfacePump` (`:95`, from `pump`,
`ServiceTransport.cpp:157`, so at the end of every command too) starts the surface when the settings changed
(`startSurface`, `:27` — its port in and out; the codec; everything sent as it connects, HUI pinged at once, `:60`;
why it cannot, said; `surface.changed protocol connected bank why`); then what it moved, through the grammar as a drag
would: a fader `set <ch>.gain` by the fader law, the master `project.masterGain`, a pot the pan in 0.02 steps (twice in
a pump from where the first left it), mute / solo toggled, arm `strip arm <ch>|none`, the transport keys as MIDI
Machine Control's, bank / channel the eight it shows (said); touch and release (`:147` — let go, its fader is sent where
the song has it). The lines land as a MIDI control's. Then what it shows (`surfaceState`, `:84` — the model's strips
in processing order from the bank: their faders by `faderPos`, pans, mute / solo, arm, names; the master; playing,
recording) is encoded against what it was sent, and sent; HUI's ping once a second. The model: `settings.surface`,
`settings.surfacePort`, `surface` (protocol, connected, why, bank). Tests: L2 `test_the_surface_codecs`
(`core/tests/serviceTests.cpp:4428`: both codecs as tables) and `test_a_control_surface` (`:4444`: connected, the
keyboard deaf to the port, everything shown and then nothing while nothing changes; a fader to +6 dB, a pot, a mute
and their light and ring at once; the song moving a fader back, not one touched until let go; play and stop and the
play light; a channel on, the first fader then the song's second strip; none disconnects; HUI's fader pair and ping; a
refused protocol; a port that is not there said). Mutants (each red): the keyboard on the surface's port, a fader moved
linearly, a touched fader moved by the song, everything sent every time, another bank never taken, a let-go fader not
sent back, HUI's pot turned the wrong way, an empty slot left up and lit, HUI never pinged. Found while building it:
HUI's first ping waited out the second the Mackie session had started — a connection pings at once now; and the first
idle check was missing, so "everything every time" survived — added.

**Part 2 — Settings.** The MIDI page's first two rows (`SettingsSheet::kSurface`, `kSurfacePort`,
`app/widgets/SettingsSheet.h:87`) are combos like every setting's — Control surface: None, Mackie Control, HUI; Surface
port: None and the MIDI ports `devices list` names (`app/widgets/SettingsSheet.cpp:371`) — each choice one `settings
set` line; the mapped controls follow under a micro-label (`:1078`). The surface row's note (`rowWords`, `:1093`) says
what the model says: what a surface does, "connected · strips 1–8" (its bank), or "not connected — <why>"; a change
cross-fades from the old line (`:766`). The rig's host has a MIDI output that goes nowhere (`QuietOut`,
`app/tests/Rig.h:184`). UI `test_a_surface_chosen_in_settings` (`app/tests/ui/uiTests.cpp:6289`): Mackie Control
picked — its line, the note caught mid-fade, then "no port"; a port picked — its line (the whole `key=value` quoted, as
the sheet spells any), "connected · strips 1–8"; None — its line, disconnected. Shot settings-midi now shows a Mackie
Control surface connected over the mappings. Mutants (each red): the note snapping, the combo sending another key, the
port list without the machine's ports, the note blind to the connection; a fifth — Settings no longer asking for the
MIDI ports — SURVIVED, and showed the ask was redundant (`devices list` already lists them): removed.

### DR-PERF-1 A CPU meter (R-PERF-1)
**The block.** The player reads a clock as a block's work starts (`core/Player.cpp:583`) and again before it waits for
the device (`timed`, `:541`, called `:617`): the time over the block's duration is the block's LOAD — smoothed over
about a third of a second into an atomic, its worst kept in another (a compare-and-swap maximum the service takes and
holds for a second). No allocation, no lock — the audio thread's rules. **The strips.** The engine adds up each
strip's processing time only when asked (`measureTime`, `engine/Engine.h:101`; the player asks of every engine it
plays, `Player.cpp:112` and at a swap `:277`): a clock read either side of the strip's work in each piece
(`engine/Engine.cpp:672`, an RAII stamp so every `continue` counts), the sums restarted each render (`:1113`); the
player smooths each strip's sum over the block's duration as its share. A render is never timed (it has no deadline)
and its bytes do not depend on it. **The model** (`pump`, `core/service/ServiceTransport.cpp:236`): `transport.cpu`
(smoothed), `transport.cpuPeak` (the worst single block in the last second — unsmoothed, over 1 a missed deadline),
`strips[].cpu` (mapped engine index → strip, `:266`); stopped, the transport's model is reset (the held worst too,
`:54`). **Drawn**: the song bar's readout before the master meter (`SongBar::cpuRect`, `app/widgets/SongBar.cpp:134`):
"CPU" tracked, the percentage in mono, a thin bar under them, muted and the destructive colour from 80 % (`:433`); the
drawn value is a value catching up (`:153`), so a stop eases it down. The learn pill now ends before it. L2
`test_the_cpu_meter` (`core/tests/serviceTests.cpp:4604`): playing, a load in (0, 10) and a worst block kept; a
chord's synth costs more than an empty bus, and no strip more than the whole (≈ 2 % of a block against ≈ 0 here);
stopped, all 0; a render before and after playing, the same bytes. UI `test_the_cpu_in_the_song_bar`
(`app/tests/ui/uiTests.cpp:6448`): the readout caught on its way up and on its way down. The meters-playing shot shows
it. Mutants (each red): the load not over the block's duration, the strips never timed, a strip's sums never
restarted, the worst block never kept, the readout snapping. Found while building it: two survived first — "a strip's
sums never restarted" (the test's bound was loose: a strip's share is now held to the whole load's) and "the worst
never kept" (the model reported `max(worst, load)`, which hid it: it reports the worst itself now); a third — the
load not zeroed at a stop — survived because a stop already resets the transport's model: that zeroing was dead, and
is gone. The held worst's reset at a stop is not covered by a test.

### DR-PERF-2 A multi-core engine, byte-identical (R-PERF-2)
**What may run apart.** At build the engine marks the strips NOTHING feeds — no route, send or key into them
(`engine/Engine.cpp:233`): what a strip's own work reads (its clips or notes, its live keys and input, its instrument
and rack, its zeroed input and key buffers) no other strip writes. **The split.** A strip's work is two functions:
its OWN (`ownWork`, `:667` — source, instrument, rack, into its input) and its ROUTING (`routeStrip`, `:872` — sends
pre and post, fader and pan, keys, output, meters, stems). A piece (`:1017`) first hands the free strips' own work to
the engine's `IParallel` (`engine/Engine.h:88`, `setParallel` `:103`; `ownWorkOf`, `engine/Engine.cpp:661`), then walks the strips in
processing order doing every remaining own work and ALL routing — so every sum is added in the order one thread adds
it, and the bytes are one thread's. The choice is the piece's own (a local; no mark outlives it). **The pool**
(`core/WorkerPool.cpp`): the caller takes items too (`run`, `:91`) and never waits to START — no worker, one item, or a
worker still leaving an old job (`:94`): it works alone; else it fills the job buffer no worker can be reading,
publishes it by an atomic pointer, bumps a generation, notifies (without the mutex — a worker that misses it costs that
piece its help), takes items, and waits only for the items handed out. Workers (`loop`, `:70`) count themselves in
before reading the job and take the caller's floating-point mode (`:83`). `threadsFor` (`:34`): auto = the cores but
one, at most 8. **Where.** The player makes the pool for the threads the setting gives (`core/Player.cpp:113`; from
`ServiceTransport.cpp:598`) and sets it on every engine it plays, a swapped one too (`:275`); a render makes one for
its engine (`core/service/ServiceRender.cpp:212`). `settings set threads=auto|1…16` (`ServiceMachine.cpp:186`; kept,
`core/Settings.cpp:78`; `settings.threads` in the model). Tests: engine `test_the_strips_nothing_feeds_render_in_any_order`
(`engine/tests/engineTests.cpp:825`: two synths, two kits and a shared reverb — the free strips run reversed and
interleaved by a runner with no threads at all: byte-identical to one thread; the bus and the reverb never free); L2
`test_the_engine_on_several_threads` (`core/tests/serviceTests.cpp:4639`: five instruments sending to a reverb — a
render on one thread and on four byte-identical; live playback on four the render, frame for frame; auto; refusals);
every existing live-equals-render test now runs with the pool (auto). Mutants (each red): a fed strip taken for free, the
free strips' own work never done, a free strip's own work done twice, a fed strip's own work skipped when the pool
runs, the caller not waiting for its items. Found while building it: per-piece marks that a "never cleared" mutant
could not fail — the pool ran every piece, so a stale mark could only bite if the pool were removed: the choice is a
local now; my own test iterated the model while its commands rebuilt it and crashed one run in two (fixed as D-15
was); and a ThreadSanitizer build of the service tests found D-16 in `clip duplicate`.

### DR-PERF-3 Freeze and bounce (R-PERF-3)
**A strip's own sound** (`renderOwnSound`, `core/service/ServiceAudioEdit.cpp:381`): a copy of the song holding only
the strip — at 0 dB, centred, unmuted, to a master at 0 dB, not frozen (`:385`) — its clips, its rack, no sends, and
only the bindings of its own devices (`:390`: its automation plays with it); compiled, its plugins given instances of
their own (R-VST-6), rendered by an engine of its own on the pool the setting gives (DR-PERF-2) from the start to the
song's end and on, until its master peak falls under −90 dB or 10 s have passed (`:414`), its output latency trimmed
(`:416`). **The commands** (`freezeCommand`, `:420`; rows `core/service/Command.cpp:94`–`:100`; dispatched
`core/service/SolarisService.cpp:237`): `strip freeze <ch>` refuses an unknown strip, one frozen already (`:489`) and
one anything feeds — an output, a send or a key into it (`:492`: its sound would not be its own) — and writes
`Frozen/<strip> <id>.wav` beside the song, 32-bit float (`:502`; a refreeze replaces it, its old decode dropped,
`:504`), the strip's `frozen` the path relative to the song; `strip unfreeze <ch>` clears it (the file stays, an undo
may want it; `:483` refuses a strip not frozen); `strip bounce <ch>` writes the same render through `writeBounce`
(`:397`, `clip consolidate`'s: `Bounces/<strip> <n>.wav`, the next free number) and adds it as a clip at beat 0 —
a new file, so a NEW strip (R-MIX-2; `:511`); the strip itself untouched. Each one edit (one undo). **Playing a frozen
strip** (`core/Compile.cpp:353`): an audio strip with one region, its file from the start; its rack is not built (its
devices cost nothing; their edits are kept and unheard); its fader, pan, mute, solo, sends and output as ever; the
song's end still its clips' ends (`:362`), not the file's tail; a file that cannot be read leaves it silent, said by
the compile (`:361`). **The file**: `frozen="…"` on its `#atrack` line (`model/Project.cpp:322`, written `:907`).
**The model**: `strips[].frozen` (`core/service/ServiceModel.cpp:289`); `audit` names each frozen strip and the file it
plays (`:620`). Test: L2 `test_freeze_and_bounce` (`core/tests/serviceTests.cpp:6549`: an instrument through a chorus
and a lookahead limiter, faded, panned and sending to a reverb bus — frozen, the song renders within 3·10⁻⁸ of its own
work; a device edit unheard and in the audit; its fader live; unfrozen, the edit heard; undone, frozen again; refrozen,
the new file heard; refusals; a bounce onto a new strip; a file made unreadable opens silent and said). Mutants (each
red): compile ignoring `frozen`; the render keeping the strip's fader, or its pan; no tail; a strip fed by a send
frozen; unfreeze keeping the file; frozen twice; the bounce onto the strip itself; the audit silent; the latency not
trimmed; a refreeze's stale decode kept; the unreadable file unsaid. Found while building it: the unreadable-file
warning bound to the clip loop's `if` (a dangling `else`) — every other strip's clip said "cannot be read"; the
refusal for an unsaved song could never fire (an open song always has its file) and is gone; consolidate's naming
became `writeBounce`, one copy for both. **Drawn** (part 2): a strip's menu in the mixer
(`app/widgets/MixerDock.cpp:1758`) — "Unfreeze" on a frozen strip; "Freeze" (`:1759`) and "Bounce to a new strip"
(`:1760`) on a strip nothing feeds, read from the model's `fromStrips` (outputs, sends and keys — the service's rule);
each one line. A frozen strip's eased amount (`:977`, `frozenAmount` `:508`, the cross-fade's time) dims its rack
(`:1852`) under a chip-sized FROZEN pill in the accent (`:1877`, its word ellipsized on a narrow card). Test: UI
`test_a_strip_freezes_from_its_menu` (`app/tests/ui/uiTests.cpp:6328`: a fed bus offers neither; Freeze sends its line,
the amount caught mid-ease both ways; Unfreeze; Bounce makes a strip). Shots: mixer-frozen, mixer-frozen-mid. Mutants
(each red): the look snapping; never advancing; Freeze, or Bounce, offered on a fed strip; Unfreeze never offered;
Freeze sending the wrong line. Found while testing it: Escape closes a context menu only while renaming, so the Duck
Under test's last check ("no such item") passed whether or not its menu opened — it now asserts the menu opened and
closes it by a click away.

