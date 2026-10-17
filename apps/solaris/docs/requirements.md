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
(`model/Project.cpp:428`) reads the suite grammar: header `key = value`, node lines, indented
continuation lines, whole-line and inline `;` comments (kept with the node they follow), unknown
keys (kept in order) and unknown nodes (kept verbatim with their indented lines). The suite's
inline `#note`s under an `#aclip` become a pattern of their own — the one normalisation, reported.
`serializeProject` (`model/Project.cpp:749`) writes §10's canonical form: `canonicalNumber`
(`model/Format.cpp:33`, shortest round-trip, always a point), `canonicalBeats` (`:52`, rounded to
1/960 beat, fewest decimals that read back to the tick), seconds to the microsecond; defaults of
optional fields are omitted. **Parse → serialize is a byte-exact fixed point** for canonical text.
Refused, each naming what and where (`validateProject`, `model/Project.cpp:963`): duplicate ids,
`master` as an id, unknown strip kinds, dangling references, an audio clip on a non-audio strip, a
note clip on a non-instrument strip, `in ≥ out`, a clip before the song, two racks for one strip,
an input port as a destination, a header that is not `app = solaris` / `timebase = beats` /
`ppq = 960`. Repaired and counted (`Reader::num`, `:213`): a non-finite or unreadable number → the
field's default; out-of-range pan, pitch and velocity clamp. Guarded by `solaris_model` (8 tests;
mutants checked: dropping unknown keys on write breaks the fixed point).

### DR-MIX-4 Routing only goes forward (R-MIX-4)
`validateProject`'s `checkTarget` (`model/Project.cpp:980`) accepts an `out` or a send target only when
it is `master`, an output port, or a strip `feedsForward` (`model/Project.cpp:897`) allows — one LATER IN
PROCESSING ORDER: on a later mixer, or after it on its own (strip order, then the file's order on a
tie) — the ONE copy of the rule, for outputs, sends and sidechain keys alike; anything else is refused
naming both ends, as `ch_1 (Main, on Buses) output → ch_2 (kick, on Sources): a strip can only feed a
strip LATER in processing order (a later mixer, or after it on its own), the master or a port (R-MIX-4)`.
A file that routes backward does not load. Because of this, `Project::stripsInOrder`
(`model/Project.cpp:141`) — mixer order, then strip order — is a topological order of the routing
graph with no cycle check at all. (**AMENDED (audit, 2026-10-09, R-MIX-4 amended):** `feedsForward` was
"a later MIXER", so a bus made on Buses could not feed Main and every return skipped it.) A reorder
that would point a route backward — `strip move`, `mixer move` — is refused by the same validator
after the command. A new strip whose output is a strip on its OWN mixer is placed just before it
(`placeBeforeItsTarget`, `core/service/ServiceEdit.cpp:73`), so `strip add --kind bus` on Buses feeds
Main: its default output is the bus that LEAVES the nearest later mixer with buses, else the one
leaving its own (`defaultOutFor`, `:101`). `strip move --order <n>` puts the strip at position n of its
mixer and renumbers the mixer 0…n−1 (`placeStrip`, `:61`; `case K::StripMove`, `:286`), as `mixer move`
renumbers mixers — an order never ties. A new project is `newProject` (`model/Project.cpp:182`): Sources (`mx_1`), Buses (`mx_2`)
holding the bus Main (`ch_1` → master), the port Main (`prt_1`) fed by the master (R-MIX-3).
Guarded by `test_routing_only_goes_forward` and `test_forward_is_processing_order_and_moves_renumber`
(`core/tests/serviceTests.cpp:5021`: a bus made on Buses goes before Main and feeds it; a same-mixer route
forward is taken, backward refused naming both ends; `strip move` that would point one back refused;
`--order 0` first and renumbered; mutant checked: the old later-mixer rule refuses the new bus's
default route). The same rule is PUBLISHED: `targetsOf` (`model/Project.cpp:936`) lists what a strip
may feed — later strips in processing order, `master`, the output ports — as `strips[].targets`
(`core/service/ServiceModel.cpp:255`), and `keyTargetsOf` (`:756`) its strips as `strips[].keyTargets`;
a front end offers exactly that list (the route picker, `MixerDock::openOut`, `app/widgets/MixerDock.cpp:912`),
and the test routes to every entry offered.

### DR-ENG-1 The engine renders a MixGraph through the DSP library's devices (R-MIX-1/5/6, R-DSP-1/5, R-RENDER-1)
`solaris_engine` knows no project and no file: it renders a `MixGraph` (`engine/MixGraph.h`) —
strips in processing order with their devices (registry types + values), audio regions in samples
over decoded PCM, note events in samples, an output and sends as forward indices, the master's rack
and gain, the ports. `Engine::build` (`engine/Engine.cpp:107`) refuses a target that is not LATER
(R-MIX-4, a second time — the graph might not come from the model), a missing port, an unknown
device type or parameter, an instrument strip whose rack does not start with an instrument; sets
the DSP library's process-wide sample rate (R-NFR-7); builds every device with
`DeviceRegistry::create`; and **warms** each with one block of silence (`warm`, `:82`), because the
library smooths every parameter write over a block and the first block of every render would
otherwise carry a ramp from the device's default; and sizes every latency-compensation delay
(`compensate`, `:244`, DR-MIX-17). `renderPiece` (`:779`) per strip in order: sum
what earlier strips routed/sent to it; add its regions (looped, offset, offline = silent) or play
its instrument with the block **split at every note event** (`:591`, R-DSP-5); run the rest of its
rack (bypass skips); tap pre-fader sends; fader + balance pan; post-fader sends; add into its
output (`route`, `:498` — master, a later strip, or a port, each through its connection's delay; a mono port gets the average of L and
R). A silent strip (muted or solo-silenced, decided by the core) sends nothing anywhere (`:688`).
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
with the grammar TABLE (`commandSpecs`, `core/service/Command.cpp:26`; `parseCommand`, `:488` —
longest verb match, flags checked against the row) and `dispatch` (`core/service/SolarisService.cpp:128`)
runs it; out come the `AppModel` (`core/service/AppModel.h`, refreshed after every command) and
`Event`s whose `formatEvent` text is the log line. **Every edit is all-or-nothing**: the project is
copied first, the command runs, `validateProject` runs, and any failure restores the copy
(`:308`) and refuses with the validator's sentence — a refused command changes nothing. An edit's
events (`project.changed` from `changed`, `:36`; a `set` line's `params.changed`) are HELD in
`mPending` and emitted only once the whole command has landed (`:380`), so a refused command
announces nothing either (D-1). An edit that made nodes prints the id it made first and `made:` with
the rest (DR-SVC-8). And a refusal always says WHY: a path that returns false without a
reason is caught in `dispatch` (`:295`) and named (`` `clip add` was refused without a reason``),
never silent (R-SVC-3) — B1's own wiring mistake showed the hole. The core
holds no codec and no device API: decoding and WAV writing are `Host` functions (R-SVC-4).

### DR-SVC-2 Unknown input is refused, naming it (R-SVC-3)
An unknown verb or flag fails in `parseCommand` with the nearest candidates (`strp add` →
`did you mean: strip add?`). An address goes through `setAddress` (`core/service/SolarisService.cpp:645`):
an unknown node, field or DEVICE PARAMETER is refused with the nearest name (`set
dv_1.filter.cutof=1` → `did you mean: filter.cutoff?`); a value out of range is refused with the
range; a device parameter's value is read against the DSP registry — a choice by name or index (listing
the choices on a miss), a number inside the spec's range (`:793`; **amended, R-SVC-8, 2026-10-09**: it was
clamped in silence, now a value outside is REFUSED naming the range with its unit, and a non-whole value
of an integer parameter is refused) — and stored as its canonical text. `get` (`getAddress`, `:887`) reads every field `set` writes plus derived ones (`<clip>.length` resolved, `<strip>.out`,
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
`clip add --src` (`core/service/ServiceEdit.cpp:635`): a file no clip uses yet gets a new audio strip
named after it on the first mixer, routed by `defaultOutFor` (`:101`) to "Main" — the bus that leaves the
nearest later mixer with buses — and, having no clips, a new lane; a file already used reuses its strip and, with no
`--lane`, its lane (`laneFor`, `:616`; **amended, R-SVC-8, 2026-10-09**: every clip got a new lane);
`--strip` overrides. The file
is decoded through the host to learn its length (refused if it cannot be read), and stored relative
to the song's folder when inside it (`relativePath`). A relative `--src` names a file in the SONG's
folder first when one is there (`:605`) — what the model stores and the browser's Song tab hands
back — else the caller's working directory.

### DR-MIX-7 Solo keeps the soloed path alive (R-MIX-7)
`silentStrips` (`core/Compile.cpp:123`): with any strip soloed, a strip is audible only if it is
soloed, reachable downstream of one (its buses, its sends' returns) or upstream of one (what feeds
it); muted strips are silent regardless. The model shows it as `strips[].audible`; the engine gets
it as `silent`.

### DR-MIX-8/9/10 Fed by, the matrix, the audit (R-MIX-8, R-MIX-9, R-MIX-10)
`refreshModel` (`core/service/ServiceModel.cpp:75`) computes each strip's `clipCount`, `fromLanes`
and `fromStrips`. `matrix print [--json]` (`matrixText`, `:603`): rows = strips in processing order,
columns = `matrixColumnsOf` (`model/Project.cpp:946`) — every strip some strip may reach or does reach
(processing order), master, the output ports, so a route, send or key to an instrument or a same-mixer
strip has its cell — computed once and published as `matrix.columns` (`ServiceModel.cpp:275`), which the
GUI's matrix draws (DR-UI-8); `●` = the main output, `-6.0pre` = a send's dB and tap, `key` = a sidechain
key. (**AMENDED (audit, 2026-10-09):** the columns were the buses only, and the GUI's "strips off the
first mixer" — a key to an instrument strip was in neither.) Guarded by `test_solo_mute_matrix_and_audit`
(a key to a strip on its own mixer prints `-3.0key` in its column; mutant: buses-only columns → red).
`audit` (`:379`): unused strips, strips that reach no output port, single-input and empty buses,
clips on silent strips, offline files, unknown device types and parameters, any strip or the
master that went over 0 dBFS in the last render, and (R-SVC-8, `:466`) notes past their pattern's end,
empty patterns and patterns no clip plays.

### DR-MIX-13 A line added from the mixer; sources relinked (R-MIX-13, R-MIX-14)
`strip relink <ch> --to <ch2>` (`core/service/ServiceEdit.cpp:325`) moves EVERY clip playing through
`<ch>` to `<ch2>` in one edit (one undo step, "strip relink <ch>"), printing how many moved; refused
onto a bus ("a bus plays no clips"), across kinds (audio clips need an audio strip, note clips an
instrument strip), onto itself, and when `<ch>` has no clips. A pattern's `strip` follows its first
clip. In the dock (`app/widgets/MixerDock.cpp`) every mixer page ends with "+ Line" after its last
card — placed from the LIVE card widths, so it slides as cards grow in and shrink out
(`MixerDock::addLineRect`, `MixerDock.cpp:357`; drawn at `:1379`) — whose menu is an audio line, a bus
and every instrument of the registry, each ONE `strip add --kind … [--instrument <type>] --mixer
<mx>` (`MixerDock::openAddLine`, `MixerDock.cpp:796`); the new card grows in. A strip's menu offers
"Move its clips to ▸" when it has clips and another strip of its kind exists — a second menu of those
strips → `strip relink` (`MixerDock.cpp:1083`). On the lanes a clip's menu (`Timeline.cpp:794`) offers
"Play through ▸" — the strips of its kind, the current one marked "now" → `clip move <ac> --strip
<ch>` — then Piano Roll (a note clip), Duplicate, Delete.

### DR-CLIP-2/3 Patterns and linked clips (R-CLIP-2, R-CLIP-3)
`compile` (`core/Compile.cpp:260`) expands a note clip: a clip longer than its pattern loops it, a
note is cut at the clip's end (`(C1)`, `:415`); events sort by time with note-offs before note-ons at
one sample (`(C2)`, `:464`). `clip duplicate` makes a second clip of the SAME pattern (`linked` = 2 in
the model) — `--count N` makes N, end to end, one edit (DR-SVC-8); `clip unique` copies the pattern.
`note add` replaces a note at the same pitch and tick; so does each note of `notes add`.

### DR-RENDER-1 Offline render (R-RENDER-1, R-RENDER-2, R-RENDER-3)
`render --out <file>` (`core/service/ServiceRender.cpp:73`; `--out` is required — the grammar row says so,
`CommandSpec::required`, `core/service/Command.cpp:214`, and a line without it is refused at parse):
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
`apiJson` / `apiMarkdown` (`core/service/ApiDoc.cpp:45`, `:118`) print, from the tables the code
runs on, every command (usage, summary, the R- tag that asked for it), every event with its fields,
every `AppModel` field (with `stable`), **every DSP registry device with every parameter's unit,
range, default and choices**, a kit's pads by name (`:81`), and the Notation — pitches, the note token,
step rows, every chord quality (`:145`, from `chordQualities()`, R-SVC-8). `docs/api.json` and `docs/API.md` are that output, committed;
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
(`touchRecent`, `core/service/ServiceMachine.cpp:67`), at most 20, persisted in the host's recents file —
by its ABSOLUTE path (`absolutePath`, `:58`), so `project open song.slp` from one folder is still found
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
`outputLatency`, `Player::heard`, `core/Player.cpp:234`, R-MIX-17), the latency and the peaks into the
model; the metronome clicks on the beat the music is on (`:568`), the bound values read are those of
the heard block (`:303`). A live `set` that moves a device's latency (a limiter's lookahead) builds a
new engine instead of a message (`latencyOf`, `core/service/ServiceTransport.cpp:74`, checked at `:513`),
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
(`keepLive`, `core/Player.cpp:280`), and the service suite counts allocations on every thread but its own
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
`app/App.cpp:230` — a refusal becomes a toast with the service's sentence). `linux_main.cpp` is the
only OS code: GTK3, the frame clock, the pickers, the host functions, the service pumped every tick.
The screen follows `AppModel::screen` and CROSS-FADES (260 ms, `App::render`, `app/App.cpp:375`).
`installSolarisAccent()` moves cosmo's accent slot to teal `#159387` before any widget exists
(`app/Theme.h`); every neutral, radius, font and spacing token is cosmo's, aliased. `HomeScreen`
(`app/widgets/HomeScreen.cpp`): the `solaris.` wordmark sized to fit, New/Open, Settings, recent-song
cards (name, `bpm · length · strips` over a plate of bars from the song's name; a moved song says
"missing" in red and is named by its file) whose grid geometry eases on reflow (`layout`, `:97`);
right-click forgets a card. `SongBar` (`app/widgets/SongBar.cpp`): wordmark, Home (asking first with
cosmo's `ConfirmDialog` when unsaved, `App::requestHome`, `:136`), the song's name with an eased
unsaved dot, Play/Stop whose glyphs cross-fade, the position as bar.beat.tick in mono, the tempo,
the master meter (40 ms rise, 300 ms fall, `advance`, `:60`), Save, Settings. Keys: Space play/stop,
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
to the visible edge when it begins off-screen (`paintClip`, `:880`); a note clip draws its
pattern's notes repeated where it loops, the seams marked, and "linked ×N" when it shares its
pattern. Ruler: bars from 1, the grid's ticks, every label faded in by its room (DR-UI-10). The
playhead is `destructive`; it follows the transport while playing and eases 140 ms on a seek.
Ctrl+wheel zooms ×1.25 a notch on a lattice about 28 px/beat — 4.7 to 637 px/beat — eased 220 ms
and anchored at the pointer (`:618`); the wheel scrolls, Shift+wheel sideways. The grid follows the
zoom and its finest level with room is THE snap step on the lanes (DR-UI-10). A clip dragged follows
the pointer exactly, on that step, between LANES only, and lands as `clip move <ac> --at <b> [--lane
<ln>]` (`:555`) where it was let go. A click selects (the teal ring cross-fades 200 ms between
clips); Delete/Backspace → `clip delete`, Ctrl+D → `clip duplicate` (`app/App.cpp:367`); a ruler
click → `transport seek <b>` on the grid you see (`Timeline.cpp:784`). A lane moved by `lane move` (DR-LANE-1)
slides to its new row through the same keyed rows. **The picture travels
(§1):** the rows are Interstellar's `AnimatedRows` keyed by lane; each clip keeps an eased beat, row
and opacity keyed by its id (`Timeline::advance`, `Timeline.cpp:342`) — it fades in when it arrives, fades out where it
was when it goes (taking no input), eases 200 ms with its row when moved from a shell; the zebra
follows the LIVE slot, a stripe's colour cross-fades, the empty-state words fade. Another song
places everything where it is. Empty, it says what to do in words.

### DR-BROWSE-2 The browser and drag and drop (R-BROWSE-1 amended, R-BROWSE-3)
`Browser` (`app/widgets/Browser.cpp`): tabs **Samples · Instruments · Song** under a highlight that
slides 220 ms. Samples lists the folders from Settings; a folder clicked is `browse "<path>"`
(`:216`) and shows its sub-folders and audio files (mono, the filename rule) under a row back up;
with no folders it says so and a click opens Settings. Instruments lists `AppModel::deviceTypes`
(the DSP registry, `core/service/ServiceModel.cpp:125`) — instruments, then effects. Song lists the
files the song plays. The list is `AnimatedRows` keyed by generation and content (`rebuild`, `:56`):
a tab or folder changed starts a new generation, so the old list fades where it was scrolled while
the new one fades in (`navigate`, `:113`); an inserted row fades in, a removed one out. A row is
dragged out: the browser reports the pointer and the drop; `ProjectScreen` draws the ghost (the
overlay pass) and the timeline's teal drop hint at the beat on the lanes' step (DR-UI-10; `overLanes`,
`app/widgets/ProjectScreen.cpp:106`), and on release sends ONE line (`place`, `:115`): a sample → `clip add --src "<file>" --at <b> [--lane
<ln>]`; an instrument → `clip add --instrument <type> --at <b> --length 4 [--lane <ln>]` (the new
strip and its empty note clip in one command, `core/service/ServiceEdit.cpp:660`); below the last
lane, `--lane new` — a new lane (`app/widgets/ProjectScreen.cpp:120`; **amended, R-SVC-8, 2026-10-09**: it sent no `--lane`, which
now means "its strip's lane", so the drop says what it wants); an effect → a notice that it goes on a strip (U3). A double-click
places at the playhead.

### DR-UI-7 A strip's colour (R-UI-7)
The model's `strips[].colour` is resolved by the service (`core/service/ServiceModel.cpp:226`): the
strip's own, else its id's number − 1 — never −1, and unchanged when other strips are added or
deleted. Every front end draws it as is.

### DR-UI-8 The mixer dock (R-UI-3, R-MIX-1/5/6/7/9/12, R-MIX-12 amended)
`MixerDock` (`app/widgets/MixerDock.cpp`) sits under the lanes (`ProjectScreen::dockTarget`,
`app/widgets/ProjectScreen.cpp:155`: 429 px wanted; its top edge dragged follows the pointer; the
chevron folds it to its tab bar, eased 220 ms; it gives way before the lanes, which keep 130 px).
Tabs: a mixer page each, "+" (`mixer add`), Matrix; keyed (`syncTabs`) so a tab added slides the
others along, measured in one weight so choosing a tab moves nothing; the highlight slides, the
pages cross-fade as layers. A page (`syncCards`, `:196`) is a card per strip in processing order,
the strips feeding one bus (two or more) gathered under its header; the master pinned right. A card:
colour, name, "audio · N clips" / "bus · fed by N"; rack chips (four slots: the devices and "+
Effect", or two, "+N more", "+ Effect"); two sends (→ name, dB, P = pre; a third says "in the
matrix"); pan; the fader (`faderPos`, `:95`: gain ∝ position², 0 dB at 0.708, +6 at the top) beside
L/R meters (−60…+6 dBFS, green → amber at −12 → red at −3); M and S (solo amber); "→ <out>".
Commands (`handleGesture`, `:883`): a fader or pan dragged — `set <ch>.gain|pan=…` (master:
`set project.masterGain=…`) at each step, double-click → 0; M/S → `set <ch>.mute|solo=…`; "→ out"
→ cosmo's `ContextMenu` of `strips[].targets` → `route`; "+ Effect" → the registry's effects →
`device add` (`openAddEffect`, `:827`); a send → pre/post, make it the main output, remove; a send
dragged sideways → `set <sd>.gain=…`; right-click → rename (`set <ch|mx>.name=…`) or delete. A
fold header click folds its group (`:994`, the view's, eased 260 ms: members narrow to nothing, the
header widens to "→ Main · N strips"). The Matrix (`paintMatrix`, `:1402`): rows by mixer, columns
= the model's `matrix.columns` (`:1406`; the same `matrix print` prints, DR-MIX-8/9/10 — amended, audit
2026-10-09: it was "strips off the first mixer", so a key to an instrument strip had no cell); ● the main
output, a send's dB, P pre-fader, K a sidechain key (`:1451`); a cell routing
refuses is hatched; a click on an open cell → `send add`, on a send → its menu, a double-click →
`route`, a vertical drag → its level. **Nothing snaps** (`advance`, `:522`): every model value is
drawn through an eased copy keyed by strip (fader and pan 220 ms, M/S/dim 200, colour cross-fade
200 from what is shown, meters 40 up / 300 down); a dragged control follows the pointer exactly;
cards and tabs are keyed — arriving grows, leaving shrinks, a moved card shrinks where it was and
grows where it is. A strip a solo silences dims. **AMENDED (R-MIX-16, R-UI-11, 2026-10-09):** a
right-click on a fader, a pan, a send's level (on a card or its matrix cell) or the master fader opens the
device parameter's menu instead of the strip's (`openParam`, `:851`; DR-MIX-16); the strip's and a mixer
tab's menus gain Copy ID. A bound fader or pan is drawn at the model's `bindings[].live` — set each frame
while playing, eased when stopped or onto a new driver (`advance`, `:557`) — under a tag saying what drives
it (`paintTag`, `:1121`); with View › Show IDs each card, chip and send shows its id (DR-UI-11).

### DR-UI-5 Device panels, generated (R-UI-5, R-WIN-2/3)
`DevicePanel` (`app/widgets/DevicePanel.cpp`) is the content of a device's WINDOW (DR-WIN-1). It knows no
device: per `DeviceModel::params` it builds — once, the first time the model has the device
(`build`, `:192`), kept — a row per parameter grouped by the name's prefix (`osc1.*` → OSC1): a number
is cosmo's `SliderRow` with the opt-in `formatValue` (Hz/kHz, ms, dB, st/ct/oct signed, a 0…1 amount in
%), a `logScale` parameter through a log taper, an `integer` one rounded; a choice is a row stepping
its names. It is the parameter LIST: the value column says what decides each row (`bindingText`,
`app/widgets/ParamMenu.cpp:33`) — its number, `auto au_1`, `= ch_2.gain`, `= <formula>` cut to the column — and the row
`DeviceModel::lastChanged` names is lit, the light easing from row to row (`ParamBody`, `:89`). A
right-click offers Create Automation, Formula… (cosmo's rename field), Clear Binding, Reset to Default
(`openParamMenu`, `:443`) — **AMENDED (R-MIX-16, R-UI-11, 2026-10-09):** built by the shared `ParamMenu`
(`paramMenuItems`, `:470`), the same menu the dock's numbers offer (DR-MIX-16), with Copy Address / Copy
Value (a bound row: its live value) / Copy as Formula (a choice row: no "as Formula", no binding items);
with View › Show IDs each row's full address fades in over its label (`ParamIds`, `:179`, DR-UI-11). The
App offers right-clicks to the windows first because a row's slider would
swallow them (`app/App.cpp:81`). Every change is one line: `set <dv>.<param>=…`, `auto create`,
`bind clear`, `set <dv>.bypass=…`, `device remove <dv>` (none for an instrument — it has "Piano Roll"
instead, DR-ROLL-1). While the pointer is
down `bind` re-seeds nothing (`:332`); the body scrolls with its own bar (`reveal` eases to a row).

### DR-AUTO-1 Automations and bindings in the `.slp` (R-AUTO-1, R-AUTO-4)
`Automation` (`model/Project.h`): id `au_n`, name, unit, min/max, `from`, points (beat, value, shape
`linear|hold|smooth|bezier`, sorted; a bezier point's handles — DR-AUTO-6). `Binding`: address +
formula (with its `=`), one per address. Written after the clips as `#aauto` with `#point` children
and `#abind` nodes (`serializeProject`); read by `parseProject` (`model/Project.cpp:532`), which also
reads the suite schema's earlier sketch — indented `<beats> = <value>` lines and
`node=/param=/interp=` — and normalises it once (`:597`). `validateProject` (`:1023`) refuses two
bindings on one address, a formula without `=`, a binding on something that does not exist, an empty
range, an unknown shape and a bezier influence outside 0 < x ≤ 100 % (`:1099`).

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
its members; an unreadable binding is left out — INERT, its own value plays. `checkBinding` (`:170`)
answers for one candidate beside the rest; `set` refuses on its answer.

### DR-AUTO-3 The engine evaluates them every 64 samples (R-AUTO-7)
`compile` (`core/Compile.cpp:484`) turns automations into `engine::Curve`s (`compileCurve`, `:565`:
Interstellar's keyframes in samples, speeds per sample, ranged as the automation — DR-AUTO-6) and
bindings into `engine::Bind`s — target (strip gain/pan, send gain, master gain, a device parameter by
registry index), clamp, integer, own value — with each symbol remapped: an automation → its curve's
slot, a bound address → its binding's slot, an unbound address → its own value as a constant.
`evaluateBinds` (`engine/Expr.h:222`) fills the clock, the curves (`Curve::valueAt`, `:182`, Interstellar's
`anim::eval`, clamped to the automation's range) and each
binding in order (`Expr::eval`, `:60`, a fixed stack, no allocation; a non-finite result keeps the last
good value). `Engine::render` (`engine/Engine.cpp:865`) cuts pieces at every multiple of `kControl` = 64
samples and evaluates there (`evalBinds`, `:332`), so the result is the same however time is chopped;
bound gains, pans and sends ramp linearly across the period, unbound strips render exactly as before.
Values at time zero are applied before the devices' warm-up (`:231`); a seek evaluates at once
(`:401`). Measured: a 0 → −20 dB curve renders −5.00 dB at beat 1 and a formula-stepped compressor makeup
+12 dB from beat 2, byte-identical at chunks of 77, 128 and 1000, no zipper, no allocation.

### DR-AUTO-4 The service: commands, `set`, `eval`, the model (R-AUTO-1…5, 8, 9)
`set <address>="=<formula>"` binds (`core/service/SolarisService.cpp:659`); a plain number clears the
binding and sets the value; `get` prints the formula; an unquoted formula with spaces is refused with
the quoting shown. `auto create <address>` (`core/service/ServiceAuto.cpp:125`) makes `au_n` named
"<owner> · <label>", ranged as the address, holding its value from beat 0 to the song's end (at least
four bars), and binds the address to `=au_n`; `auto add|delete` (refused while read, `--unbind`),
`auto point add|move|delete|shape` (a value outside the range is refused naming it, `inAutoRange`, `core/service/ServiceAuto.cpp:69` —
**amended, R-SVC-8, 2026-10-09**: it was clamped; `shape` takes a bezier point's handles, DR-AUTO-6), `bind clear`; deleting a strip, a
send or a device drops the bindings that drive them. `eval <address> [--at] [--explain]`
(`:261`) compiles and evaluates with the engine's own function and prints each name it reads.
`refreshModel` (`core/service/ServiceModel.cpp:356`) publishes `bindings[]` (reads, ok, problem),
`automations[]` (points with their handles, usedBy, `now` — DR-AUTO-7), `params[].formula`,
`strips[].gainFormula/panFormula`, `sends[].gainFormula`, `masterGainFormula`, `devices[].lastChanged` and — **AMENDED (R-MIX-16, 2026-10-09)** —
`bindings[].live`, each binding's value at the heard position (`evaluateLive`, `:445`; DR-MIX-16); `audit` (`:437`) names inert
bindings and automations no formula reads. While playing, a binding or curve edit — or a `set` on an
address a formula reads — swaps in a new engine (`core/service/ServiceTransport.cpp:463`).

### DR-EDM-1 Undo and redo (R-EDM-1)
The history is a TREE now (DR-HIST-1): after every edit that LANDED, `dispatch` keeps the song as the edit left it
as a node (`core/service/SolarisService.cpp:330`); consecutive `set`s of exactly the same addresses overwrite the
newest node instead of adding one, so a dragged fader is one step (a rule a script sees the same way). `undo` /
`redo` (`historyCommand`, `:447`) move to the parent / the newest child, print the label of what they take back or
put back (`set ch_2.gain`, `clip add`, `strip delete ch_3`), emit `project.changed what=undo|redo node=<label>`, mark
the song unsaved and, while playing, swap in a new engine. A new edit no longer clears redo's steps: they stay a
branch. Not edits: machine settings, the transport, a save, `get`/`eval`/`audit`, `history`. `project
new|open|close` start a new tree. The model names both (`undoLabel`, `redoLabel`, `undoDepth`, `redoDepth`); the
window's undo and redo keys (the keymap's, DR-SET-4) send `undo` / `redo` (`app/App.cpp`).

### DR-HIST-1 The history is a tree (R-HIST-1, R-HIST-2)
`SolarisService` keeps `mHist`: nodes keyed by an id never reused while the song is open (ascending = the order
they were made), each the song as an edit left it, its label, its parent and its children
(`core/service/SolarisService.h`), in memory only — never saved, with or beside the song (R-HIST-4, the user's
choice). A new, opened or closed song resets it (`historyReset`, `core/service/SolarisService.cpp:417`; `dispatch`, `:359`):
one root, "New song" or "Opened", the song as it came. An edit that landed is `historyAdd` (`:425`): a child of the
current node — a BRANCH when that node already has children, and the old ones stay. `undo` goes to the parent,
`redo` to the child with the highest id (the newest branch), `history jump <id>` to any node (`historyCommand`,
`:463`): the song becomes that node's, the move is announced (`what=history.jump`), the song is unsaved, the player
hears it — and a move is no node itself; the next edit after one starts a node of its own. `history` prints the tree
— `*` at the current node, its id, its parent's (`-` at the root), its label — and `history --json` the same as the
model's `history[]` (`id`, `parent`, `label`) and `historyCurrent` (`core/service/ServiceModel.cpp:406`, which also
derives the undo / redo labels and depths from where the song is). At most 200 nodes (`historyPrune`, `SolarisService.cpp:421`): past
that the oldest leaf OFF the path from the root to the current node goes first; when the tree is one path, its root
does and the next node becomes the root. L2 `test_history_is_a_tree` (a branch kept, a jump, redo's newest branch,
the root, a drag one node, the cap, a song opened its own root). Mutants: a new edit dropping the undone branch (the
old stack), redo taking the oldest branch, pruning the root before an old branch — all seen red.

### DR-HIST-2 The tree, seen (R-HIST-3)
Edit ▸ Show History Tree… (`app/App.cpp:197`, as cosmo's History menu has it) is `App::openHistory` (`:279`): cosmo's
own `HistoryView` — compiled from `apps/cosmo/widgets`, never copied — shown over a snapshot of the model's
`history[]` (a node's parent mapped to its index; the hint under the title names this machine's undo and redo keys,
R-SET-4). It is modal: the gesture sink gives it every gesture while it is open (`:101`; a wheel notch pans a row),
Escape closes it, and only the undo and redo keys pass through, walking the tree it shows (`App::key`, `:432`). A
node clicked is ONE `history jump <id>`; the view stays open, and when the model comes back `syncHistory` (`:329`)
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
`SongBar::layout`, `:31`) · the song's name; the transport is centred when there is room and otherwise
starts right of the menus and `kNameMin` (120 px) of name (`transportX`, `:41`), so the bar fits at
1024; the master meter and Save on the right. `App::buildMenus` (`app/App.cpp:100`): **File** — New
Song…, Open…, Save, Save As…, Render…, Render Stems… (the host's save picker, `onPickSave`, then
`project save "<path>"`, `render --out "<path>" [--stems <every strip>]`), Home; **Edit** — Undo / Redo
naming what they would take back (from `undoLabel`/`redoLabel`), Duplicate / Delete the selected clip;
**Song** — Add Mixer, Add Bus, Add Audio Line, Add Lane, Quantize Clip (the selected note clip's pattern at the
lanes' snap step, 1/32 of a beat when nothing snaps: ONE line, `pattern quantize <pt> --grid <step>`,
`app/App.cpp:132`); **View** — Hide/Show Mixer (the dock eases
to its tab bar), Hide/Show Browser (its width eases to 0, everything right of it follows the live
width), Metronome On/Off (`settings set metronome=…`), Show/Hide IDs (`settings set showIds=…`, R-UI-11,
`app/App.cpp:178`), Settings…. Labels follow the state
(`refreshMenus`, `:151`). A press outside an open menu closes it first (`:320`), as cosmo's does.

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
undo, redo `Ctrl+Shift+Z, Ctrl+Y`, settings `Ctrl+Comma`; Lanes: delete / duplicate / copy the selected clip, paste it (DR-CLIP-9); Piano roll:
select all, deselect, delete, duplicate, copy, paste, up / down a semitone, up / down an octave, earlier / later by
the snap — the keys the app had before they were settings). A combo is spelt ONE way, `Ctrl+Alt+Shift+<key>`
(`core/service/KeyCombo.h`, a pure header shared by the service and the window): A–Z, 0–9, F1–F12 and named keys
(Space, Enter, Escape, Backspace, Delete, Tab, arrows, Home/End/PageUp/PageDown/Insert, punctuation by name — `Comma`);
the codes are the window's, a letter or digit its ASCII, the rest Windows' virtual-key numbers. `settings set
key.<id>=<combos>` (`core/service/ServiceMachine.cpp:178`) takes any case and order and keeps the canonical spelling;
`none` takes every key away, `default` gives the defaults back (and drops the line); an unknown action is refused
with the nearest, a key not in the table naming it, and — once every key of the line is in, so one line can swap two
actions — a combo another action of the same keyboard has (Transport, Song and Lanes are one; the roll's is its own)
naming that action (`:209`). Only re-keyed actions are written to the machine's file (`key.<id> = <combos>`,
`core/Settings.cpp`); a hand-edited line that does not read is the defaults (`keysOf`, `Keymap.cpp:46`). The model
publishes every action with its keys and defaults (`settings.keys`, `core/service/ServiceModel.cpp:126`); the API
document's Keyboard section is generated from the same table. In the window `App::key` (`app/App.cpp:365`) spells the
press (`comboText`) and looks it up there: while a roll has the keys (R-ROLL-8) its keyboard first — `PianoRoll::action`
(`app/widgets/PianoRoll.cpp:318`), which declines a selection's action with no selection — then the song's
(`songAction`, `:441`), so the roll's keys shadow the song's only while it uses them. The host names the KEY, not the
character: GTK's keysym with no modifier (`linux_main.cpp:191`, `keyCodeOfKeysym` in `app/KeyInput.h`), so Shift+1 is
"Shift+1"; what the press types follows as a Text event (D-9). L2 `test_keys_are_settings`; UI
`test_the_keys_are_the_machines` (re-keyed by a script, the window follows; the roll's keys shadow the song's; the
host's mapping). Mutants: no conflict refusal, the window reading the defaults instead of the model — both seen red.

**The sheet's Keyboard page** (`SettingsSheet`, the last section — DR-SET-5; laid out and measured in the paint,
`paintKeyboard`, `app/widgets/SettingsSheet.cpp:1234`): a muted micro-label per area, then an action a line — its label,
its keys as chips right-aligned, a "+" at the right edge, a dot before it while it is not its defaults; "Reset to
defaults" beside the page's title, quiet while nothing is changed. A chip clicked (or "+") LISTENS (`listen`, `:534`;
the gesture at `:866`, a double-click a click there): its border and wash turn to the accent and its label cross-fades
to "Press a key…", its width easing between the two. The next key (`handleKey`, `:914`) is the slot's combo —
`settings set "key.<action>=<the keys>"`, ONE line; Backspace removes the slot's key, Escape cancels, a key no combo
can name keeps it listening, a click elsewhere lets it go. A key another action has is refused by the service and
the App's toast says which action. Reset is ONE line, `key.<id>=default` for every changed action. The chips are
keyed by combo (`bind`, `:397`): a key that arrives grows in from no width, one that leaves shrinks out where it was,
so its neighbours slide (design rule §1). Escape and Enter cannot be listened for with the sheet (they cancel and
— as Backspace — have their own meaning there); `settings set` takes them. UI `test_the_keyboard_section`
(listening caught mid-ease, a chip caught growing while the old one shrinks, a refusal toasted, Reset as one line);
shots `settings-keyboard`, `settings-key-listening`. Mutants: a listening look that snaps, chips that snap — both
seen red.

### DR-SET-5 Settings in pages; a choice is a combo box (R-SET-5, R-SET-6)
The sheet is a fixed card (720 × 520, smaller in a small window): a title band, a RAIL of the six sections — Audio,
Playback, New songs, Sample folders, Interface, Keyboard (`railRect`) — and beside it ONE page, under a footer with
Done (`onPaint`, `app/widgets/SettingsSheet.cpp:951`). Each section is a `Page` child (`:171`): clipped (its own paint
too — `clipToBounds` clips only children), input-transparent (its controls take clicks; what the sheet draws goes to
the sheet), with its own eased scroll. A section clicked is `showSection` — no line, the view's — and `advance` (`:628`)
cross-fades the chosen page in as the last fades out (200 ms) and slides the rail's highlight to it (220 ms), each
name easing to the foreground as the highlight reaches it; an open list closes and a listening key stops. The sheet
opens again on the page it was left on. A setting that is one value of a list is a `SettingCombo` (`:104`):
Artboard's `ComboBox` in cosmo's tokens (`comboStyle`, `:79`, as Genesis dresses it) — the drop, the list, its scroll
and a pick are the library's; the field is drawn here (FR-41) so its label CROSS-FADES to a new value, whoever set
it. A row picked is ONE `settings set <key>=<value>` line. A value the list lacks — a device not connected now, a
tempo or a level set from a shell — is an entry of its own ("137 bpm (set elsewhere)", `bind`, `:259`), so the setting
is never invisible. An on/off setting is a `SettingToggle` (`:149`): Artboard's `ToggleSwitch`, its knob drawn from an
eased copy of `on()`, so a value set from the model slides as a click does; a click is ONE `settings set …=on|off`.
Controls are laid out each frame (`layoutPages`, `:581`) — name and note left, control right — and a control scrolled
out of its page is hidden so it takes no input. Escape closes an open list first, then the sheet (`:944`); a click
anywhere off a list closes it. The App draws the sheet in both passes now — its card, pages and controls, then an open
list over everything (`app/App.cpp:461`). UI `test_settings_combos_send_lines_and_ease` (a list dropped, a row one
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
Every float window carries a `ResizeGrip` (`app/widgets/FloatWindow.cpp:21`) — a child of its own added after the
content, so it is drawn and hit on top of it — at its bottom-right corner (`gripRect`, 13 px; placed by
`FloatWindow::layout`, `:114`). A press raises the window; dragged, the size is where the pointer has the corner —
direct manipulation, no easing — never below `kMinW` × `kMinH` (260 × 156) nor past the layer's edge. The content
is laid out at the new size each frame: a roll shows more rows (its grid grows by exactly the height added), an
instrument's face reflows its modules. Three strokes mark the corner, brighter under the pointer. The view's: not
saved, not a command (R-WIN-4). Closes the UX audit's U-5.

### DR-AUTO-5 Automation on the timeline (R-AUTO-6)
`Timeline` (`app/widgets/Timeline.cpp:230`) appends a row per automation after the lanes, keyed
`auto:<au>` in the same `AnimatedRows` (a lane added slides them down; a new one grows in), its header
the accent stripe, its name and the address that reads it. `TimelineAuto.cpp` draws the curve over the
beat grid by the ENGINE's keys (`keysOf`, `:44` → `engine::curveKeys`; `paintCurve`, `:364`, samples
each non-linear segment with `anim::segment`) — holding the first value before the first point and the
last after, linear / hold / smooth / bezier per segment, on a log scale for Hz and ms (`valueToY`,
`:109`) — with the points as handles and a bezier point's two handle stems (`paintAutomation`, `:466`).
`autoGesture` (`:508`): a click on the row adds a point at the
lanes' snap step under the pointer (DR-UI-10; `:625`) with the value under it (`auto point add`) once the double-click has had its chance — 350 ms, a ghost point
drawn at once and fading in meanwhile (`:671`, sent by `advanceAuto`, `:323`; **AMENDED (R-AUTO-11)**:
a double-click there opens the window instead); a point dragged is drawn under the pointer while held
(its beat on the same step, `:543`) and sends ONE `auto point move <au> --at <from> --to <to> --value <v>` on release,
staying where it was let go; a double-click ON a point deletes it; a right-click offers Linear / Hold /
Smooth / Bezier, Delete Point and Delete Automation (`auto delete --unbind`). A curve the model changes
eases there point by point over 220 ms — handles too; a point added or removed, or a shape changed,
cross-fades the two curves (`shownPoints`, `:84`).

### DR-ROLL-1 The piano roll (R-ROLL-1…5, R-EDM-6)
**Grammar.** `note move <pt> --pitch <p> --at <b> [--to-pitch <p>] [--to-at <b>] [--length <b>] [--vel
<1…127>]` (`core/service/ServiceEdit.cpp:915`; a pitch by number, name or pad, DR-SVC-8) edits the one note at that pitch and beat — a moved note
landing on another's place replaces it; `pattern quantize <pt> [--grid <b>] [--swing <0…0.75>]`
(`:786`) moves every start to `k·grid`, odd `k` delayed by `swing·grid`, two notes landing together
merging into the louder. Both are edits (undoable, all-or-nothing). The model gives a pattern the strip
its first clip plays through and that strip's instrument (`patterns[].strip`, `.instrument`), and a
device type its named keys (`deviceTypes[].noteNames`, `core/service/ServiceModel.cpp:132`) — the DSP
registry's (REQ-device-6: the Drum Machine's ten pads, Kick = 36 …).
**The window.** `PianoRoll` (`app/widgets/PianoRoll.cpp`) is the content of a `roll:<pt>` window
(`WindowLayer::openRoll`, `app/widgets/FloatWindow.cpp:156`), opened by double-clicking a note clip
(`Timeline.cpp:789`) or by an instrument window's "Piano Roll" (`DevicePanel.cpp:533` — the strip's
pattern; a menu when it plays several; `clip add --strip` and then its roll when it has none). Titled
"Piano Roll — <pattern> · <strip>"; a pattern gone closes it. A toolbar: snap 1/4 · 1/8 · 1/16
(default) · 1/32 · Off, Notes | Steps; below them (DR-ROLL-11) Quantize… (a menu of three lines: straight, swing
25 %, 50 %, at the snap — and the grooves). **Notes:** keys on the left (C named with its octave, C4 = 60; a kit's keys by its pads,
nothing else named), black-key rows shaded, bars stronger; notes in the strip's colour, brighter the
louder; the velocity lane below; past the pattern's end dimmed, the end a handle (`PianoRoll::onPaint`, `PianoRoll.cpp:512`).
A click on empty grid → `note add <pt> --pitch --at <the snapped cell> --length <the last length>`; a
note dragged is drawn where the POINTER has it (beat snapped) and lands as ONE `note move … --to-pitch
--to-at`; its right 5 px — or its right third when it is narrower than 15 px, so a short note keeps a body to grab (D-7) — resize it (`note move … --length`, which becomes the last length); a
velocity stem dragged → `note move … --vel`; a double-click or right-click → `note delete`; the end
dragged → `set <pt>.length=` — a drag lands where it is let go however short (under the recognizer's 5 px
threshold too, `PianoRoll::track` at the release, the Click after it swallowed; D-6) (`PianoRoll::handleGesture`, `PianoRoll.cpp:258`). **Steps:** a row per pad (a kit) or per pitch in
use and middle C, a cell per sixteenth, a click toggling a note there (`note add … --length 0.25` /
`note delete`) (`PianoRoll::paintSteps`, `PianoRoll.cpp:482`). **Nothing snaps:** notes are keyed by (pitch, beat) — a new one
fades in, a deleted one fades out where it was, a dragged one is the pointer's and lands without
re-fading (`PianoRoll::bind`, `PianoRoll.cpp:68`; `PianoRoll::advance`, `PianoRoll.cpp:173`); the two modes cross-fade; Steps zooms to fit the pattern
(64–160 px a beat) and Notes gets its own zoom back, eased; Ctrl+wheel zooms about the pointer, the
wheel scrolls, eased; a length changed from a shell moves the end, eased.

### DR-ROLL-6 Play from the roll (R-ROLL-6)
**Grammar.** `transport play --clip <ac>` (`core/service/ServiceTransport.cpp:334`) plays the clip's span —
`at` … `at + clipLengthBeats` — LOOPED, the whole song sounding there, from its start (or from `--from` when
that lies inside it); a clip unknown or of no length is refused naming it. The song's loop is kept aside and
given back by `transport stop`, by a plain `transport play` (the song's) and when the clip is deleted
(`endClipLoop`, `:478`); a `transport loop` set meanwhile is the song's and stays. After every edit the
loop follows the clip — moved, resized, a looped clip lengthened (`followClipLoop`, `:492`, called from
`dispatch`, `core/service/SolarisService.cpp:354`) — sent to the Player at once. The model publishes it:
`transport.loopClip` (`core/service/AppModel.h:342`), with `loopFrom`/`loopTo` the clip's span while it
loops. L2: `test_a_clip_plays_looped_and_gives_the_songs_loop_back` hears the clip's kick at the top of
each of three loops and checks every way back to the song's loop.
**The window.** A roll knows its clip: the one it was opened from (a double-click or the clip's "Piano
Roll", `WindowLayer::openRoll(pt, ac)`, `app/widgets/FloatWindow.cpp:168`) while that clip lasts, else the
pattern's first in time; none when the pattern is on no track (`PianoRoll::bind`, `PianoRoll.cpp:83`). Its
toolbar starts with ▶ (`PianoRoll::togglePlay`, `:159`): `transport play --clip <ac>`, or `transport stop` as
■ while its clip loops — lit in the accent then, its glyph dimmed with no clip and the click saying nothing.
Space stops whatever plays; stopped, with a roll the front window (`WindowLayer::frontRoll`,
`FloatWindow.cpp:198` — the last open window in the stacking order), it is that roll's ▶, else the song's
play (`App::key`, `app/App.cpp:364`). The playhead is drawn where the clip is heard — the transport's beat
less the clip's start, modulo the pattern's length for a looped clip — fading in and out with the play
(`:617`), and the grid follows it a page at a time, eased, when it leaves the view (`:278`).
### DR-ROLL-7 A note is heard when it is made (R-ROLL-7)
**Grammar.** `note preview <ch> <pitch> [--vel <1…127>] [--length <beats>]` (`core/service/ServiceTransport.cpp:301`)
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
**The roll** (`PianoRoll::preview`, `app/widgets/PianoRoll.cpp:234`) sounds through the track of its clip
(DR-ROLL-6), as a line beside the edit's own: a note added (`:614`, at the last length), a note pressed, each pitch
row a dragged note enters (`:511`), a Steps cell turned on, and a key pressed or swept (`:451` — the key lit while
held, eased in and out); lengths 1/8 to 1 beat. A pattern on no track is made, not heard.

### DR-ROLL-8 A selection in the roll, and what is done to it (R-ROLL-8)
**Grammar.** `notes move <pt> "<pitch@beat> …" [--by <beats>] [--semi <n>]` and `notes delete <pt> "<pitch@beat>
…"` (`core/service/ServiceCompose.cpp:257`) act on several notes as ONE edit (one undo step); `--by` may be
negative. Refused whole, naming the first: a name that matches no note, a note that would land on one not moved
(a chord moved together may pass over its own places), a pitch outside 0–127, a start before beat 0. A copy is
`notes add` with full tokens (`pitch@beat:length:vel`), which already replaces a note it lands on.
**The roll** (`app/widgets/PianoRoll.cpp`). The selection is the view's: a set of note keys, emptied of any note
the model no longer has. A click on a note selects it, Shift+click adds or removes it (`:654`); a click on empty
grid adds a note (R-ROLL-2) and lets go of the selection; Ctrl draws a box that selects every note it touches
(Shift keeps what was selected) — wherever the press is, even on a note: Ctrl is selecting and never moves a
note, and a Ctrl+click on a note selects it (the box of no size touches it) (U-10, R-ROLL-8 amended). Selected notes are lit — brighter, outlined in the
accent — eased in and out (120 ms). A selected note dragged (with others selected) carries the whole selection
with the pointer, kept at or after beat 0 and inside 0–127, heard at each row the pressed note enters (R-ROLL-7),
and lands as ONE `notes move` (`:875`, `moveSelection` `:283` — the moved notes appear where they were let go
at once, still selected). Keys (`PianoRoll::key`, `:303`): ↑/↓ a semitone (Shift: an octave), ←/→ a snap step
(a sixteenth when Off) — `notes move`; Delete/Backspace — `notes delete`; Ctrl+D — the selection again right
after itself, by its span (earliest start to latest end, up to the snap), `notes add`, the copy selected
(`addSelected`, `:300`); Ctrl+C — to the clipboard every roll shares (the window layer's); Ctrl+V — pasted into
the roll at the same beats, selected; Ctrl+A — every note; Escape — none. The keys reach the roll the pointer
last pressed in: `App` notes on every press whether it fell in the front roll (`app/App.cpp:101`) and offers the
key to that roll first (`:379`) — a press on the lanes gives the keys back to them.

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
The roll's toolbar names it after Quantize… (`PianoRoll::lengthRect`, `app/widgets/PianoRoll.cpp:374`; drawn at
`:1213`): "Length 1/16*" and a caret — a note value (`lengthName`, `:398`: 1/32 … 1/1, a beat being a quarter, else
"<n> beats"), starred while it is the LAST length (the note last made or resized, the default). Its menu (`:788`)
offers "Last used (…)" and 1/32, 1/16, 1/8, 1/4, 1/2, 1/1; the choice is the view's — no line — and a click on the
grid makes a note of `clickLength()`, the choice or the last length. Closes the UX audit's U-4.

### DR-ROLL-11 Note tools (R-ROLL-11)
Each tool is ONE edit on a pattern (`core/service/ServiceCompose.cpp`), on every note or on the ones `--notes
"<pitch@beat> …"` names (`scope`, `:228`), and where it draws, a seeded splitmix64 stream (`Rng`, `:75`; `seedOf`,
`:87` — `--seed`, else a hash of the notes, printed), so the same line on the same notes writes the same notes.
**Scale lock** — `pattern scale <pt> <key> <scale>` | `off` (`:361`; `#apattern … scale="D dorian"`): twelve scales
(`scaleKinds`, `model/Project.cpp:1200`; a key in sharps, `parseScale` `:1219`), published as `patterns[].scale` and
its pitch classes `scaleMask`. A note PLACED — `note add` and each note of its `--chord` (`core/service/ServiceEdit.cpp:921`),
`notes add` — goes to the nearest pitch of the scale, a tie DOWN (`snapToScale`, `model/Project.cpp:1252`), which
makes a major chord stamped on any degree that degree's own triad; a note MOVED in pitch (`note move --to-pitch`,
`ServiceEdit.cpp:913`; `notes move --semi`) goes on the way it moved to the scale's next pitch, two moved notes landing
on one refused whole. A move in time, an import, a step row, a recording and `pattern transpose` keep their pitches.
**Chance** — `Note::chance` 0 … 1 (`#note … chance=0.5`, written below 1; the note token's fifth field
`C4@0:0.25:100:0.5`; `note add|move --chance`, `notes set <pt> "<sel>" --vel --chance --length`, `:338`): the compile
rolls it per clip and per pass of the pattern (C4, `core/Compile.cpp:418`) by `noteSounds` (`model/Project.cpp:1302`
— FNV-1a over the clip id, splitmix64's finaliser over the pass, the pitch and the tick, 53 bits against the chance):
no clock, so a render is pure and two clips of one pattern vary apart; `project export-midi` rolls the same
(`core/service/ServiceMidi.cpp:237`), so the file is what the render plays. **Grooves** — `pattern quantize --groove
<name> [--strength 0…1] [--notes]` (`:390`): nine templates (`grooves`, `core/service/Notation.cpp:172` — the MPC's
54/58/62/66 % swings, shuffle, hip-hop, house, push, lazy; empirical, listed in the API document) of grid steps from
beat 0, each an offset (a fraction of a step) and an accent (a velocity scale); strength moves a start and a velocity
that part of the way; a groove with `--grid`/`--swing` refused. **Humanise** (`:440`): each start within ± `--timing`
beats, each velocity within ± `--vel` (neither given: 0.02 and 8), a triangular spread; never before beat 0, never
onto another of its pitch (it keeps its time). **Legato** (`:473`): each note to the next later start among those
acted on, a chord's notes together, the last to the pattern's end — overlaps shortened. **Arpeggiate** (`:491`): on
each `--rate` step of the beat grid inside the notes' span, what is held there (pitches ascending, a pitch once,
the loudest) over `--octaves`, indexed up, down, updown (a cycle of 2n−2: the turns once) or by the seeded stream;
`--gate` of a step long, at the held note's velocity and chance; the index back to the first after a step with
nothing held; the notes arpeggiated replaced, a step on another note of its pitch replacing that one. L2
`test_scale_lock_places_and_moves_notes_on_the_scale`, `test_note_chance_is_rolled_per_pass_and_the_render_stays_pure`
(heard 21 of 32 at 0.5; the same bytes twice; a second clip apart; the exported notes = the heard),
`test_the_note_tools_humanise_groove_legato_arpeggiate`; model `test_note_chance_and_scales_round_trip_and_are_checked`.
Mutants: the roll forgetting the clip, a tie snapping up, updown repeating its turns, humanise drawing from a stream
not of its seed, the compile or the MIDI file ignoring chance — each red. **The roll's tools.** The roll's toolbar is two
rows (`PianoRoll::kBarH`; its window opens 720 × 520, `app/widgets/FloatWindow.cpp:247`): ▶, snap and Notes | Steps;
then Quantize…, Length, Scale, Chord and Tools (`quantizeRect`, `app/widgets/PianoRoll.cpp:375`), each menu chip with a
drawn caret (`caret`, `:29` — the UI font has no ▾). Every tool acts on the selection when there is one, else the
pattern (`notesFlag`, `:393`, ` --notes "<sel>"`), each ONE line; the menus list what the model publishes —
`chords`, `scales`, `grooves` (`AppModel`, filled from `chordQualities` / `scaleKinds` / `grooves`). **Scale ▾**
(`:885`): No scale, the scales in its key (C until one is chosen), Change key… (a second menu of the twelve, keeping
the scale) → `pattern scale`; the rows off the scale dim and their keys darken, per pitch class, eased (`:476`).
**Chord ▾** (`:906`): Single notes, or a quality — the view's; a click on the grid then stamps it, its root the row
clicked (`note add <pt> --chord <root><q> --octave <o> --at --length`, `:969`), a scale-locked pattern making it the
key's own. **Tools ▾** (`:917`): Humanise, Legato, Arpeggiate up · down · up-down · random (at the snap), Chance 50 % ·
100 % (`notes set`). **Quantize…** adds a line per groove. **The Chance lane** — a fourth tab beside the lane below
(`kVelH` 65): each note's odds as a stem, dragged as velocity is, to a hundredth (`:662`), landing ONE `note move
--chance` (`:1205` draws them); a note is drawn fainter the less it sounds. A velocity or chance changed elsewhere
(humanise, a groove, `notes set`) eases to its value (`:459`). UI `test_the_rolls_note_tools`; shots `piano-roll-tools`,
`piano-roll-scale-fading`. Mutants: the scale's rows snapping, a chord click sending one note, a shell's velocity
snapping, the tools ignoring the selection — each red.

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
**One text:** Solaris's `parseParam` / `paramText` (`core/Compile.cpp:119`, `:121`) ARE the DSP library's
`paramFromText` / `paramToText`, so a `.slp`'s device values and a plugin's state are the same text by
construction (the model and service suites, unchanged, still pass on it). The device panel's log taper
is the same formula as `normalizedFromValue`'s (N2) but its own copy: the front end does not link the
DSP library (law 13), and every value it sends is parsed by the service.
**Verified** (root `ctest`): `vst3_validate_synth`, `vst3_validate_drums` — the SDK's validator, 47
tests, 0 failed, each; `vst3_equivalence` — each bundle hosted offline equals the device rendered
Solaris's way, 0 of 96 000 samples differing, not silence, its state read back as text.

### DR-MIX-15 The sidechain: a key into a later strip's compressor (R-MIX-15, R-EDM-3)
`send add <ch> --to <ch2> --sidechain [--pre] [--gain]` makes a KEY (`#asend … sidechain=true`,
`<send>.sidechain` settable): `feedsForward` (`model/Project.cpp:897`) — the one rule, later in processing
order, its own mixer included, as for every route since R-MIX-4's amendment — and the validator refuses a
key to the master or a port (`:989`); `strips[].keyTargets` (`keyTargetsOf`, `:916`) is what a
picker offers. The engine sums a key into the target's KEY buffers instead of its input (`routeKey`,
`engine/Engine.cpp:490`, through its latency-compensating delay, DR-MIX-17) and hands them to every device
whose type `takesKey` before it runs (`:653`) —
the DSP Compressor with Sidechain on detects on it (DSP REQ-fx-sidechain-1); silence when nothing keys
it. A strip silenced by another's SOLO still keys (`keyLive`, `core/Compile.cpp:296`; `engine/Engine.cpp:726`) and
a key does not pull its source into a solo (`Compile.cpp:52`), so a soloed bass keeps its pump while the
kick stays silent; a MUTED strip keys nothing (live, a mute in a song with keys swaps the engine,
`core/service/ServiceTransport.cpp:494`). The audit names a key that no compressor with Sidechain on
hears (`core/service/ServiceModel.cpp:558`). In the dock a key reads "key <target>" in the solo amber,
and a strip's menu offers "Sidechain to ▸" = its `keyTargets` (`app/widgets/MixerDock.cpp:1236`).
Measured (L2): a ghost kick (−120 dB fader, pre-fader key) through −30 dB at 4:1 dips a −6 dBFS bass
6–22.5 dB 15–35 ms after each kick, back within 1 dB before the next; identical with the bass soloed;
nothing with the kick muted.

### DR-EDM-2 The metronome (R-EDM-2, R-TIME-4)
`settings set metronome=on|off`, `metronomeLevel=<dB>` (machine settings, DR-SET-3). The click is the
`Player`'s (`core/Player.cpp:463`), after the engine, so it is in what is heard and never in a render: its
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
the accent, and tints the region on the lanes under the clips (`app/widgets/Timeline.cpp:1317`, `:1152`;
`loopRect`, `:581`). It is eased (`:442`): it fades in where it is when a loop appears, fades out when
it goes, and a loop changed by anyone moves there over 220 ms. `loopGesture` (`:589`): Shift-drag on the
ruler draws the brace under the pointer (both ends on the lanes' snap step, like a seek — DR-UI-10,
`:598`) and sends ONE `transport loop <from> <to>` on release, the brace staying where it was let go
(both ends on one line is a Shift-click: no region, `:612`); a click inside the brace sends `transport
loop off`; a plain click elsewhere on the ruler still seeks. The engine already loops (DR-PLAY-1), so
the playhead wraps at the brace's end while playing.

### DR-EDM-8 A sampler (R-EDM-8)
The DSP library's `sampler` (REQ-inst-sampler-1: chromatic or one-shot, a span, reverse, an ADSR) is a
registry instrument that `takesSample`: the host decodes, the device copies. Solaris stores its sound
on the device node (`#aeffect … sample=<path>`, relative to the song as a clip's `src`), sets it with
`strip add --kind instrument --instrument sampler --sample <file>`, `clip add --instrument sampler
--sample <file>` (one drop, one command) or `set <dv>.sample=<file>` (`""` takes it away) — resolved and
refused unread by `resolveSample` (`core/service/ServiceEdit.cpp:124`; `:195`; `SolarisService.cpp:694`),
refused on a type that plays none. Compile decodes it through the same cache as clips and puts it on the
device's description (`core/Compile.cpp:216`); the engine hands it in at build, never on the audio thread
(`engine/Engine.cpp:66`); a change of sound is a structural live update (an engine swap). The model
gives `devices[].takesSample`, `.sample` and `deviceTypes[].takesSample`; the audit names a sampler with
no sound or an unreadable one (`core/service/ServiceModel.cpp:537`). In the UI a sampler's window names
its sound where others name their type (`DevicePanel::sampleText`, `app/widgets/DevicePanel.cpp:456`),
and a browser sample dragged over it lights the window (eased) and drops as ONE `set <dv>.sample=`
(`WindowLayer::samplerAt`, `app/widgets/FloatWindow.cpp:303`; `app/widgets/ProjectScreen.cpp:100`).
Measured (L2): at the root the render IS the decoded file sample for sample (a mutant whose engine skips
`setSample` fails it), an octave up is 2 kHz and over in half the time.

### DR-EDM-9 Audition in the browser (R-EDM-9, R-BROWSE-2)
`audition <file>` / `audition stop` — a machine command (not an edit: no undo, the song stays saved),
with or without a song open. The file is decoded through the clips' cache and played by an
`Auditioner` (`core/Auditioner.cpp:40`) — its OWN output stream and thread, so a preview is heard
whether the song plays or not, at the `auditionLevel` setting (dB, default −6), to its end, then it
stops itself; it never allocates on its thread and is never in a render (a render does not know it).
`auditionCommand` (`core/service/ServiceTransport.cpp:273`) refuses a file it cannot read naming it; the
service's `pump` (`:76`) carries its progress into `audition.{file, playing, progress}` and announces its
end (`audition.changed … playing=0`); the model keeps it across refreshes, and the App re-binds while it
plays and once when it ends (`app/App.cpp:273`). In the browser a click on a sample hears it, a click on
the one being heard stops it (`app/widgets/Browser.cpp:440`); its row fills as it plays with an accent
bar along its foot, keyed by the file and eased in and out (`:260`, `:483`) — a double-click still places
it. Settings › Playback has PREVIEW LEVEL. Measured (L2): the live stream carries the decoded file at
the audition level, sample for sample; it ends itself, said.


### DR-UI-10 The grid follows the zoom (R-UI-10, R-TIME-5)
**The levels.** The lanes' grid has seven: a bar (the song's meter), a beat, and 1/2, 1/4, 1/8, 1/16
and 1/32 of a beat (`Timeline::gridSpan`, `app/widgets/Timeline.cpp:59`). How much of a level is drawn
is a smoothstep of its line spacing at the EASED zoom — nothing closer than 6 px, all of it from 16 px
apart (`kGridHidePx`, `kGridFullPx`, `app/widgets/Timeline.h:101`; `room`, `Timeline.cpp:30`;
`gridAlpha`, `:64`) — recomputed every frame, so a Ctrl+wheel zoom (eased 220 ms) fades a level in or
out with it and never pops one. A line is drawn once, by the coarsest level it belongs to: bars
strongest (white 7 %), beats next (4 %), the divisions faintest (2.2 %); every fourth bar is always
drawn, so a far-out song keeps its phrases (`paintGrid`, `:940`). The ruler repeats the levels as ticks
— long for a bar, shorter as they get finer — and its labels are MEASURED against their room: a bar's
number fades in as its every-1/2/4/… bars get room for the widest number in view, and zoomed in the
beats are named `bar.beat` (`2.3`), fading in as a beat gets room for one (`paintRuler`, `:962`; the
labels `:751`). The old fixed thresholds, which swapped the labels in one frame mid-zoom, are gone.

**The step.** THE snap step is the finest level whose lines are at least 11 px apart (`kSnapPx` — past
half drawn: you snap to lines you can see), never coarser than a bar; at the deepest zoom it is 0 and
nothing snaps (`snapStep`, `:73`; `atDeepestZoom`, `:70`). `snap()` (`:80`) is the ONE rounding of
every gesture on the lanes: a ruler click → `transport seek` (`:575`), a clip drag (`:538`), the loop's
Shift-drag (`:469`), an automation point added or dragged (`app/widgets/TimelineAuto.cpp:496`, `:222`),
the browser's drop and its hint (`app/widgets/ProjectScreen.cpp:110`) — at the deepest zoom the tick
under the pointer. Every gesture is still ONE command, its beats printed to the tick: rounded to 1/960
and spelled with the fewest decimals that keep it (`Timeline::beatText`, `Timeline.cpp:44` — the `%g`
it replaces kept six significant digits, so past beat 1000 a line lost ticks). The step is published
LIVE for tests (`snapStep`, `snapLabel`, `gridAlpha`, `snapLabelAmount`, `Timeline.h:120`) and named in
the ruler's corner over the lane headers — `Snap Bar`, `1/4` … `1/128`, `Off`, in the piano roll's note
values (a beat = 1/4) — cross-faded 200 ms when it changes, older names fading out from where they are
however fast the wheel turns (`stepName`, `Timeline.cpp:88`; `advance`, `:310`; `paintRuler`, `:723`).

**The zoom.** A lattice of ×1.25 notches about 28 px/beat, 8 out (4.7 px/beat: a 4/4 bar is 18.8 px)
and 14 in (637 px/beat), so a notch in and a notch out are exact inverses and the deepest zoom is one
place (`:620`). **Decision:** the deepest zoom rose from 320 px/beat so that 1/32 of a beat gets room —
20 px apart at 637, fully drawn, and THE step one notch before snapping turns off. The step by zoom:
a bar below 11.5 px/beat, a beat from 11.5, 1/2 from 22.4 (a song opens at 28: bars, beats and halves
drawn, the step 1/2), 1/4 from 55, 1/8 from 107, 1/16 from 209, 1/32 from 408, nothing at 637.

**Proof.** `test_the_grid_follows_the_zoom` (`app/tests/ui/uiTests.cpp:2070`) drives the real service:
at the default zoom a ruler click at beat 3.3 seeks 3.5 (a fixed quarter says 3.25); a level's alpha
is caught mid-fade during an eased two-notch zoom, between its two ends; at 1/8 a seek, a clip drag
(on the step while held, then `clip move ac_1 --at 3.125`) and an instrument dropped from the browser
land on the same step, the step's name caught mid cross-fade; at the deepest zoom a click seven ticks
past beat 2 is exactly `transport seek 2.007`; all the way out a click at 9.9 is `transport seek 8`.
Mutants — a fixed 1/4 snap, a level's alpha taken from the TARGET zoom (it pops), a deepest zoom that
snaps — each fail it. Shots `lanes-zoomed-in`, `lanes-zoomed-out`, `lanes-zoom-mid` (mid Ctrl+wheel)
(`app/tests/shots/renderShots.cpp:219`).
### DR-MIX-16 The mixer's numbers bound (R-MIX-16)
**One menu.** `paramMenuItems` (`app/widgets/ParamMenu.cpp:51`) builds the right-click menu of every number a
formula can drive from a `ParamTarget` (address, formula, value as shown, reset, bindable): Create Automation
unless it already IS one (`auto create <address>`), Formula… (cosmo's rename field seeded with the formula or
"=", → `set <address>="=<typed>"`, quoted when it has a space), Clear Binding when bound (`bind clear
<address>`), Reset to Default (`set <address>=<default>`), and Copy Address / Copy Value / Copy as Formula
(DR-UI-11). The device window (`DevicePanel::openParamMenu`, `app/widgets/DevicePanel.cpp:499`) and the dock
(`MixerDock::openParam`, `app/widgets/MixerDock.cpp:964`, offered first on a right-click, `:1197`) both call
it, so the menus cannot drift; a hosted third-party VST3's parameter list (R-VST-6, not built yet) will call
it the same way. In the dock: a fader → `<ch>.gain`, a pan → `<ch>.pan`, a send's row on a card or its cell
in the matrix → `<sd>.gain`, the master fader → `project.masterGain` (the addresses that already bind,
R-AUTO-1); Reset is 0 (unity, the centre); any other right-click on a card is still the strip's menu.

**The value at the heard position, published.** `bindings[].live` (`AppModel.h`). Stopped: `evaluateLive`
(`core/service/ServiceModel.cpp:445`) compiles the song as `eval` does and evaluates every binding with the
engine's own `evaluateBinds` at the transport's position (an inert binding: its own value) — so it equals
`eval <address> --at <position>`. Playing: after every block the `Player` copies `Engine::bindValues()` into a
ring of 64 slots of pre-sized atomics (`keepLive`, `core/Player.cpp:280` — stores only; a sequence number
brackets each write), each stamped with the frames handed to the device and the generation of the engine
that played it; `pump` (`core/service/ServiceTransport.cpp:247`) reads the slot the listener hears now —
the newest at or before the newest minus the device's latency, of the engine the service last built
(`liveValues`, `core/Player.cpp:298`; `buildLive` bumps the generation, `ServiceTransport.cpp:58`, and a
`Swap` carries it, so a new bind order is never read with the old one) — and maps the engine's bind
order to addresses. While playing, `refreshModel` keeps the pumped values and evaluates only a binding new
since (`ServiceModel.cpp:81`, `:340`). At most 256 bindings report (`Player::kMaxBinds`). Not in the stable
dump: it moves with the transport.

**The dock draws it** (`MixerDock::advance`, `app/widgets/MixerDock.cpp:589`): a bound fader or pan takes its
live value; while playing it is SET each frame — it follows the audio like the playhead — and stopped a change
(a seek, a curve edited) eases 220 ms; a new driver (the formula changed) or play pressed mid-ease is a
catch-up ease that lands when it ends and then follows (`follow`, `:629`). A bound send shows its live
level. What drives each: a tag over the fader — `auto au_1`, `= ch_3.gain`, `= -2 + 2 * s…` (`paintTag`,
`:1285`) — "ƒ" after a bound pan and before a bound send's level, the readout in the accent; every tag fades
in and out (200 ms) and its text cross-fades when the formula changes (`:719`).

Measured. L2 `test_a_bound_value_is_published_live` (`core/tests/serviceTests.cpp:1706`): stopped, `live` is
`eval`'s number at the position; playing (a fake clock device), a −2 dB-a-beat ramp is within 0.1 dB of the
curve at the heard position at beats 2…12 and falling, a link (`=ch_2.gain / 2`) exactly half of it, a device
formula `1000 + 500 * sin(beat)` within 25 Hz; after a structural edit while playing the new engine's order is
read; 0 allocations on the audio thread. Mutants: `pump` ignoring the player → stuck at 0 dB at beat 2;
`bindValues()` copied on the audio thread → 1019 allocations. UI `test_the_mixers_numbers_bind_from_the_dock`
(`app/tests/ui/uiTests.cpp:1853`): a fader's right-click is the parameter menu, Create Automation is `auto create
ch_2.gain` and the tag fades in (caught mid-tween); Formula… on a pan sends `set ch_3.pan="=0.25 - 0.75"` and
the knob EASES onto −0.5; a send copies `-8.0 dB` and `=sd_1.gain`; the master copies `project.masterGain`;
Clear Binding fades the tag out; playing, the fader equals `faderPos(live)` every frame and moves. Mutants:
the dock ignoring `live` → the pan never reaches −0.5; easing instead of following while playing → fader
0.707946 against live 0.705559. Shots `mixer-bound` (a fader on an automation, one linked to it, an auto-pan,
a send and the master on formulas) and `copied-toast`.

### DR-UI-11 IDs shown and copied (R-UI-11)
**The setting.** `showIds = on|off` is a MACHINE setting (`core/Settings.cpp:24`), `settings set showIds=…`
(`core/service/ServiceMachine.cpp:106`), published as `settings.showIds` — so an agent turns the ids on as the
View menu does (View › Show IDs / Hide IDs, `app/App.cpp:178`).

**Drawn, faded.** `ProjectScreen` eases ONE amount from the model's setting (`app/widgets/ProjectScreen.cpp:204`,
200 ms; `idsAmount()` is the live value) and hands it to every widget that draws ids; `drawNameWithId`
(`app/widgets/ParamMenu.cpp:98`) draws a name with its id right-aligned in the accent's mono, the name giving
way by the EASED amount. Where: lane headers (the lane's id; a strip's own row, the strip's) and automation
rows (`au_n`) (`app/widgets/Timeline.cpp:1382`, `:893`); clips (`ac_n`, and a note clip's pattern `pt_n`;
`:736` — "linked ×N" fades out for them); in the dock each strip card (`ch_n`), the master (`master`), each
rack chip (`dv_n`) and each send (`sd_n`, cross-fading with "→ name") (`paintCard`,
`app/widgets/MixerDock.cpp:1348`); a device window's rows show their FULL address (`dv_1.filter.cutoff`) in a
pill over the label (`ParamIds`, `app/widgets/DevicePanel.cpp:146`); its header already names its id.

**Copied, by the host.** Copy Address / Copy Value / Copy as Formula on a device parameter row and on the
dock's faders, pans, sends and master (DR-MIX-16; a choice row has no "as Formula" — no formula reads a
choice); Copy ID on a clip (`app/widgets/Timeline.cpp:1063`), a strip's and a mixer tab's menu in the dock; Copy ID
and Copy as Formula (`=au_1`) on an automation row (`app/widgets/TimelineAuto.cpp:560`). The clipboard is the
HOST's (the core has none, R-SVC-4): `App::onCopy`, which `linux_main.cpp:240` gives GTK's CLIPBOARD and
PRIMARY selections and the test rig records (`Rig::copied`). A copy says so: `App::copy` (`app/App.cpp:221`)
toasts "Copied dv_1.filter.cutoff", outlined in the accent (a refusal's toast stays red). When hosting lands,
a third-party VST3's parameter list (R-VST-6) builds a `ParamTarget` per row and offers this same menu.

Measured. L2 (`test_the_machine_settings_folders_devices_and_recents`): `settings set showIds=on` reaches the
model and the file and a second process reads it; `showIds=yes` is refused. UI `test_ids_shown_and_copied`
(`app/tests/ui/uiTests.cpp:1951`): View › Show IDs sends `settings set showIds=on`, the ids' amount caught between 0
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
`model/Project.h:235`) keeps `shape=` and gains Interstellar's spelling for a bezier point's handles —
`speedIn inflIn speedOut inflOut`, speeds in the automation's unit PER BEAT (the `.slp`'s timebase),
influences in % (0 < x ≤ 100), read always (`model/Project.cpp:407`), written only for a bezier point
(`:862`), so a file of linear / hold / smooth points reads and writes byte for byte as before.
`engine::curveKeys` (`engine/Expr.h:140`) is the ONE mapping of a point onto a key's two sides, used by
the compiler and the timeline's drawing alike: linear → both sides linear; hold → its out side holds;
smooth → After Effects' Ease on the segment after it (speed 0, influence ⅓ on both ends — a Bézier
whose time is exactly linear, so its value is exactly R-AUTO-4's smoothstep, to 1e-9 measured) unless
the next point is bezier, whose own handle wins on its side; bezier → both sides bezier with its
handles. `compileCurve` (`core/Compile.cpp:565`) converts beats to samples (speeds ÷ samples per beat)
and ranges the curve: `Curve::valueAt` (`engine/Expr.h:182`) clamps to the automation's min/max, so a
handle's overshoot stops where the drawing (which clamps to the row) stops. Grammar: `auto point shape
<au> --at <b> [--shape bezier] [--speed-in <v>] [--influence-in <%>] [--speed-out <v>] [--influence-out
<%>]` — flags not given are kept, so a bezier point takes one handle's numbers alone; refused, naming
it, are a handle flag on a point that is not (or is not becoming) bezier, an influence outside
0 < x ≤ 100 %, a speed that is not a number, no flag at all (`handleFlags`,
`core/service/ServiceAuto.cpp:33`; `:200`). `auto point add --shape bezier` starts flat (speed 0,
influence 33.333 %, Interstellar's default key). In the timeline (`app/widgets/TimelineAuto.cpp`) —
cosmo's `CurvePanel` gesture over Interstellar's numbers: a handle's end is its influence of the
segment on its side along its speed (`handleEnd`, `:63`); Alt-drag a point pulls out symmetric handles
(both sides the pointer's slope, both reaching the pointer's distance in beats), a handle dragged moves
its own side and MIRRORS the opposite, Alt-drag a handle moves only its own (`dragHandle`, `:214`); a
handle is clamped between its point and the neighbour. While held it is drawn under the pointer
exactly (`drawnPoints`, `:166`; `autoHandleAt`, `:192`); the release sends ONE `auto point shape …
--shape bezier --speed-in … --influence-in … --speed-out … --influence-out …` with the numbers it
draws parsed back from that text, so the model returns exactly what is drawn and nothing eases after
(`:437`). Measured (L2, `solaris_service`): a bezier gain −24 → 0 dB over 8 beats with 80 % / 70 %
influences renders, at beats 1…7, the level `eval` prints (= the cubic solved independently by Newton,
to 1e-6 dB) within 0.05 dB (worst 0.019 dB); the engine test checks linear and hold bit for bit, smooth
against smoothstep, bezier against the cubic to 1e-9 and no allocation.

### DR-AUTO-7 The automation's window (R-AUTO-11)
A double-click on an automation row's HEADER (`app/widgets/TimelineAuto.cpp:511`), or on its curve away
from a point (`:536` — the first click's pending point is dropped, so the gesture adds nothing),
calls `onOpenAutomation` (`app/widgets/ProjectScreen.cpp:62`) → `WindowLayer::openAutomation`
(`app/widgets/FloatWindow.cpp:173`): an `auto:<au>` FloatWindow (fading in and out as every window does)
around an `AutomationPanel`, titled "Automation — <name>" and closed when anyone deletes the automation
(`WindowLayer::bind`, `app/widgets/FloatWindow.cpp:255`). Every line is the model's (`AutomationPanel::bind`,
`app/widgets/AutomationPanel.cpp:56`): name, id, range in its unit, the address it was made from, its
points (beat, value, shape — a bezier point's speeds and influences), every formula that reads it
(`usedBy`, each with its binding's formula from `bindings[]`), and its value at the playhead —
`automations[].now`, which the service computes with the engine's own compiled curve
(`compileCurve`) at `transport.position` in `refreshModel` (`core/service/ServiceModel.cpp:414`,
`refreshNow`, `:430`) and again in `pump` while playing (`core/service/ServiceTransport.cpp:270`); it is
left out of `state print --stable`, as the position is. The rows are an `AnimatedRows` keyed by what
they say — a point added slides the rows below and fades in, an edit or a rename cross-fades its row;
the value at the playhead follows the transport continuously while playing and eases to a seek or an
edit (`app/widgets/AutomationPanel.cpp:105`). Rename is cosmo's field over the header's button and ONE
`set <au>.name=…` (an address that already existed, R-AUTO-4; `app/widgets/AutomationPanel.cpp:185`). A double-click ON a point still deletes it
(R-AUTO-6). Measured (L2): `now` equals `eval` at the sought beat and the cubic while playing; (UI) the
window caught mid-fade, the facts present, `now` and a new row caught mid-tween, the rename one line.

### DR-AUTO-8 An automation loops; a pump in one line (R-AUTO-12, R-AUTO-13)
`Automation::loop`, `at`, `length` (beats, `model/Project.h`) — `set <au>.loop|at|length=<beats ≥ 0>`
(`core/service/SolarisService.cpp:794`), read back by `get`, published on `automations[]`, written on `#aauto` only
when it loops (`model/Project.cpp:877`, docs/project-format.md §8a), refused below 0 naming the field. `compileCurve`
turns them into samples (`core/Compile.cpp:585`) and the engine's `Curve::cyclePos` (`engine/Expr.h:186`) folds a
position into the cycle before the same evaluator reads the curve: `(s − from) mod loop` inside the span, the cycle's
end (`loop`) outside it — no allocation, deterministic, so a render, playback and `eval` agree (R-AUTO-7) and the
model's `now` is that value too. `auto create <address> --pump` (`core/service/ServiceAuto.cpp:139`) makes the
automation a one-beat cycle — a point on the beat at the property's value less 18 dB (a level in dB) or 20 % of the
way up its range (anything else), `smooth`, and a point at half a beat back at its value — loop 1, named
"<owner> · <parameter> (pump)", bound as `auto create` binds. L2 `test_an_automation_loops_and_a_pump_is_one_line`
(eval repeats every beat; a render's level at six beats is eval's within 0.15 dB, the dip > 15 dB; `.at` / `.length`
bound the repeats and outside them it rests at the cycle's end; loop 0 is the curve as drawn; the `.slp` round
trip). Mutants: an engine that ignores the loop, a file that forgets it — both seen red. (The window's half — the
row's span and its grips, the Loop combo, ParamMenu › Create Pump — is M2b, below.)
**In the window (M2b).** A looping automation's row (`Timeline::paintLoop`, `app/widgets/TimelineAuto.cpp:302`) is drawn
twice over: the whole of it FAINT — sampled every 2 px through the same evaluator and the same cycle rule the engine
uses, so the repeats and the rest outside the span are what plays — with a seam at each repeat's start (when the
cycles are 6 px or wider); then its FIRST cycle bright, clipped to that cycle, with its points and handles. That first
cycle is drawn at the span's start: every beat↔x of the row's points goes through `autoOffset`, so a point dragged
or added there is in the curve's own beats; a point is kept within its cycle while dragged, and a click in a repeat
adds the point at its place in the cycle. The span is a brace along the row's top with a grip at each end
(`spanOf` / `spanGripAt`, `:271` / `:279`; the resize cursor over them, R-UI-14): taken (`:509`), the pointer has it
— the end never closer than one cycle to the start — and let go (`:566`), ONE line: `set <au>.length=…`, or for the
start `set <au>.at=… <au>.length=…` so the end stays put. A span set from anywhere eases there (`advanceAuto`,
`:349`); one let go is already where it lands. The automation's window (`AutomationPanel`) says the loop in a Loop
row ("every 1 beat · from beat 4 for 8 beats") and offers it as a combo box in its header (`SettingCombo`,
`app/widgets/AutomationPanel.cpp:45`: Off, 1/4, 1/2, 1, 2 beats, 1, 2, 4 bars, and a value set elsewhere kept
visible) — a row picked is ONE `set <au>.loop=…`. Right-click any bindable number (`ParamMenu`,
`app/widgets/ParamMenu.cpp:61`): Create Pump beside Create Automation — `auto create <address> --pump`. UI
`test_a_looping_automation_is_seen_and_made` (Create Pump from a fader; the span where the model says; the grip's
cursor; both grips one line each; a shell's span eased, caught mid-way; a click in a repeat folded into the cycle; the
Loop combo one line, its label caught cross-fading); shot `automation-pump`. Mutants: a span that snaps, a click in a
repeat not folded — both seen red.

### DR-SVC-8 Composition is agent-sized (R-SVC-8)
Built from the audit of an agent's 16-bar song (110 commands, 64 single `note add`s, every id guessed,
edits lost between calls, comment lines reprinting ids). The guide is `docs/AGENTS.md`.
- **A session that lasts.** `solaris-cc shell [--song <f>]` (`cli/main.cpp:180`) reads stdin lines into
  ONE service — the open song, its ids and its undo history last the session; `--song` opens the file
  or makes it when it is not there; a refusal is reported and the session goes on (exit 3 at the end).
  Every run ends in `finish` (`:170`): a song left with unsaved edits is named on stderr and the run
  exits **4**, unless `--discard`.
- **Scripts that say where.** A refusal prints `refused: line N: …` — the script's own line number, or
  the chained command's (`run`, `:158`); `--keep-going` runs past refusals and still exits 3 (`:227`).
  `parseCommand` drops a `#` that starts a word outside quotes (`withoutComment`,
  `core/service/Command.cpp:494`) — `F#3` stays a pitch; a comment or blank line clears the output
  (`core/service/SolarisService.cpp:60`), so nothing is printed twice.
- **Everything made is said.** `dispatch` diffs the song's ids around an edit (`:277`): the first line
  stays the command's own id, then `made: strip=… device=… pattern=… lane=… clip=…` lists the rest by
  kind. Each made node is announced by its own `project.changed` — the instrument of a new instrument
  strip (`core/service/ServiceEdit.cpp:256`), an auto pattern (`:602`), every new lane (`:454`).
- **Notes in bulk, by name.** `core/service/Notation.cpp` is the notation, pure: `parsePitchName`
  (`:41`, C4 = 60, `#`/`b`, c-1 = 0), `chordQualities` (`:80`, 21 qualities — maj, m, 5, dim, aug,
  sus2, sus4, 6, m6, 7, maj7, m7, mmaj7, dim7, m7b5, 7sus4, add9, madd9, 9, maj9, m9), `chordPitches`
  (`:108`, `--octave`, `--inversion` raising the lowest notes), `parseNoteToken` (`:136`,
  `<pitch>@<beat>[:<length>[:<vel>]]`), `parseSteps` (`:168`). `SolarisService::pitchOf`
  (`core/service/ServiceCompose.cpp:118`) resolves a pitch anywhere — `--pitch`, `--to-pitch`, a note
  token, a step row — as a number, a name, or a pad of the kit that plays the pattern (the registry's
  note names, case/space/hyphen-blind; every kit's when no clip plays it yet), refusing an unknown one
  with the pads listed. `notes add` (`:191`) parses every token before touching the pattern and refuses
  the first bad one by number and text — ONE edit, ONE undo step. `note add --chord` (`ServiceEdit.cpp:778`)
  adds a chord as one edit and prints its notes. `pattern steps` (`ServiceCompose.cpp:190`): x / X (127) /
  `.` per `--step` from `--at`, replacing that pitch in the span. `pattern duplicate | clear [--pitch] |
  delete` (refused while a clip plays it, naming the clips, `:248`) `| transpose --semi` (refused whole if a
  note would leave 0–127, `:259`). A note written past its pattern's end grows the pattern (DR-ROLL-9 — it used
  to be warned, `pastEndWarning`, `:122`, which the audit still uses); one left outside by an end set back by hand
  is audited (`core/service/ServiceModel.cpp:560`, with empty and unused patterns).
- **Arrangement.** `clip duplicate --count N` makes N copies end to end in one edit
  (`ServiceEdit.cpp:712`). A `clip add` with no `--lane` goes on the lane of its strip's newest clip; a new
  lane only for a strip with none; `--lane new` asks for one (`laneFor`, `:537`) — the GUI's drop below the
  last lane says `--lane new` (`app/widgets/ProjectScreen.cpp:120`), so R-BROWSE-3 holds.
- **Reading back** (read-only: in `mutates()`, `SolarisService.cpp:112` — no undo step, still saved):
  `ls` (`lsText`, `ServiceCompose.cpp:329`), `show <id>` (`showText`, `:404` — a device's non-default
  parameters with unit and default, a formula where one drives it), `pattern print` (`:294`, the notation
  with the pitch's number and name after `#`, a kit's pads by name), `state print --json --compact`
  (`compactJson`, `core/service/AppModelCodec.cpp:414`; `Json::dumpCompact`, `core/service/Json.cpp:118`):
  the song only, one line, a device's non-default parameters as `name=value`, notes in the notation.
- **Honest values.** A device parameter outside its registry range is refused naming the range and unit
  (`SolarisService.cpp:722`), as is an automation point (`inAutoRange`, `core/service/ServiceAuto.cpp:69` — a value within printing's
  rounding of a bound is that bound, so a dragged point at the top edge still lands).
- The API document gains the Notation section and each kit's pads (`core/service/ApiDoc.cpp:145`, `:81`).

Guarded by `solaris_service` (`core/tests/serviceTests.cpp:1782` … `:3260`: the notation tables; comment
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

### DR-SVC-5 The control channel and attach (R-SVC-5, R-SVC-6)
**The window.** `solaris --control <socket> [song.slp]` (`linux_main.cpp:269`) builds a `ControlServer`
on the window's own service after any song opened and polls it from its own 16 ms GLib timeout
(`onControl`, `:172`; `:283`) — not the frame clock, so a minimised window is still drivable. The
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

**attach** (`cli/Attach.cpp:55`, hooked at `cli/main.cpp:95` before any service is built — it builds
none): lines from `--script`, chained ` : ` args, or stdin; sends ONE line, waits for its `[ok]`
(matched by echo) or `[refused]`, then the next (`:170`–`:204`), so a script ends when its last answer
arrives, never on a quiet-period guess. It prints what solaris-cc prints where solaris-cc prints it:
output on stdout, event lines on stderr (always — watching is the point), `refused: <why>` and exit 3
on a refusal (`:228`); `--follow` keeps printing events; `--timeout` (300 s). Empty lines are not sent:
the channel delivers none, so none would be answered (`:199`).

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
`docs/ntwb-api.json` (ctest `solaris_ntwb_api_current`, `cli/CMakeLists.txt:20`); it points to
`docs/API.md` for the grammar instead of copying it.

**`solaris-cc ntwb`** (`cli/NtwbMain.cpp:207`): `api`; `install [--data-dir D] [--web-dir W]` (`:87`)
writes `D/solaris/{ntwb.json, api.json, web/}` (D defaults to `$XDG_DATA_HOME/ntwb/apps`) — the web
UI plus what it borrows, copied not forked: cosmo's `js/core/signal.js` + `dom.js` (`:103`) and cosmo's
typeface (`:109`); the manifest is `single: true` (`:140`) — one engine on the clock device, its pages
sharing the song; `uninstall [--data-dir D]`; `serve` (`:152`) — the service with the window's host
(files, devices, the clock device, the machine's settings and recents), the adapter, and the loop that
owns the clock: `client.poll(8)`, `pump()`, `tick()` (`:195`).

**The page** (`web/`, no build step, MVVM): `js/model/session.js` (NTWB → signals; intents → lines),
`js/vm/song.js` (the shared shapes — the lanes' rows by Timeline's rule (`:53`), clips with their
pattern's notes, mixers with their strips in order, the transport — `view.*` the page's own; every
intent ONE command line, the window's own: `set <ch>.mute=true|false` (`:150`), `transport seek <beat>`
on Timeline's snap step (`js/views/lanes.js:36`), a fader's `set <ch>.gain=<dB, 1 dp>` by
MixerDock's law (`js/views/util.js:19`)), and the views: the song bar (transport, position, bpm,
undo/redo/save/close, the session), Home (recents, new/open by a path on the machine), the lanes
(ruler, rows, clips, playhead — every model value through an eased copy, `Tween`, `lanes.js:19`), the
mixer (pages, strips with meter, fader, gain, pan, mute, solo; the master) and a console (the line in,
its output, refusals and the event stream, the verb's usage from `commands`). The tokens are generated
from Theme.h with the teal installed (`cli/webTokens.cpp:41`, `web/css/tokens.css`, ctest
`solaris_web_tokens_current`, `cli/CMakeLists.txt:31`).

**Proof.** `solaris_ntwb` (`tests/faces/ntwbAdapterTests.cpp`): the real service, adapter and
`ntwb::Client` over `ntwb::MemoryTransport` with a fake host — calls become commands, a refusal is a
failed result with the service's words and one `command.rejected` event, `wait` and an unknown method
are refused, a notify lands, `model` and the pushed state equal `state print --json` but for the
revision, `commands` lists the whole table, the transport has its key, every frame validates
(`:124`); `ntwb install` into a scratch data dir writes the manifest, the API, the page, cosmo's two
files unchanged and the fonts, and `uninstall` removes them (`:207`). Mutant: an adapter that relays
no events fails it. The page was looked at in headless Chrome over a fake bridge fed a real `state
print --json` (desktop, Home, the console mid cross-fade) and its intents checked to send the window's
lines; it has NOT yet been run through a real Arstro Remote.

### DR-MIX-17 Latency compensated (R-MIX-17)
**A device says how late it is** — DSP `Device::latency()` (`src/device/Device.h:125`, REQ-device-8, DSP
`b611e4a`): the samples its output lags its input at its current parameters; the limiter's `EffectDevice`
reports its lookahead (`src/device/DeviceRegistry.cpp:273`, 96 at 2 ms and 48 kHz), every other type 0.

**The engine delays every other path** (classic plugin delay compensation). `Engine::build` calls
`compensate` (`engine/Engine.cpp:244`) once the bindings' values at time zero are in (`:235`), and walks
the graph in processing order (every target is later): a strip's INPUT is aligned to the latest arrival —
its own sound (an instrument's latency), earlier strips' outputs, sends and sidechain keys; its OUTPUT is
that plus its effects' latencies (`stripLatency(i)`, `engine/Engine.h:136`). The master's input is
aligned the same way, its output adds its rack's, and every port and the captured master meet at the
latest of all (`outputLatency()`, `engine/Engine.h:134`). Then each connection — a main output, a send, a
key — carries a `DelayLine` (`engine/Engine.h:58`) of what its target's alignment exceeds it by
(`route`, `:407`, and `routeKey`, `:399`, through `delayed`, `:270`); a strip's own clips or notes go
through one of their own when something later reaches its input (`:456`, `:511`); a bypassed device
runs a delay of its latency in its place (`:523`), so the timing never jumps with a bypass; a key
reaching a compressor behind a latent device in its OWN rack is delayed by those devices too (`:536`); a
silent strip's delayed connections drain what they hold, in time (`drain`, `:549`); the master is
delayed up to the ports' alignment (`:656`). Every delay is sized at build — the audio thread
allocates nothing (the counting `operator new` test now plays two limiters, one bypassed live: 0).
Rendering stays byte-identical however time is chopped (128 vs 77).

**The render starts on beat 0; live hears it later.** `render` renders `outputLatency()` more and drops
it from the start of the mixdown and the ports, a stem its strip's own latency
(`core/service/ServiceRender.cpp:202`, `trim`, `:239`). `Player::heard` subtracts it from the position
(`core/Player.cpp:234`, set at `start` and on every swap, `:269`); the metronome clicks on the music's beat
(`:568`) and the published bound values are the heard block's (`:303`). A live `set` that moves a
device's latency (a lookahead) builds a new engine instead of a message (`latencyOf`,
`core/service/ServiceTransport.cpp:74`, checked at `:513`; the live engine's latencies recorded by
`buildLive`, `:68`, via `Engine::deviceLatency`, `engine/Engine.cpp:466`).

**Stated limits.** Bound values (automation, formulas — R-AUTO-7) are evaluated at the engine's position,
so a value applied AFTER a latent device lands up to that path's latency early (≤ 10 ms, 2 ms by
default); a latency an automation moves while the song plays is compensated at its value at the start
(build time); toggling a latent device's bypass while playing clicks once (≤ its latency) — its own
buffer is not fed while bypassed.

**Measured.** Engine (`test_latency_is_compensated_everywhere_signals_meet`,
`engine/tests/engineTests.cpp:585`): a click+tone on the LEFT through a limiter and its twin on the RIGHT
with nothing meet at a bus — left and right equal sample for sample, the click at 1000 + 96 — by main
output or by post-fader send, the limiter bypassed or not, in chunks of 128 or 77; a master limiter
puts Main and a cue sent straight to Phones at 96 together; a key from a limited strip delays its target's
audio 96 to meet it, and a limiter before the keyed compressor delays the key 96 — the keyed strip's output
exactly the unlimited one 96 later (and it does duck). Mutants: every delay sized 0 → the twins fail;
no key delay in the rack → the key check fails. Service (`test_latency_is_compensated_and_a_render_starts_on_the_beat`,
`core/tests/serviceTests.cpp:4946`): two strips of one tone, a limiter on one — each stem from beat 0 and
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
`lane move <ln> --to <index>` (`core/service/Command.cpp:117`; `case K::LaneMove`,
`core/service/ServiceEdit.cpp:553`) puts the lane at row `index` (0 = the top) and renumbers every lane
0…n−1 in the new order — one edit, one undo step; an index past the last row or an unknown lane is
refused. The lanes draw it through Interstellar's `AnimatedRows`, keyed by lane, so the rows slide
(DR-UI-3). Guarded by `test_lanes_are_reordered` (`core/tests/serviceTests.cpp:5057`: to the top and back,
renumbered, refused out of range, undone) and, drawn, by `test_instrument_drop_and_clip_drag`
(`app/tests/ui/uiTests.cpp:492`: a lane moved from a shell slides to the top with its clip, caught mid-tween).

### DR-TIME-6 The ruler dragged (R-TIME-6)
`Timeline::rulerGesture` (`app/widgets/Timeline.cpp:765`, called first in `handleGesture`, `:860`) takes a
press on the ruler without Shift and waits: a click is still the seek (R-TIME-5) or clears the loop
(R-EDM-7). A drag decides ONCE, by where it began: within `kBraceGrip` (6.5 px) of an end of the loop's
brace, or on its body, in the ruler's lower half (`:785`), it moves the loop; anywhere else it scrubs.
Scrubbing, the playhead is the pointer's on the snap step (`advance`, `:465`, overrides both the eased
seek and following the audio while it is held) and a `transport seek` goes at each new line — a fader
sends each step the same way; a seek is transport, not an edit, so no undo step and the song stays
saved — and once more on release if the last line was not sent; the playhead is `set` there, so
nothing eases back. The brace is the pointer's while held (`loopSpan`, `loopRect`), moved with its length
kept or resized never shorter than one step, and ONE `transport loop <a> <b>` goes on release (`:846`),
the brace placed where it was let go so the model's echo moves nothing.

Guarded by `test_the_ruler_is_dragged` (`app/tests/ui/uiTests.cpp:1676`): held, the playhead equals the
snapped pointer beat exactly and four or more seeks were sent; let go, it stays; PLAYING, it stays the
pointer's frame after frame and the song plays on from there; the brace moved 4–8 → 6–10, nothing sent
until release, then one line; resized by its right end, clamped at its left; a click still seeks. Mutants
seen red, then restored: the playhead following the audio while scrubbing, a seek only on release, a body
drag keeping the brace's end. Shots `ruler-scrubbing` and `loop-brace-moving`
(`app/tests/shots/renderShots.cpp:527`, `:492`), both sizes, looked at.

### DR-TIME-7 Stop takes the time bar back to where it was put — or leaves it (R-TIME-7, R-TIME-8)
The service keeps a CUE (`mCue`): where the user PUT the time bar. A `transport seek` puts it where the time bar went
(`core/service/ServiceTransport.cpp:634` — a ruler click or drag, the start key), and a play that says where it
begins puts it there — `--from`, a roll's `--clip` (`:487`); a plain `transport play` starts wherever the time bar
stands and leaves the cue alone. `transport stop --return` stops and moves the time bar back to the cue (`:613`); a
plain `transport stop` leaves it where it was heard (R-TIME-4) — a TEMPORARY place: the next play starts there, and
a stop with the return after it still goes back to the cue (R-TIME-8, amended 2026-10-16). A new or opened song
starts it at 0. Published as `transport.cue`. In the window: the play/stop key (Space) while playing is `transport
stop --return`, the stop-here key (Ctrl+Space) is `transport stop`, either one while stopped plays (`App::songAction`,
`app/App.cpp:393`, the keys being the keymap's — DR-SET-4); the song bar's Stop and a roll's ■ return too (`:54`).
On the ruler an outlined mark stands at the cue while playing — where Stop takes the time bar back — fading in
with the play and out with it (`Timeline`, `app/widgets/Timeline.cpp:1447`, eased at `:474`), beside the
playhead's filled one. L2 `test_stop_returns_to_the_cue_or_stays`; UI `test_space_returns_and_ctrl_space_stays`
(Ctrl+Space, then Space plays on from there, then Space's stop is back at the cue); shot `ruler-cue`. Mutants: a
stop that ignores `--return`, a mark that snaps, every play moving the cue — all seen red.

### DR-LANE-3 Instrument tracks (R-LANE-3; R-LANE-1 amended)
**The model.** `Lane::track` (`model/Project.h`) names the instrument strip a lane is the TRACK of; the
file writes it `#alane … track=ch_2` (the suite's word for a strip, as a clip's `track=`), and every
save writes the header line `tracks = on` (`model/Project.cpp:769`; read at `:601`, any other value
refused). `validateProject` refuses a track of no strip or of a non-instrument (`:1036`) and a clip on a
track that does not play through it — audio named as audio (`:1093`). `laneTitle` (`:49`) is the name a
lane shows: its own, else a track's strip's — so renaming the instrument renames its tracks (D-2's
lane half).

**The service.** A lane made for an instrument is its track with no name of its own (`newLane`,
`core/service/ServiceEdit.cpp:591`); an instrument's clip with no `--lane` goes on its track (`laneFor`,
`:616`; `trackOf`, `:605` — the track its newest clip is on, else its newest, else a new one), and only
an instrument with no track and clips on plain lanes keeps R-SVC-8's rule. `clip add --lane <a track>`
(`:641`) implies the strip and places `--pattern` there; audio, `--instrument` and a different `--strip`
are refused there by name. `clip move` (`:779`): onto a track a note clip is re-routed in the same
edit (one undo step) and the output says `<ac> plays through <ch> (<name>)`; `--strip <other>` on a
track moves it to that one's track; audio and `--lane` with a different `--strip` are refused. `lane add
--strip <ch>` (`:526`); `set <ln>.strip=<ch>|none` (`core/service/SolarisService.cpp:769` — refused while
the lane holds another strip's or an audio clip; `none` keeps the name it showed). `strip relink` takes
the strip's tracks with its clips (`ServiceEdit.cpp:324`); `strip delete` leaves them plain lanes named
as they were (`:267`). A song saved before tracks (no `tracks` line) is adopted on opening
(`adoptTracks`, `SolarisService.cpp:75`, called at `:453`): a lane whose clips all play through one
instrument becomes its track, a name it was made with (its strip's, or its instrument's registry
label) cleared so it shows the strip's, each said as an `info` event; opening stays clean (not dirty).
Published: `lanes[].name` (what it shows), `lanes[].strip`, `lanes[].ownName`
(`core/service/ServiceModel.cpp:265`); `ls` marks `· track of <ch>` (`core/service/ServiceCompose.cpp:668`).

**The view.** A track's row takes its strip's colour unless it has its own and names its instrument
under its name (`Timeline::bind`, `app/widgets/Timeline.cpp:169`), that line fading in and out as the
lane becomes and stops being a track (`advance`, `:491`). A lane header's right-click (`:989`): Track
of ▸ (the instrument strips) → `set <ln>.strip=<ch>`, Plain Lane → `…=none`, Copy ID. A sample or an
instrument dropped from the browser onto a track asks for a lane of its own (`--lane new`,
`app/widgets/ProjectScreen.cpp:120`) — it could not play through another's instrument.

Guarded by `solaris_model` (`model/tests/modelTests.cpp:369`: the four refusals; `tracks` read, refused
other than `on`, written in the canonical text), `solaris_service` (`test_instrument_tracks`,
`core/tests/serviceTests.cpp:2076`; `test_a_song_from_before_tracks_adopts_them`, `:3220`) and
`solaris_app_ui` (`test_a_lane_is_an_instruments_track`, `app/tests/ui/uiTests.cpp:236`: a rename
followed, a clip dragged onto another track re-routed with its colour caught mid-ease, Track of ▸ and
Plain Lane caught mid-fade, a browser drop onto a track → `--lane new`). Mutants seen red, then
restored: no re-route onto a track, no adoption, a track not showing its strip's name, the line placed
instead of faded, a drop onto a track not redirected.

### DR-CLIP-6 MIDI clips made on a track, looped by their end, moved between tracks (R-CLIP-6, R-CLIP-7, R-CLIP-8)
**Made** (R-CLIP-6): a double-click on an instrument's track where no clip is (`app/widgets/Timeline.cpp:981`)
or its right-click menu's New MIDI Clip (`:1012`) sends ONE `clip add --lane <ln> --at <the snap step>
--length <a bar>` (`newClipOn`, `:754`) — the track's instrument implied (DR-LANE-3), a new empty pattern
made; a double-click on the clip opens its piano roll as before (R-ROLL-1). **Looped** (R-CLIP-7): a press
within `kEdgeGrip` (6.5 px) of a clip's right edge takes its END (`:878`); dragged, its length is the
pointer's on the grid you see, never shorter than a step (`:900`); let go, it stays where it was let go
and ONE `set <ac>.length=<beats>` is sent (`:918`). A clip's drawn length is eased like its place (`len`,
`:604`; `liveLength`, `:344`), so a length set from a shell or an undo grows or shrinks it rather than
cutting it; the pattern's notes repeat inside it with a seam at each repeat (`paintClip`, `:1119`, from
the live length). **Moved** (R-CLIP-8): the clip drag's `clip move --lane` onto another instrument's track
re-routes it (DR-LANE-3); audio is refused there and eases home.

Guarded by `test_midi_clips_are_made_looped_moved_and_listed` (`app/tests/ui/uiTests.cpp:305`): a
double-click on the Bass track → the exact line and a clip of a new pattern through the bass; the empty
track's menu; the end dragged → the snapped length while held with nothing sent, one line on release,
no ease back; a shell's `set ac_1.length=6` caught mid-ease; a clip dragged onto the drums' track plays
through the drums. Mutants seen red, then restored: a length that snaps, an edge never taken. Shot
`clip-end-dragging` (`app/tests/shots/renderShots.cpp:558`), both sizes, looked at.

### DR-CLIP-9 A clip copied: Shift-drag, Ctrl+C / Ctrl+V (R-CLIP-9, R-CLIP-10)
`clip duplicate <ac> [--at <b>] [--count <n>] [--lane <ln>]` (`core/service/ServiceEdit.cpp:813`): with `--lane` the
copies go to that lane under the move's rule (R-CLIP-8) — onto an instrument's track a MIDI copy plays through that
instrument (the pattern still shared, R-CLIP-3; "it plays through ch_3" said), an audio copy is refused there, a plain
lane (or `none`) keeps its strip. In the window, the lanes' clip drag reads Shift at every step of the drag
(`Timeline`, `app/widgets/Timeline.cpp:947`) and as it is let go: held, the drop is ONE `clip duplicate <ac> --at <b>
[--lane <ln>]` and the original stays where it is (`:993`); not held, it is the `clip move` it always was. While held
`mCopyAmt` eases in (120 ms): the original, dimmed during a move, comes back to full strength, and a "+" in the accent
fades onto the copy in hand (`:1405`) — pressed or let go mid-drag, it eases back. `expectCopy` (`:434`) makes the next
clip the model brings the selection (`bind`, `:220`) and — after a drop — places it at full strength where the
pointer left it, never fading in what was already shown; a refused copy brings none and the expectation lapses.
Ctrl+C and Ctrl+V are the keymap's `lanes.copy` / `lanes.paste` (DR-SET-4) and the Edit menu's Copy Clip / Paste Clip:
copy keeps the selected clip's id in the window (nothing sent; a toast says it, naming the paste key); paste
(`App::pasteClip`, `app/App.cpp:456`) is ONE `clip duplicate <copied> --at <the time bar>` on its own lane, the copy
selected — and with the time bar unmoved since the last paste, right after that copy, so pastes run end to end. A copied
clip since deleted is said, not pasted. While a piano roll has the keys its Ctrl+C / Ctrl+V are the notes' even with
nothing to copy (`:451`), never a clip's. L2 `test_a_clip_copied_onto_another_lane`; UI
`test_clips_are_copied_by_shift_drag_and_ctrl_c_v` (the copy look caught mid-ease; the copy at full strength and
selected; onto another lane; a plain drag still moves; pastes at the time bar, end to end, then where it moved; a
roll's Ctrl+V sends nothing); shot `clip-copy-dragging`. Mutants: Shift ignored at the drop, the copy look snapping,
pastes all at the time bar, the dropped copy fading in — all seen red.

### DR-BROWSE-4 The song's MIDI list (R-BROWSE-4)
The Song tab lists a MIDI header, **New MIDI**, then every pattern, then the samples under SAMPLES
(`Browser::rebuild`, `app/widgets/Browser.cpp:192`) — keyed rows that grow in and fade out. A pattern's row
reads its name and, in mono at the right, its length (bars when whole bars), its note count and the
strips that play it, or "unplayed" (`bind`, `:94`, from the model's `patterns[].strips`, published with
`patterns[].lastStrip` — the strip its newest clip plays through — by `core/service/ServiceModel.cpp:348`).
New MIDI sends `pattern new --name "MIDI <n>"` (`:382`); a double-click opens the pattern's piano roll
(`:389`); its menu (`:395`): Rename… (`set <pt>.name=`), Piano Roll, Duplicate, Copy ID, Delete (the
service refuses while a clip plays it, naming them). Dragged onto the lanes (`ProjectScreen::place`,
`app/widgets/ProjectScreen.cpp:127`): onto an instrument's track → `clip add --pattern <pt> --lane <ln>
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
mapping, which the service now uses too, `core/service/ServiceModel.cpp:60`), the panel's song controls
off (`DevicePanel::songControls`, `app/widgets/DevicePanel.h:95`: the header names the instrument with the
suite's mark and teal dot instead, `app/widgets/DevicePanel.cpp:628`). Solaris's accent and embedded fonts
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
`app/widgets/DevicePanel.h:98`: room under the title, the rows below it; opt-in, 0 in Solaris), five to a
row (`:264`): a pad per key the registry names, in its order, each joined to its parameters by the
registry's `notePrefixes` (`:149` — the join D-3 lacked). A press plays the pad through the plugin —
`ParamAccess::play`, the controller's `playNote`, an `arstro.note` message to the processor, heard at the
next block — and picks it; the release is its note-off (`handleGesture`, `:199`). The pick eases its ring in
and the hit flashes and dies away like the sound (`advance`, `:174`); the panel scrolls, eased, so the pad's
group begins at the top (`DevicePanel::revealGroup`, `app/widgets/DevicePanel.cpp:428`, called at `:33`).
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
`app/widgets/DevicePanel.h:88`), so the same face is Solaris's window and the plugins' editor. Still generated
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
Tokens: `surface::moduleBg`, `screenBg`, `knobCap`, `knobTrack`, `screenGrid` (`app/Theme.h:82`).

**Motion and gestures** (`advance`, `:272`; `handleGesture`, `:429`). A value set anywhere turns its knob there
(220 ms) and every screen redraws through the turning knob; a switch's light slides (220 ms); the last change's
halo fades in and out. A drag is direct manipulation — up turns it up, Shift five times finer (`:455`), the knob
the pointer's — and each step that moved the value is ONE `set` through the panel; a bound knob takes no drag
(`:439`); a double-click is the default; a right-click opens the parameter's menu (`DevicePanel::openParamMenuFor`,
`app/widgets/DevicePanel.cpp:515`, R-WIN-3); the wheel scrolls. Show IDs puts each address over its label (`:742`).
An instrument's window opens 820 × 580 (`app/widgets/FloatWindow.cpp:144`), three modules a row; an effect's
keeps the list at 390 × 520. The plugins' editor opens at the face's own size — 980 wide, as tall as its modules
(`InstrumentEditor::preferredSize`, `plugins/InstrumentEditor.cpp:40`, asked by `plugins/X11View.cpp:64`).

**The filter screen found a defect** (D-4): measured against the real `StateVariableFilter`, every resonance
peaked at −3.01 dB — the synth's Resonance had never done anything (`tick` read a smoothed value only the block
path advances). Fixed in the DSP library (`66b5ecb`, REQ-svf-2 amended); the screen and the filter now agree to
0.000 dB over 4 modes × 3 resonances × 5 frequencies.

Guarded by `solaris_app_ui` (`test_an_instruments_face_is_an_analog_panel`, `app/tests/ui/uiTests.cpp:421`: six
titled modules, a window ≥ 800 wide; a shell's change caught mid-turn and lit; a key's light caught mid-slide;
a double-click's default; the menu's Create Automation, after which the knob names `au_1` and takes no drag; the
flow at 390 and 980; an effect's list) and `solaris_plugin_editor` (`plugins/tests/editorTests.cpp:164`: the
modules, screens and rows at 980, one a row at the minimum; `:200` a knob dragged up → beginEdit once, performEdit
= the shared mapping of each value, strictly rising, endEdit, Shift 5× finer, a key's choice; `:257` the host's
change caught mid-turn and a choice's light mid-slide; `:285` the filter screen against the filter). Mutants seen
red, then restored: a knob that snaps, a light that snaps, a bound knob that drags, the screen's Q drifting.
Shots `device-panel` (the face), `device-face-turning` (mid-turn and mid-morph), `effect-window`
(`app/tests/shots/renderShots.cpp:288`, `:299`), and the plugin's `plugin-editor-synth.png` — looked at.

### DR-UI-13 The song's tempo is set in the song bar (R-UI-13)
The song bar's "<bpm> bpm" is a control (`SongBar`, `app/widgets/SongBar.cpp`; its hit rect `kBpm`, `:68`, placed
after the position by JetBrains Mono's fixed 0.6 em advance, so it needs no render target). A press takes it;
dragged up or down it is the POINTER's — a bpm per `kBpmStepPx` (3.25 px) of vertical travel, a tenth with
Shift, 20…999 — and every step that moved it sends ONE `set project.bpm=<n>` (`SongBar::handleGesture`, `:126`;
the recognizer reports a drag past its slop, so the first line may cover two steps). A double-click opens the
App's text field with the tempo, and what is typed is one `set project.bpm=` — the service refuses what is not a
tempo, said in its toast (`:164`). It is lit while held, washed on hover, and a tempo set from elsewhere is
drawn easing there over 220 ms (`kCatchUpMs`) — `bpmShown()` is the LIVE value (`:243`). Closes the UX audit's
U-1. UI test `test_the_tempo_is_set_in_the_song_bar` (mutant: a tempo that snaps, seen red).

### DR-UI-14 The pointer says a drag resizes (R-UI-14)
`Cursor` (`app/Cursor.h`): Default, ResizeH ↔, ResizeV ↕, ResizeDiag ⤡, each with its CSS name. Every widget with a
grip answers twice, from the SAME zone its press takes: `dragCursor()` — the cursor a resize in hand keeps, wherever the
pointer is — and `cursorAt(local)`. `Timeline` (`app/widgets/Timeline.cpp:434`): ↔ over a clip's end (`kEdgeGrip`,
R-CLIP-7) and the loop brace's ends (`kBraceGrip`, the ruler's lower half), and while either is held (pressed, not yet
moved, included). `PianoRoll` (`app/widgets/PianoRoll.cpp:578`): ↔ a note's end (the zone `noteAt` gives, D-7) and the
pattern's end handle, ↕ a velocity bar, and while any is held. `MixerDock` (`app/widgets/MixerDock.cpp:847`): ↕ its top
edge (`Part::Resize`). `WindowLayer` (`app/widgets/FloatWindow.cpp:304`): ⤡ a window's corner (`FloatWindow::resizing`
while it is held); the front-most window under the point decides, so it covers the lanes beneath. `ProjectScreen::
cursorAt` (`app/widgets/ProjectScreen.cpp:20`) asks every drag in hand first, then the window, the dock, the lanes;
`App::cursor` (`app/App.cpp:427`) gives the arrow while a modal is open (a menu, a dialog, the settings, the history
tree) or off the song, else the screen's answer at the last pointer position (`App::pointer` keeps it). The GTK host
asks after every pointer event and, when it changed, sets the window's cursor by name (`showCursor`,
`linux_main.cpp:98`) — the OS's own cursors, which do not ease. UI `test_the_pointer_says_a_drag_resizes` (every grip;
a clip's end held off the edge; a window over the lanes; a modal). Mutants: no clip-end zone, a resize in hand
forgetting its cursor, a modal not the arrow — all seen red. The GTK half is compiled and read, not driven live (D-5).

### DR-SAFE-1 Autosave and recovery (R-SAFE-1, R-SAFE-2)
While a song has unsaved edits the service writes it beside itself, `<song>.slp.autosave` — the `.slp` text exactly as a
save would write it — through a temporary file renamed over the last one, so a crash mid-write keeps the previous
autosave (`SolarisService::autosave`, `core/service/SolarisService.cpp:629`). When: once `settings.autosave` seconds
(default 60; `settings set autosave=<1…3600|off>`) have passed since the song opened or was last autosaved — checked by
`pump`, so an idle window still writes (`core/service/ServiceTransport.cpp:127`) — and AT ONCE after an edit whose verb
deletes or removes something (`dispatch`, `core/service/SolarisService.cpp:367`). Each write is announced (`project.autosaved path=`). A save drops it
(also the old place's, when saved elsewhere); so does a close — closing is choosing. A session that dies leaves it.
`recoveryOf` (`:503`) finds an autosave NEWER than its song: Home's card says "unsaved changes kept" (`recents[].recovery`);
`project open` opens the SAVED song and says `recovery: …` (`:629`), emits `project.recovery path= age=`, and the model's
`recovery` / `recoveryAge` name it until it is answered: `project recover <path>` (`:635`) opens the autosave as the song
— at `<path>`, unsaved, its history rooted "Recovered" — and `project discard-recovery <path>` deletes it. The window asks
once per offer (`App::askRecovery`, `app/App.cpp:344`: Recover · Open the Saved One); Settings › Interface has an
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
and a clip of it is made by `clip add`'s own rules (`:116`) — so a track's instrument plays it, or a new strip is made.
`pattern export` and `project export-midi` (`:129`) write a pattern, or the song as it plays (a track per instrument
strip, each note clip's pattern at its place, repeated to fill it and cut at its end; a kit on channel 10). The
browser lists `.mid` files (the host classes them `midi`) and a drop is ONE line (`ProjectScreen::place`,
`app/widgets/ProjectScreen.cpp:135`): on a track `pattern import <file> --lane <ln> --at <b>`, elsewhere `--instrument
synth`. L2 `test_midi_files` (a pattern's round trip to the tick; a hand-made file with running status, velocity-0
offs, 96 ticks a beat, named tracks; SMPTE time through its tempo; a drop on a track merged, one undo; a new
instrument; the song exported as it plays; refusals); UI `test_a_midi_file_dropped_on_a_track`. Mutants: velocity 0
not an off, running status unread — both seen red.

### DR-METER-1 The master's loudness (R-METER-1)
The engine writes the master as it leaves — after its rack, gain and latency delay, exactly what `capturedMaster`
holds — into buffers the CALLER owns (`Engine::tapMaster`, `engine/Engine.h:130`; written in `renderPiece`,
`engine/Engine.cpp:831`), so the tap never allocates. The player owns those buffers and the DSP library's meters
(`LoudnessMeter`, `TruePeakMeter`, `CorrelationMeter`, `SpectrumAnalyser` — REQ-meter-1…4), built in `start` after the
engine's build set the rate; after every block `Player::measure` (`core/Player.cpp:365`) runs the tap through them and
stores momentary, short-term, integrated (re-read only when a gating block was added), true peak (the most since play
or a clear), correlation and 61 bands into atomics. `pump` publishes them as `loudness` (`core/service/ServiceTransport.cpp:256`)
— not in the stable dump, −144 for nothing measured, KEPT after a stop so a played song's integrated loudness can be
read, reset by another song (`core/service/SolarisService.cpp:361`). `render --measure` runs the trimmed mixdown through
the same meters 100 ms at a time and prints ONE line, `measure integrated= momentaryMax= shortTermMax= truePeak=
correlationMin=` (LUFS, LUFS, LUFS, dBTP, −1…+1; the lowest correlation only where the momentary is above −70 LUFS —
silence has no phase; `core/service/ServiceRender.cpp:50`). The window: the master's card has a chip with the
integrated LUFS where a strip has mute and solo (`MixerDock::loudnessRect`; caught up over 220 ms, "— LUFS" cross-faded
in for none — `app/widgets/MixerDock.cpp:707`); a click opens the Loudness window (`WindowLayer::openLoudness`,
`app/widgets/FloatWindow.cpp:274`), a `LoudnessPanel` (`app/widgets/LoudnessPanel.cpp`): the five readings (each
catching up over 220 ms; nothing measured cross-fades to "—"), the correlation's needle, the 61 bands on −72…0 dB
(rising in 40 ms, falling in 300) and Reset — `meters clear`. L2 `test_meters_measure_what_plays` (a −6 dBFS 1 kHz
sine: −6.02 LUFS momentary, short-term and integrated, −6.02 dBTP, correlation 1, band 34 at −6, two octaves down 20 dB
less), `test_render_measure`; UI `test_meters_and_the_loudness_window`; shots `meters-playing`, `loudness-window`.
Mutants: the tap's right channel silent (−9.02 LUFS), the clear not resetting the loudness, the readings snapping to
"—" — each seen red.

### DR-METER-2 Strip meters: RMS, held peak, clip latch (R-METER-2)
For every strip and the master, after every block (`Player::ballistics`, `core/Player.cpp:352`): the RMS through a
300 ms one-pole on the blocks' mean squares (the engine's per-strip sums; the master's from the tap), and a held peak
— the most of the last 1.5 s, then falling 20 dB a second — in SAMPLES, so it is the same however the window draws.
A block at or over full scale sets the strip's clip flag; `pump` takes the flags (exchange) into a latch the SERVICE
keeps (`mClipLatch`, published by `refreshModel`, `core/service/ServiceModel.cpp:246`), so a clip light outlives a
stop and an edit. Published: `strips[].rms/.hold/.clip`, `transport.masterRms/.masterHold/.masterClip` (not stable);
a stop zeroes peaks, RMS and holds and leaves the lights lit. `meters clear` (`core/service/ServiceTransport.cpp:576`,
not an edit, `meters.cleared`) puts every light out, zeroes the holds and starts the true peak and integrated loudness
again: on the audio thread it is a `Live::ClearMeters` MESSAGE (`core/Player.cpp:261`), so it lands in order after a
gain edit sent before it; until the thread has taken it (`Player::clearing`) `pump` reads no holds, flags or loudness,
which are from before it. The window (`MixerDock::paintFader`, `app/widgets/MixerDock.cpp:1324`): the peak a lighter bar,
the RMS its solid core, the held peak a 1.5 px line in its zone's colour, all eased (40 ms up, 300 ms down); above the
meters a clip light lit red, eased, whose click is `meters clear` (`:1109`). L2 `test_meters_measure_what_plays` (RMS
0.354 and a held 0.5 for a 0.5 sine; +12 dB lights both latches; a clear while playing allocates nothing on the audio
thread; latched through a stop and an edit), `test_held_peak_holds_then_falls`; UI `test_meters_and_the_loudness_window`.
Mutants: no hold (the held line = the peak), a latch lost on refresh, the clip light snapping — each seen red. The
pending-clear guard's race was not reproduced by its mutant in three runs; it is kept for the window it closes.

### DR-EXPORT-1 Delivery formats (R-EXPORT-1)
`render` (`core/service/ServiceRender.cpp:73`) takes the format from `--format`, else the file's extension (`:78`):
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
and a Stems toggle; a row the format does not take greys out, eased, and takes no input (`applies`, `:96`); a
choice the new format cannot take moves (`formatChanged`, `:84`); Export… hands the host's save dialog the
name with the format's extension and sends ONE `render --out <file> --format …` (`flags`, `:113`; `App`,
`app/App.cpp:92`). L2 `test_delivery_formats` (each format and its defaults by the extension or --format; 2 s at
44.1 kHz is 88200 frames and the tone still 1 kHz; 16 bits on the grid, dithered, the same bytes twice; −20 LUFS
by the meter; held at −2 dBTP; ten refusals); L3: the CLI wrote FLAC 24 / 48 kHz, MP3 44.1 kHz 192 kbps, Vorbis,
Opus 48 kHz and WAV 16 that ffprobe reads as such, FFmpeg's own ebur128 reading the −14 LUFS FLAC at −14.0 and the
44.1 kHz MP3 in tune; UI `test_the_export_sheet_is_one_render_line`; shot `export-sheet`. Mutants: no dither, the
rate not rendered at, the ceiling ignored, the greying snapping, greyed rows taking input — each seen red.

### DR-MIX-18 A side-chain in one line (R-MIX-18)
`strip duck <ch> --under <key> [--amount <dB>]` (`core/service/ServiceEdit.cpp:367`) is made of the commands that make each
part, so each one's rules hold: `device add <ch> --type compressor` (at the end of the rack), then `set` on it —
threshold −30 dB, the ratio R = 30 / (30 − amount) at which a full-scale key cuts the amount (default 8, 1…24), attack
1 ms, release half a beat at the song's tempo, Sidechain on — then `send add <key> --to <ch> --pre --sidechain` (a
pre-fader key: the kick's fader does not move the duck). It is ONE edit: one undo takes both back; a key that would go
back in processing order is refused by `send add`'s rule (R-MIX-4) and the whole line leaves nothing; a pair already
ducked is refused naming its send. It prints the compressor, then `made: send=…`. A real kick peaks under full scale
and cuts less than the amount: the default Drum Machine kick, 12 dB asked, takes the bass 7.3 dB down just after it,
back within 2 dB before the next beat at 120 bpm. The window: a strip's right-click menu has Duck Under ▸ listing every
strip whose published `keyTargets` include it — the strips that may key it — each ONE line (`app/widgets/MixerDock.cpp:1245`).
L2 `test_strip_duck_is_one_line` (the settings, the key, the duck measured on the bass's stem, one undo, six
refusals); UI `test_a_strip_ducks_under_another_from_its_menu`. Mutants: Sidechain left off, the send not a key,
the submenu's strips from the wrong side of `keyTargets` — each seen red.

### DR-DEV-9 A PipeWire driver; xruns counted (R-DEV-9, R-DEV-5)
`settings set driver=pulse|pipewire` (default pulse; saved with the machine's settings; Settings › Audio › Driver).
Every stream the service opens — the player, the browser's audition, the roll's note preview — goes through
`openOutput` (`core/service/ServiceTransport.cpp:98`): with `pipewire`, the host's `audioOutFor("pipewire")`; when the
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
**Which.** At play (`core/service/ServiceTransport.cpp:499`) each output port goes where this machine's map puts it:
the clock (`settings.output`), or another device — a FOLLOWER, opened through the clock's driver with the channels
its ports need; one that will not open (`:533`) is silent, said by an Info line ("the device … did not open (why) —
its ports are silent") and shown in the model; the song plays on. **Together** (R-DEV-4; `:543`): each path's delay
from a frame rendered to its being heard is its device's latency as opened — plus, for a follower, its fill target
over the rate (the least: `Follower::minTarget`, `core/Follower.h:73`, the lookahead and two of each block). The
slowest path sets when; the clock is delayed to it by a delay line on its channels (`Player::delayClock`,
`core/Player.cpp:201`; `setClockDelay`, `core/service/ServiceTransport.cpp:561` — the heard position counts it), and
each follower's target is the rest (`:557`), its drift as last measured kept for the next play. **Feeding** (the audio
thread): after each render `feedFollowers` (`core/Player.cpp:178`, called `:571`; silence through a count-in, `:531`)
mixes each follower's ports at their channels and `push`es them (`core/Follower.cpp:60`): an SPSC ring sized at
start, no lock, no allocation; a full ring drops the block, counted; each push time-stamped under a seqlock. **Held**
(`pullOnce`, `:82`, the follower's own thread, paced by its device's blocking write): the fill is CONTINUOUS — up to
the last push's start, plus the frames the clock has made since by its time stamp, less what was taken, plus what the
resampler holds (REQ-resample-2 (A5)) — and steers the DSP library's `FillController` (REQ-resample-3), whose step
the `AdaptiveResampler` reads at. Primed (`:107`): silence until the fill reaches the target, what came past it
skipped to the nearest frame; starved (`:143`, the clock stalled) or far ahead (`:124`, its device stalled): silence
and primed again. Its state for the model: drift, smoothed fill, the target as ramped (`:138`, an atomic — the
controller is its thread's), device latency read after each write. **Seen** (R-DEV-5; `pump`,
`core/service/ServiceTransport.cpp:186`): `transport.devices[]` while playing — the clock, each follower (latency,
drift, xruns, starved, dropped), each that would not open (`:211`, its problem); none stopped
(`core/service/AppModel.h:351`). Tests: L2 `test_a_follower_alone` (`core/tests/serviceTests.cpp:4296`: in simulated
time, a clock 200 ppm slow and unlike blocks — an impulse out exactly its target after it was made, within a frame;
a stalled clock: starved, primed again, on time; a stalled device: pushes dropped, on time again at once);
`test_a_port_on_another_device_follows_the_clock` (`:4396`: devices paced in REAL time, a follower 300 ppm fast — its
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
(`core/service/ServiceTransport.cpp:199`; `Player::driftInput`, `core/Player.h:145`). Tests: L2
`test_an_input_held_alone` (`core/tests/serviceTests.cpp:4531`: in simulated time, a capture 200 ppm fast landing
256-frame blocks read 128 at a time — an impulse read exactly its target after it landed, within a frame; its device
stalled: starved, primed again, on time; the clock stalled: emptied, primed again, on time);
`test_an_input_on_another_device_follows_the_clock` (`:4617`: a capture paced in REAL time 500 ppm fast, monitored
through an armed line — the tone on the clock continuous and at its level; its latency its device's and its target's;
its drift read the right way; the Settings input read as it is). Mutants (each red): never held; the Settings input
held too; no priming skip; the time since a landing ignored; the resampler's hold uncounted; a starve not priming
again; far ahead left alone; the step the wrong way; the latency without the target; the drift the wrong sign; the
start drift ignored. Found while building it: the device's latency was first taken AFTER the read had emptied the
ring it subtracted — 3 ms short, measured. Not yet: a recording through a held input measured against a cable (its
alignment is by its latency, as a default input's is).

### DR-DEV-5 Each port on its device, seen in Settings › Audio (R-DEV-5, R-DEV-1, R-DEV-3)
**The rows** (`bind`, `app/widgets/SettingsSheet.cpp:289`): under the Audio page's settings, the open song's PORTS,
keyed by name — each with the device this machine's map puts it on (`settings.ports`; "Clock device" / "Default input"
when none) and a line under its name: stopped, where it plays (on the clock, or on a device from a channel, held to
the clock; an input beside the clock or held to it); playing (`:325`), its device as `transport.devices[]` has it —
"the clock · 21.7 ms · 0 xruns", "held to the clock · 41.6 ms · +62.50 ppm · …", or "silent — why". **Eased**
(`advance`, `:724`): a row grows in and shrinks out; a line that changes cross-fades, the old out, the new in.
**Drawn** (`paintPorts`, `:1134`): a "PORTS" micro-label, each row's name and direction, its line, and a field drawn as
a combo's (SettingCombo) naming the device; the page's content grows by them (it scrolls to them). **A pick**
(`:892`, `openPortMenu` `:785`): a click on the field opens the app's context menu over the sheet
(`app/App.cpp:90`) — the clock (or the default input), then every device of the port's direction, the current one
marked — and a pick is ONE line, `settings set port.<name>=<device>:<channel>` (or empty: back on the clock). Test:
UI `test_settings_puts_a_port_on_a_device` (`app/tests/ui/uiTests.cpp:4519`: Phones' field lists the clock and the
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
`core/Player.cpp:410`): a note-on or -off into the armed strip's instrument through the engine's live ring
(`Engine::liveNote`, `engine/Engine.cpp:423`: 63 waiting, applied at the next piece's start), the sustain pedal
(CC 64) holding a released note until it comes up (`SustainPedal`, a bitset; a held pitch struck again is let go
first), all-notes-off obeyed, the notes still held let go when the armed strip changes. `strip arm <ch>|none`
(`core/service/ServiceInput.cpp`) is the session's, never the song's — not an edit, `strip.armed strip=` — and
refuses a bus or an audio line. While the song plays its player drains the queue; while it is stopped an INPUT
MONITOR does (`startMonitor`, `ServiceInput.cpp:286`): a player of its own over the song with no clips and no automation, so the
strip sounds through its rack, its sends and the master as it will in the song; rebuilt after an edit, stopped
before the song plays and started after it stops (one consumer at a time; the queue drained in between). `midi
devices` lists the inputs (Settings' Audio page: MIDI input), `midiMessages` counts what came. The window: an
instrument track's header has an arm button, a ring lit red as it is armed, eased (`armRect`,
`app/widgets/Timeline.cpp:326`). Live: `solaris_midi_tests` plays into the kernel's Midi Through from a second
ALSA client — a note on, the pedal, its off and a centred bend arrive as their bytes. L2
`test_a_keyboard_plays_the_armed_strip` (silent until struck; struck, it sounds through the monitor and through the
song from beat 4; let go, it dies away; under the pedal it rings on until the pedal comes up; no allocation on the
audio thread; refusals; not an edit; eight messages counted); UI `test_an_instrument_track_is_armed_from_its_header`.
Mutants: the pedal ignored, the song's player not handed the keyboard (first SURVIVED: the test played from beat 0
and the tone clip masked the key — it plays from beat 4 now, silent until struck), the arm light snapping — red.
**Recording.** `transport record [--from b] [--count-in bars] [--overdub]` (`core/service/ServiceTransport.cpp:419`) is a
play that keeps the armed strip's notes: the player stamps each where it was HEARD (`Player::heard` — rendered minus
the device's and the engine's latency) into a lock-free queue the service empties as it pumps. A count-in (the flag,
else `settings countIn`, 0…4 bars) is a pre-roll of silence clicked on the song's beats with the metronome forced on,
the engine waiting (`core/Player.cpp:525`), aligned as playing is; while it runs `heard` counts back to the start, so
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
(`core/tests/serviceTests.cpp:4669`: dropped as one line, `get` its table, a render that sounds, a square cycle given as
its sample changing that render, the song keeping `osc1.table=sample`). Shot: wavetable-window. Found while building
it: two tests here held the registry's size and its instruments' names as constants — the device-type count now
reads the registry; the refusal's list names the five instruments.

### DR-INST-8 The FM synth (R-INST-8)
**The sound** is the DSP library's (REQ-fm-1…3, its commit 3cb818d): four sine operators, each a ratio of the note, its
phase moved by the operators above it as one of eight algorithms has them, operator 4 fed back through the mean of its
last two outputs, an envelope, level and velocity sense each; pitch on the 32-sample grid. **Here** it is the
registry's "fm" and nothing more: dropped as one line, its parameters addresses (`<dv>.algorithm`, `<dv>.op2.ratio`, …),
its window the generated face (four operator modules with their envelopes, the algorithm and feedback). Test: L2
`test_every_instrument_drops_as_one_line_and_sounds` (`core/tests/serviceTests.cpp:4704`: every instrument the
registry has — the FM synth among them — dropped as one line, a note in its pattern, a render that sounds; one that
takes a sound given one first — written once, it holds every instrument to come). Shot: fm-window.

### DR-INST-9 The multi-sample instrument, its SFZ read here (R-INST-9)
**The sound** is the DSP library's (REQ-multisample-1…3, its commit 2cf1457): an SFZ's regions by key, velocity,
round robin and chance, each its own sound, root, tuning, loop and envelope, choking by group; its parser turns text
into regions and says what it did not read. **Here**: the registry's "multisample" takes a DEFINITION
(`takesDefinition`): its `sample=` is an .sfz. `set <dv>.sample=` checks it reads (`resolveSample`,
`core/service/ServiceEdit.cpp:124` — a text, not audio; an unreadable one refused). At every build Compile reads it
through the service's text provider (`textFor`, `core/service/SolarisService.cpp:561`; `core/Compile.cpp:236`),
parses it with the DSP library's `parseSfz` — each problem a warning (an opcode not read, a bad value) — and asks the
decoder for each sound it names, beside the .sfz; one that cannot be read is said and leaves its regions silent. The
engine hands the text and the sounds to the device at build (`engine/Engine.cpp:70`), never on the audio thread. The
model: `devices[].takesDefinition` / `deviceTypes[].takesDefinition`; the panel asks for "an .sfz"
(`app/widgets/DevicePanel.cpp:460`); `audit` says one with none. Tests: L2 `test_a_multisample_reads_its_sfz`
(`core/tests/serviceTests.cpp:4741`: an .sfz of a soft layer and a loud one in turn — beat by beat 220, 330, 440 Hz,
each alone; a missing sound and an opcode not read said; a file that is no instrument refused; none, said by the audit);
`test_every_instrument_drops_as_one_line_and_sounds` (`:4704`, now giving a definition-taker its .sfz). Mutants (each
red): the definition read as a sound; its sounds looked for beside the song; never handed to the device; an .sfz
refused; its problems unsaid. Not yet: a browser drop of an .sfz onto the device (by `set`, or `--sample` on
`strip add`).

### DR-INST-10 The granular sampler (R-INST-10)
**The sound** is the DSP library's (REQ-granular-1/2, its commit eb1d014): a voice starts a grain every fs/density
samples from its note, each read from a place in its sound (scanned, sprayed) at the note's pitch against its root,
Hann-windowed, panned, the cloud divided by its overlap. **Here** it is the registry's "granular" and nothing more: a
`takesSample` instrument, given its sound as a sampler is (`set <dv>.sample=`), its parameters addresses
(`<dv>.grain.size`, `.grain.position`, …). Test: L2 `test_every_instrument_drops_as_one_line_and_sounds`
(`core/tests/serviceTests.cpp:4704`: the granular given a sound, a note, a render that sounds).

### DR-MIDI-3 Expression lanes (R-MIDI-3)
A pattern carries expression lanes beside its notes (`model/Project.h` `ExprPoint`; `#expr lane= at= value=` in the
`.slp`, docs/project-format.md §8): `bend` −1 … +1, `cc0` … `cc127` 0 … 1 (`mod` is `cc1`), linear between points,
held at the ends (`exprAt`). `expr add <pt> --lane --at --value` (a point already at that beat moved; the pattern
grows to hold it), `expr delete`, `expr clear [--lane]` — edits like any (`core/service/ServiceMidi.cpp:68`),
`patterns[].expr` in the model. The compile turns a clip's `bend` and `cc1` lanes into control events on its strip
(`core/Compile.cpp:434`): each repetition's value from its start, each point on its sample, a ramp a step every
64 samples (the engine's control period, `kControlSamples`), at rest where the clip ends; the engine applies them
in time order with the notes — a wheel before a note at the same sample — through the device seam
(`pitchBend` / `modWheel`), and a seek restores the last value of each before it (`engine/Engine.cpp:408`). Other
controllers are kept and go out in MIDI files, but no instrument plays them. MIDI files carry them both ways:
`parseMidi` reads pitch bend and controllers per track, `pattern import` makes them lanes (a repeated value
dropped), `pattern export` / `project export-midi` write them (each point, a ramp stepped every 1/32 beat; a song's
clip back at rest at its end). The keyboard's own wheels play the armed strip at once (`Player::midi`,
`core/Player.cpp:447`, through the engine's live ring) and a recording keeps them: the take's lanes thinned to where
the wheel moved by more than 0.005 or a sixteenth on, its last move kept; an overdub writes a moved lane over the
take's span. L2 `test_expression_lanes_play_the_wheels` (at rest 440 Hz, half way up a two-beat ramp a semitone,
held two after its last point, the next clip at rest, a seek landing bent; refusals; the .slp; undoable),
`test_expression_lanes_go_through_midi_files` (the curve to 1 %), `test_the_keyboards_wheels_play_and_record`; model
`test_expression_lanes_round_trip_and_are_checked`. Mutants: no ramp steps, a seek forgetting the wheel, no rest at a
clip's end — each red. **The roll's lane.** Vel | Bend | Mod beside the piano roll's lower lane choose what it shows
(the view's, cross-faded; `laneTabRect`, `app/widgets/PianoRoll.cpp:165`): the velocities, or the pattern's `bend` or
`cc1` lane as a curve through its points (linear between, held to the pattern's start and end; bend's centre line
drawn), each point keyed by (lane, beat), fading in and out, its value easing to a change made elsewhere. On a
wheel's lane: a click away from a point adds one at the snapped beat and the height's value (to a hundredth), a
point dragged up or down follows the pointer and lands as ONE `expr add` at its beat, a double-click (or a
right-click) removes it (`expr delete`). UI `test_the_rolls_lane_draws_the_wheels`; shot `piano-roll-bend`. Mutants:
the lane switch snapping, a shell's value snapping — red.

### DR-ARR-1 Markers and sections (R-ARR-1)
A marker is Interstellar's (law 16 — its `mk_` ids, `name`, `at`, `note` and `marker add <name> --at`) in the song's
beats, written `#amarker id=mk_1 name=Chorus at=16.0 length=8.0 note="…"` (`model/Project.cpp:564`; `length` and
`note` only when set; in the id space, so `nextId("mk")`; docs/project-format.md §9a, docs/audio-format.md §2.10). With
a `length` it is a SECTION; without, a point whose section runs to the next marker after it, else the song's end
(`Project::sectionEnd`, `model/Project.cpp:77`) — published as `markers[].end` beside `at`, `length`, `name`, `note`, by
time (`core/service/ServiceModel.cpp:371`). `marker add <name> --at [--length] [--note]` (prints its id), `marker rename
<mk> <name> [--note]`, `marker move <mk> [--at] [--length]`, `marker delete <mk>` — edits like any, kept sorted by time
(`core/service/ServiceArrange.cpp:335`); `ls` lists them with their sections. `transport seek <mk>` goes to its beat and
`transport loop <mk>` loops its section (`markerSpan`, `ServiceArrange.cpp:31`; `core/service/ServiceTransport.cpp:635`,
`:674`), an empty section — the last point at or past the song's end — refused. L2
`test_markers_name_places_and_sections`; model `test_markers_round_trip_and_name_their_sections`. Mutants: a point's
section ignoring the next marker, a loop taking only its length, markers outside the id space — each red. **The ruler.**
The ruler grows a band at its top for them (`Timeline::kMarkH` 13 of `kRulerH` 35.75, `app/widgets/Timeline.h:98`; the
bars' labels and the brace's grip — the lower half of the bars' band — below it): a flag per marker at its beat, its
name MEASURED (the width a press hits is the one last drawn), a section a band to its `end`, a hairline down through
the bars (`paintMarkers`, `app/widgets/TimelineMarkers.cpp:221`). Keyed by id: a marker fades in and out, a move made
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
starting before it and running over it stays and is named in the output (no `clip split` yet, R-EDIT-1); a
non-looping automation's points at or after it move, a looping automation's span (`at`) moves or, holding the point,
grows (`:100`); markers at or after it move, a section holding it grows (`:127`). **Delete:** clips, automation points
and markers wholly inside the span go; what follows moves earlier by its length; a note clip, or an audio clip set to
loop, that starts before `--from` and runs into the span is shortened by the overlap (what repeats keeps repeating);
any other clip across `--from` or `--to` is REFUSED, naming it — nothing changes; a section or a looping span across an
edge shrinks, one whose head is in the span starts at `--from`. **The loop** (the session's, `:153`) moves, grows or
shrinks the same, ends when taken out entirely, and stays where it is on undo. The output says what moved, what went,
and what crossed. **The ruler's menu** (right-click away from a flag, `app/widgets/TimelineMarkers.cpp:201`): Insert a
bar here (`time insert --at <the bar's start> --length <a bar>`), Delete this bar, and — with a loop — Delete the loop's
time. L2 `test_time_insert_and_delete_move_the_whole_song`; UI `test_time_edits_from_the_ruler` (a clip moved eases
there); shot `ruler-menu`. Mutants: the inside clips kept, insert leaving automation points, a plain audio clip let
across an edge, the ruler inserting at the pointer and not the bar — each red. Found while building it: a clip moved
INTO the span by the shift was then taken as inside and deleted — the inside is decided first now.

### DR-ARR-3 Lane groups (R-ARR-3)
A group is organisation only, as a lane is: `#alanegroup id=lg_1 name=Drums [folded=on]` written before the lanes, each
of its lanes naming it (`#alane … group=lg_1`; `model/Project.cpp:510`; a lane naming a group the song lacks is refused).
Its lanes are side by side in the lane order — `gatherLaneGroups` (`model/Project.cpp:1273`) gathers each group at its
first lane's place and drops a group with no lane; a song opened scattered is gathered and said so. `lane group <ln> …
[--name]` (`core/service/ServiceArrange.cpp:189`) makes a NEW group of the lanes, gathered at the first one's place in
their order (a lane leaves any group it was in; prints the id); `lane ungroup <lg>` lets them go; `lane move <lg> --to
<i>` moves the group whole, never into another group's run (`:232`); a LANE moved (`core/service/ServiceEdit.cpp:571`)
joins the group whose run it lands inside and leaves its own when it lands outside it; a lane deleted can leave its
group empty, and the group goes. `set lg_1.name=` / `set lg_1.folded=on|off` (`core/service/SolarisService.cpp:1077`), `get`
(and a lane's `.group`). `clips move "<ac> …" --by <beats>` (`:282`) moves clips in time as ONE edit, refused whole
when a clip is not the song's or would start before beat 0 — a folded group's clips dragged together, or any
selection. The model: `lanes[].group`, `groups[]` (id, name, folded, its lanes — in the order of their first lanes);
`ls` prints a group before its first lane. L2 `test_lane_groups_gather_fold_and_move_together`; model
`test_lane_groups_round_trip_and_are_checked`. Mutants: groups not gathered, a lane landing in a run not joining it, an
emptied group kept — each red (the last only once the test read the song itself: the model lists no empty group).
A lane's `.group` (`core/service/SolarisService.cpp:1048`): `set <ln>.group=<lg>` joins it (gathered with it),
`=none` leaves its group, placed right after the run it left. **The timeline.** An open group's lanes carry a bar down
their headers' edge (`app/widgets/Timeline.cpp:1552`, from where the rows ARE) and, on the first, a chevron and the
group's name in the header's bottom band. Folded, the group is ONE row (`:145`): its lanes fade out, the rows below
ease up, its clips travel into it — each lane's clips in their own band of the row (`clipBox`, `:373`; band and height
eased), so they never lie on one another — and its header says the group's name, "a group, folded", "▸ N LANES". The
chevron turns as it folds and opens (`:658`), and a click on it or on the name (`groupToggleRect`, `:385`) is ONE `set
<lg>.folded=on|off`. A clip dragged on a folded row carries the group's clips (`:952`; never before beat 0), the
pointer's while held, then ONE `clips move "<ac> …" --by <beats>` where they were let go (`:1015`). The folded row's
menu (`:1106`): Open group · Rename group… · Ungroup · Copy ID; a lane's (`:1132`): New group · Into group… · Leave
group · Fold group · Ungroup. UI `test_lane_groups_on_the_timeline`; shots `lane-groups`, `lane-group-folded`,
`lane-group-folding`. Mutants: the chevron snapping, a folded row dragging one clip, folding keeping the lanes — each
red.

### DR-REC-1 Record audio (R-REC-1)
**The seam.** `IAudioIn` (`core/AudioIn.h`) beside `IAudioOut`: `open(device, rate, channels, buffer)`, `read(frames)` that
never blocks (what has not arrived is silence, counted by `missed`), `latency()`; a host gives one through
`Host::audioIn` — none, and arming an audio line is refused by name. **The song.** A strip's `input` (an input port —
`port add <name> --dir in [--channels 1|2]`, the machine's settings mapping it to a device as any port, `port.<name>=
<device>:<channel>`) and `monitor` (`auto` | `on` | `off`) are the song's (`#atrack … input=prt_2 monitor=off`, written
when set; checked: the input an input port, `model/Project.cpp:1062`), set by `set <ch>.input=<port>|none` /
`set <ch>.monitor=` (`core/service/SolarisService.cpp:934`). **Arming** (`strip arm <ch> [--input <port>]`,
`core/service/ServiceInput.cpp:552`): an audio line arms for its input — `--input` sets it first, and only then is the
line an edit; with no input it is refused naming the song's input ports (or `port add`); one strip armed at a time, as
the keyboard's. **Monitoring** (`monitorHears`, `:134`): `on` heard whenever armed, `off` never, `auto` while stopped or
recording — not while the song plays back. Stopped, the input monitor (the song's mix without its clips, R-MIDI-2's)
plays it; playing, the song's player does — the input opened beside the output (`attachInput`, `:141`; `openInput`,
`:102`, on the port's device). A change of `.monitor` on the armed strip is heard at once (`SolarisService.cpp:947`). **The path.** Each
cycle the player reads a block of the input before it renders (`Player::input`, `core/Player.cpp:75`) and the engine
hears it through the armed strip's chain — its rack, its fader, its meter — as a clip of it (`Engine::liveInput`,
`engine/Engine.cpp:433`; mixed at `:806` by where each piece sits in the block; sized at build: no allocation). A
count-in reads it and neither hears nor keeps it. **Recording** (`transport record` with an audio line armed,
`core/service/ServiceTransport.cpp:426`): `startTake` (`core/Player.cpp:33`) — the audio thread pushes what it reads into a
lock-free ring (never waiting; a frame the ring cannot hold is counted and said at the stop) and a recorder thread of
the player's drains it into the take every 2 ms (`:45`), so a long take needs nobody to pump it. At the stop the audio
thread joins first, so nothing more can come (`ServiceTransport.cpp:461`), and `commitAudioTake`
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
(`armRow`, `app/widgets/MixerDock.cpp:71`; a bus and the master keep M | S), R lit red when the strip is armed, eased.
R (`:1117`) disarms an armed strip (`strip arm none`); on an audio line with no input it offers the song's input
ports — "Arm with Mic" is ONE `strip arm <ch> --input <port>` — or, with none, "Add an input port (In)" (`port add In
--dir in`); else it arms (`strip arm <ch>`). An audio line's menu (`:1255`): Input… (the input ports, the current one
marked, No input, Add an input port… — a name typed in cosmo's field) and Monitor… (Auto · On · Off — `set
<ch>.monitor=`). The ruler's menu adds Punch the loop's span (`transport punch <loop>`) and Punch off
(`app/widgets/TimelineMarkers.cpp:209`); the punch is a red line atop the bars (`Timeline::punchRect`,
`app/widgets/Timeline.cpp:377`), eased in and out, its ends travelling when it moves (`:634`). The song bar's Record
records whichever strip is armed. UI `test_recording_audio_from_the_window` (the Rig's host now has a capture that
hears nothing); shot `recording-audio`. Mutants: the R light snapping, R arming with no input asked, the punch
snapping in — each red. Loop takes are R-REC-2's (N17).

### DR-REC-2 Takes and comping (R-REC-2)
A clip's TAKES (`Clip::takes`, `#take src=…` under its `#aclip`) and its COMP (`#comp take= from= to=`, song beats) are
read and written in place (`model/Project.cpp:512`); `Clip::takeAt` (`:64`) is the take that plays at a beat — the comp
range's (the latest written where they meet), else the LAST; a comp naming a take the clip lacks is refused. **Made by
recording while the song loops**: the recorder thread notes each jump of the song's position — a loop turning — with
the take's frame there (`Player::Take::marks`, `core/Player.cpp:54`); at the stop (`core/service/ServiceInput.cpp:162`)
every frame is placed where it was HEARD (its read position minus the latency, as DR-REC-1 says), and a frame heard
before the loop's start just after it turned is the END of the pass before (`:176`) — so each pass is exactly what was
heard over the loop. A pass that heard less than half the loop is dropped; two or more make ONE clip spanning the loop,
every pass a take — its own file, `Recordings/<strip> <n>.wav`, the next free numbers — the clip's own source the last;
one whole pass is a plain clip; none, as before. `record.finished` says `takes`. `clip comp <ac> --take <n> [--from
--to]` (`core/service/ServiceArrange.cpp:340`, the whole clip without a range): ONE edit; the range overwrites what it
covers — a range it cuts keeps its head and its tail — neighbours of one take are merged; refused on a clip with no
takes, a take it lacks, an empty range. **Played** (C5, `core/Compile.cpp:356`): the clip's span cut at its comp's
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
each the take heard there (`core/service/ServiceModel.cpp:324`) — so a front end only draws it. A clip with takes says how
many at its top right where there is room beside its name (`app/widgets/Timeline.cpp:1230`); its menu offers Show takes
· Hide takes (`:1191`; the view's — `showTakes`, `app/widgets/TimelineTakes.cpp:29`, re-binds at once): a row per take
under the clip's lane (`Timeline.cpp:178`; keyed rows, so the rows below slide down and back, eased), its span drawn
faint and lit where `plays` gives it the take (`paintTakes`, `:142`) — cross-faded from what was lit when the comp
changes (`bindTakes`, `:51`). On a take's row (`takeGesture`, `:80`): a drag across it, on the grid you see and
inside the clip, is ONE `clip comp <ac> --take <k> --from --to` (the pointer's range shown while held); a
double-click, that take for the whole clip (`clip comp <ac> --take <k>`). UI `test_takes_under_their_clip`; shot
`takes`. Mutants: the lit takes snapping over, a drag taking the whole clip, Show takes changing nothing until the next
edit — each red.

### DR-EDIT-1 Audio editing on the lanes (R-EDIT-1)
**Reverse.** `Clip::reverse` (`#aclip … reverse=true`, written only when set — `model/Project.cpp:386`), `set <ac>.reverse=
true|false` (`core/service/SolarisService.cpp:1004`; a note clip refused) and `get`; compiled to `Region::reverse`, which the
engine reads from its span's end (`engine/Engine.cpp:573`) — a loop's every repetition backwards; a clip with takes
mirrored stretch by stretch (`core/Compile.cpp:409`). **Crossfades** (C6, `heardFades`, `core/Compile.cpp:118`): an audio
clip's fade in grows to cover a clip of its strip on its lane that began before it and ends inside it, its fade out one
that begins inside it and ends after — a clip wholly inside another, or one on another lane, is mixed; the compile plays
the grown fades, the model publishes them (`clips[].fadeInHeard`, `.fadeOutHeard`, `core/service/ServiceModel.cpp:317`)
and the clips keep their own. **Split** (`splitClip`, `core/service/ServiceAudioEdit.cpp:93`): `clip split <ac> --at <b>`,
`b` on the tick grid strictly inside the clip; the second half a new clip after the first in the list, its id printed —
the halves' `in`/`out` meet where the cut is (a reversed clip's first half its span's end), the fade in the first's and
the fade out the second's (each no longer than its half), the comp cut in two, every take kept by both. A looping clip
(`:107`) is cut only within half a tick of where a repetition begins — each half loops from its start — and the refusal
names the nearest such beats inside it; a note clip is refused (R-CLIP-3). **Consolidate** (`consolidateClips`, `:279`):
audio clips of one strip; a copy of the song holding only them and their strip — no devices, no sends, no bindings, the
strip at 0 dB, centred, to a master at 0 dB — compiled and rendered by an engine of its own from the first one's start
to the last one's end (its output latency trimmed), written as `Bounces/<strip> <n>.wav` (`writeBounce`, `:349`, shared with a strip's bounce, DR-PERF-3: 32-bit float,
the next free number, a clip's name counted as taken so an undone bounce is never overwritten); the clips deleted and one `clip add`
in their place on the first one's lane — one edit. **Slice** (`audioEditCommand`, `:164`): `--every <b>` cuts every `b`
from the clip's start; `--transients` decodes what the clip plays (its span, mixed to mono, reversed when the clip is)
and cuts at the DSP library's `detectOnsets` (`:212`, REQ-onset-1; `--sensitivity`), dropping a cut within half a tick
of either end; each cut a `splitClip`. It prints the first piece; `made:` names the others in order. `--every --kit`
(`sliceToKit`, `:234`): N = the clip's length over `b`, rounded (1 … 64); a `clip add --instrument sampler --sample <the
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
Its menu (`clipEditItems`, `:332`, `app/widgets/Timeline.cpp:1196`): Split at the time bar (where the time bar is inside
it) · Reverse / Play forwards · Consolidate — the clips it runs into on its lane through its strip, touching or
overlapping, "Consolidate N clips" when more than itself · Slice ▸ Every bar / beat / half beat · At its hits (not a
comped clip) · To a kit, a slice a beat (no Slice on a loop). Ctrl+E — the keymap's `lanes.split`
(`core/service/Keymap.cpp:23`) — and Edit › Split Clip at the Time Bar split the selected clip at the time bar
(`app/App.cpp:546`). UI `test_a_clips_fades_gain_and_edits`; shots `clip-edit`, `clip-edit-dragging`. Mutants: a dragged
fade drawn from the model, the fades snapping, the line sent while still held, Consolidate only the clip itself, the
heard fades drawn as the clip's own, Ctrl+E doing nothing — each red.

### DR-WARP-1 Warp and pitch on audio clips (R-WARP-1)
An audio clip's `warp`, `bpm` and `pitch` (`#aclip … warp=true bpm=100.0 pitch=-3.0`, each written only when set —
`model/Project.cpp:387`). `clipSecondsPerBeat` (`core/Compile.cpp:31`): a warped clip's beat is 60/bpm seconds of its
file, else the song's — so `clipLengthBeats` gives a warped clip its span's own beats, and `clip split` / `clip slice`
cut it at them. **Played** (C7): `stretchKey` (`:33`) names what a warped or pitched clip plays —
`stretch:<bpm/song bpm>:<semitones>:<in>:<out>:<src>`, exact numbers; empty for a plain clip or one with takes — and the
compile asks the sound provider for that key in place of the file (`:425`), the region reading the whole stretched
sound. The service makes a key's sound once (`pcmFor`, `core/service/SolarisService.cpp:578`): the file's span through
the DSP library's `timeStretch` at the song's rate (`stretched`, `core/service/ServiceAudioEdit.cpp:53`), kept beside the
decoded files — sixteen at most before those no clip plays go — and a render at another rate stretches the file decoded
at that rate (`core/service/ServiceRender.cpp:141`). A song tempo change changes the key, so the next build stretches
again. **Set** (`core/service/SolarisService.cpp:979`): `warp=true` with no `bpm` listens first — `tempoOf`
(`core/service/ServiceAudioEdit.cpp:81`, the DSP library's `estimateTempo` on the span's channel mean, to a thousandth)
— in the same edit, and nothing heard refuses naming `.bpm`; `bpm` 20 … 999, `pitch` −24 … +24; a note clip and a clip
with takes refuse all three. `clip add --src … --warp` (`core/service/ServiceEdit.cpp:726`) listens on import; `--warp`
without `--src` is refused. A kit (`clip slice --kit`) of a warped or pitched clip is refused. Model `clips[].warp`,
`.bpm`, `.pitch`. L2 `test_a_warped_clip_follows_the_tempo` (a 100 bpm groove heard at 100.000 and its 8 beats its own;
a 2 s sine at its own 60 bpm warped into a 120 song — its 2 beats in one second at 440.000 Hz, then nothing; +12
semitones 880.000 Hz as long, twice the same bytes; the song at 60 — two seconds; split at its own beat; refusals).
Not covered at L2: a render at another rate (the fake decoder hands back what it was given at any rate). Mutants: the
compile playing the file, the song's beats for a warped clip, the ratio upside down, no listening (120 always), the
pitch never moved — each red. **The window** (`app/widgets/TimelineClipEdit.cpp`): an audio clip's menu
(`clipEditItems`, `:372`) offers Warp to the song's tempo / Unwarp (`set <ac>.warp=true|false`), Tempo… (the rename field
with its tempo; what is typed is ONE `set <ac>.bpm=…`) and Pitch ▸ (+12, +7, +5, +1, 0 as recorded, −1, −5, −7, −12; the
current one marked "· now"; ONE `set <ac>.pitch=…`). The clip says its tempo when warped and its pitch when moved —
`clipTag` (`:67`), "60 bpm · +7 st" — at its left past its fade in, above its gain line, cross-faded from what was shown
when it changes (`advanceClipEdit`, `:88`; LIVE `clipTagAlpha`, `:80`). UI `test_a_clip_warped_from_its_menu`; shot
`clip-warp`. Mutants: the tag snapping, Tempo… setting the pitch, a pitch item sending the wrong number, Warp always
on — each red.

### DR-TEMPO-1 A tempo map (R-TEMPO-1)
`#atempo id= at= bpm= [ramp=true]` (`model/Project.cpp:566`; validated after beat 0, rising, 20 … 999). **The map**
(`engine/Tempo.h:23`): plain data in the engine — segments of a constant tempo (T1) or one gliding linearly in beats to
the next (T2), each with its first beat and sample; `add` (`:45`) appends a change (a glide bends the segment before
it); `sampleAt` (`:74`) and `beatAt` (`:81`) are closed-form both ways (a glide's time is (60/k)·ln(bpm(b)/bpm0)). One
constant segment computes exactly the old `b · (60/bpm · rate)`. `tempoMapOf` (`core/Compile.cpp:16`) builds the song's.
**The compile** (C8): every clip start, take run, fade, loop span, note, wheel step and the song's end through `S(b)`
(`:306`) — b·spb, as ever, for one tempo; the map in the clock (`:515`). **The engine's clock** (`engine/Expr.h:220`): a
formula's `beat` and `bpm` are the map's; curves stay compiled in beats at the start's samples-per-beat and are read at
the song's beat (`curveSample`, `:228`). **The service**: `sampleOfBeat` / `beatOfSample`
(`core/service/SolarisService.cpp:540`) for playback's start, seek, the loop, the time bar heard and stopped, a
recording's takes and notes; the count-in is its bars at the tempo where it starts; the metronome reads the playing
engine's map (`core/Player.cpp:478`). `tempo add|move|delete` (`core/service/ServiceArrange.cpp:271`): after beat 0 (the
start's is `project.bpm`), one to a beat, sorted; refusals name the way. `time insert|delete` carry them (`:147`).
`project.bpm` is refused as a formula's target naming `tempo add` (`core/Bindings.cpp:27`). A warped clip under a map
asks for `stretch:map:<its bpm>:<semis>:<in>:<out>:<at>:<song bpm>:<changes>:<src>`; `parseStretchKey`
(`core/Compile.cpp:49`) rebuilds the map at the stretch's rate and gives the DSP its own beats every sixteenth → where
the song hears them. The MIDI file (`core/service/ServiceMidi.cpp:262`): each change at its tick, a glide as a step every
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
`app/widgets/TimelineMarkers.cpp:198`). The song bar shows the tempo where the time bar is (`app/widgets/SongBar.cpp:55`,
eased as ever) and its drag or typed value sets the change in effect there — `project.bpm` before the first — from
`transport.tempoId` (`tempoAt`, `core/service/SolarisService.cpp:552`). UI `test_tempo_changes_on_the_ruler`; shot
`tempo-map`. Mutants: a change appearing at once, the drag sent while held, the song bar at the start's tempo, the song
bar always setting the song's tempo, a retimed change snapping — each red.

### DR-PRESET-1 Device presets (R-PRESET-1)
Two folders, named by the host (`presetPaths`, `host/Machine.cpp:55`): the user's (`SOLARIS_PRESETS`, else
`$XDG_DATA_HOME/solaris/presets`) and the factory's (`SOLARIS_FACTORY_PRESETS`, else `apps/solaris/presets` of this build
— five ship: three synths, a reverb, a delay). A preset is `<folder>/<type>/<name>.slpreset`: `arstro-preset = 1`,
`type`, `name`, then `<param> = <text>` lines as the `.slp` keeps a device's set values (`sample = <path>` for a
sampler); `;` comments. `scanPresets` (`core/service/ServicePresets.cpp:71`) lists every registry type's — the user's,
then the factory's of a name the user has not — into the model (`presets[]`, refreshed once and after a save or a
delete, `core/service/ServiceModel.cpp:104`). `applyPreset` (`:102`): the user's file of the name, else the factory's; its
type must be the device's; the device's set values and sound cleared (every value back to its default), then each of
the preset's through `setAddress` — so a value `set` would refuse refuses the load, and `dispatch` puts the song back:
nothing changes. `presetCommand` (`:138`): `preset list [type]`; `preset delete <type> <name>` (the user's only; a
factory's refused by name); `preset save <dv> <name> [--replace]` (`:187`; a name of 1 … 64 characters, no slash, not
starting with a dot; an existing one refused without --replace; the folder made) — save, list and delete are in
`mutates`' list (the machine's: no undo, no unsaved mark); `preset load` is an edit. A drop: `device add … --preset`
(`core/service/ServiceEdit.cpp:481`) and `clip add --instrument … --preset` apply it in the same command; `--preset`
without `--instrument` refused. The presets folders are read with `std::filesystem` at the host's paths (the test host
lists nothing real). L2 `test_presets_are_saved_loaded_and_dropped`, `test_the_shipped_presets_load` (every shipped one
onto a device of its type). Mutants: a load keeping the values it does not name, a bad value skipped and the rest
loaded, a save an edit of the song, the factory's before the user's, a drop's preset ignored — each red. `preset list`
reads the folders afresh (`core/service/ServicePresets.cpp:137`), so presets written since show. **The window** (N22
part 2, 2026-10-17): the browser's fourth tab, Presets (`app/widgets/Browser.cpp:167`) — each type that has presets, its
label in capitals as a header, then its presets, the factory's marked "factory" at the right; keyed rows, so a saved one
grows in. The four tabs are as wide as their names need (`tabRect`, `:263`) and the highlight eases its width with its
place. A preset row drags like an instrument: an instrument's dropped on the lanes is ONE `clip add --instrument <t>
--preset <name> --at … --length 4 [--lane …]` (`app/widgets/ProjectScreen.cpp:150`); an effect's says it loads onto its
device. A device window's header has Presets (`app/widgets/DevicePanel.cpp:574`): its type's presets (the factory's
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
the machine's (no undo, no unsaved mark). `project new <path> --template <name>` (`core/service/SolarisService.cpp:712`):
the template's song with the new song's id (from its path) and name (--name or the path's stem), its tempo, meter and
rate unless --bpm, --sig or --rate say; written and opened as any new song. L2 `test_templates_start_songs`. Mutants: a
template keeping the song's relative files, a song from a template keeping the template's name, --bpm ignored, the
factory's first, a save as an edit — each red. **The window**: Home's third action, From template…
(`app/widgets/HomeScreen.cpp:137`), and File › New from Template… open a menu of the templates, the factory's marked
(`pickTemplate`, `app/App.cpp:285`); one picked, the host is asked where the song goes and the answer is ONE `project new
<path> --template <name>` (`newSongPicked`, `:275` — the template forgotten once used, so a plain new song after carries
none). File › Save as Template… (`:157`; at the menu's end, the others where they were) is the rename field: ONE `template
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
--favourites`, `core/service/ServiceMachine.cpp:324`): the sample folders (or the folder given) walked through the host's
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
(`engine/MixGraph.h:55`) is used instead of the registry (`Engine.cpp:61` — a parameter it lacks is skipped), and a
made device is NOT warmed (`:238`): what it plays may be the engine the swap replaces. A binding to a plugin's
parameter is resolved by name at build (`:207`; `Bindings.cpp:65`, `Compile.cpp` sets `paramName`). *Compile*
(`Compile.cpp:188`) turns a `vst3` node into its numbers and an offline stand-in the service replaces; an instrument
strip whose first is a plugin instrument stays one (`:341`). *The model* (`model/Project.cpp:293`): a `vst3` device
keeps `plugin` (the UID), `name`, `kind`, `bundle` after its type, its `p<id>` values, and `state` (base64) last.
*The service* (`core/service/ServicePlugins.cpp`): `findPlugin` (`:74`, by id or name in any case, the nearest named);
`device add <ch> --plugin` (an effect; an instrument refused, pointing at `strip add --kind instrument --plugin`),
`strip add --kind instrument --plugin`, `clip add --plugin` (`core/service/ServiceEdit.cpp:444`, `:206`, `:660`); `startPlugin` (`ServicePlugins.cpp:104`) loads it at the
song's rate, gives it the song's state, then the song's set values (a no-frame block), and its type; `livePlugin`
(`:140`) is the device's connection, made on first need (the model asks first), kept across engine swaps, made again
for another plugin, bundle or rate, and says `plugin.offline device why` once if it cannot start; `typeOf` (`:160`) is
how `set`/`get` (`SolarisService.cpp:1129`), live updates and the model read a plugin's parameters; `attachPlugins`
(`ServicePlugins.cpp:166`) gives each compiled `vst3` node its player — the live connection (`buildLive`, `ServiceTransport.cpp:61`), a new
one with a budget for another stream (a note preview `:336`, the input monitor `ServiceInput.cpp:309`), or a new one that
is waited for (a render, `ServiceRender.cpp:163`, which says afterwards which were offline and so silent or dry in the
file); `pluginFacts` (`ServicePlugins.cpp:198`) puts `plugin`, `offline`, `why` and the plugin's name in `devices[]`; `prunePlugins`
(`:314`, every refresh) ends the connections of devices no longer in the song; `capturePluginStates` (`:324`) asks each
playing plugin for its state before the song or its autosave is written; `device reload <dv>` (`pluginReload`, `:337` —
not an edit) starts a new process from the last state and swaps the engine to it, printing `playing` or
`offline: <why>`. `pump` (`ServiceTransport.cpp:147`) says a plugin that went offline while playing, once, and refreshes
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
`devices[].editor`. `pump` gathers what the plugins said (`gatherPluginEdits`, `:240`; `ServiceTransport.cpp:143`):
an editor's values become ONE `set <dv>.p<id>=…` line per plugin per pump (the last of each), a plugin's own new values
move those the song already sets, a closed window is said, a new latency rebuilds the playing engine. The lines land
(`applyPendingEdits`, `ServicePlugins.cpp:285`) when no command is running — `dispatch` counts them
(`SolarisService.cpp:135`), since `wait` pumps from inside one — else first thing in the next `dispatchText` (`:51`);
consecutive ones are one undo step (R-EDM-1's coalescing). What a plugin set is not sent back to it
(`ServiceTransport.cpp:664`): an echo landing mid-drag would pull its knob back. Tests: L2 `test_a_plugins_own_editor`
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
stands (a plugin's presets are its own) is **Editor** (`editorRect`, `DevicePanel.cpp:484`; drawn `:650`): a click is
`device editor <dv>`, or `--close` while its window is open (`:569`), and its fill eases to the accent while that window
is open (`editorAmount`). Offline — never started, crashed, overran — a band slides open under the header
(`kOfflineH` = 29.25, `app/widgets/DevicePanel.h:50`; drawn `DevicePanel.cpp:687`, its content riding the band's
lower edge): "Offline — <why>" in the destructive colour, and **Reload** (`reloadRect`, `:485`) — `device reload <dv>`
(`:565`); the rows below are pushed down with it, because `bodyTop()` (`DevicePanel.h:99`) includes the eased band.
Both ease from the model wherever the change came from (`DevicePanel.cpp:347`; the first placement is set, not
animated). The rig's host plays two in-process plugins (`RigPlugin`, `app/tests/Rig.h:59`; one named by
`rigPluginCrash()` is offline at once). UI `test_a_plugins_device_window` (`app/tests/ui/uiTests.cpp:4298`): an
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
settings (`map.<cc> = <address>`, `core/Settings.cpp:34`, read back `:75`) and says `settings.changed what=map.<cc>`;
`map remove`, `map list` (`(inert)` beside what the open song lacks); `map learn <address>` waits (`mapLearning` in the
model), `--cancel` stops. All four are in `mutates`' list — never an edit, never in the song. **Listening**: the MIDI
input's sink (`ServiceInput.cpp:89`) puts every controller and every MIDI Machine Control message into a second queue,
the service's; the input opens when a mapping is added or a learn starts, and once at `pump` while there are mappings
(`controlMidi`, `ServiceControl.cpp:70`). **What a control does** (`pump`, `ServiceTransport.cpp:144`): during a learn the
first controller moved is bound on ITS channel (`:108` — its value not taken; `map.learned cc address`); otherwise its
mapping — its channel's first (`:105`), then any channel's — moves the address (`controlValue`, `:54`): a strip's,
a send's or the master's gain by the fader law (`faderDb`, `core/service/FaderLaw.h:14` — moved out of the dock, which
now calls it, `app/widgets/MixerDock.cpp:113`), a registry device's parameter by `valueFromNormalized` (its taper, as
its knob), a plugin's `p<id>` as its own 0 … 1, anything else linearly; clamped, whole where the number is. A pump's
moves are ONE `set` line (the last value of each address), and MIDI Machine Control (`:85` — Play / Deferred Play,
Stop / Pause, Record Strobe, Fast Forward / Rewind to the next / last bar) `transport` lines; they land as a plugin
editor's do (`applyPendingEdits`, `ServicePlugins.cpp:287`: when no command runs, else first in the next one) — but
heard, as any edit (only a plugin's own are kept from echoing back). **The host** (`host/MidiInAlsa.cpp:99`) parses a
SysEx `F0 7F <dev> 06 <cmd> F7` into `{0xF0, cmd, 0x7F}` (`MidiEvent::mmc`, `core/MidiIn.h:28`); other SysEx does not
arrive. The model: `settings.mappings[]` (cc, address, inert) and `mapLearning`; `audit` says a mapping the song lacks
(`ServiceModel.cpp:575`). L2 `test_midi_learn_and_the_transport_keys` (`core/tests/serviceTests.cpp:3923`): not an edit,
in the settings file; the top of a control is +6 dB, a sweep lands its last value and undoes in one step; a channel's
mapping deaf to another channel; a cutoff by its taper; learned on its channel with its value not taken; cancelled;
removed; MMC plays and stops; refusals (128, 17/1, a name, a missing strip, no mapping, no address); inert in another
song and said; read back by a new service. Mutants (each red): a gain moved linearly, a parameter moved linearly, a
channel ignored, the learned value taken, the transport keys ignored, a mapping taken for an edit, an inert mapping
not said, the mappings not kept. Found while building it: the first "not an edit" check could not fail (the song was
already unsaved) — it now saves first; and one mutant did not compile, so its run tested the old binary — rewritten
and run again.

**Part 2 — the window.** The shared parameter menu (`app/widgets/ParamMenu.cpp`) offers every number **Learn MIDI CC**
(`:85` — `map learn <address>`) and, for each control that moves it, **Forget CC <n>** (`map remove <n>`); the
widget fills `ParamTarget::controls` from `settings.mappings[]` (`controlsFor`, `:43`; the dock,
`app/widgets/MixerDock.cpp:996`; a device's rows and knobs, `app/widgets/DevicePanel.cpp:523`). While a learn waits, the
song bar shows a pill after the tempo (`app/widgets/SongBar.cpp:297`; its rect `:82`, up to the meter, 260 at most) —
"MIDI learn · <address> — move a control" — easing in and out (`:132`; the address kept while it fades), and a click on
it is `map learn --cancel` (`:215`). Settings has a **MIDI** page (`SettingsSheet::kMidi`): each mapping a row —
`CC <n>`, the address, dimmed and "not in this song" when inert — with × forgetting it (`map remove`,
`app/widgets/SettingsSheet.cpp:902`); the rows are keyed by control, a new one growing in and a forgotten one shrinking
out (`:271`, `:710`; drawn `:1079`), the first placement set. UI `test_midi_learn_in_the_window`
(`app/tests/ui/uiTests.cpp:4357`): Learn from a fader's menu — the line, the pill caught mid-fade, a click on it
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
none|mcu|hui`, `surfacePort = <port>` (`core/Settings.cpp:69`; `settings set`, `core/service/ServiceMachine.cpp:157`,
refusing another protocol). The keyboard never listens on the surface's port (`ServiceInput.cpp:70` — every port but
it when it listens to all). **The service** (`core/service/ServiceSurface.cpp`): `surfacePump` (`:95`, from `pump`,
`ServiceTransport.cpp:145`, so at the end of every command too) starts the surface when the settings changed
(`startSurface`, `:27` — its port in and out; the codec; everything sent as it connects, HUI pinged at once, `:58`;
why it cannot, said; `surface.changed protocol connected bank why`); then what it moved, through the grammar as a drag
would: a fader `set <ch>.gain` by the fader law, the master `project.masterGain`, a pot the pan in 0.02 steps (twice in
a pump from where the first left it), mute / solo toggled, arm `strip arm <ch>|none`, the transport keys as MIDI
Machine Control's, bank / channel the eight it shows (said); touch and release (`:135` — let go, its fader is sent where
the song has it). The lines land as a MIDI control's. Then what it shows (`surfaceState`, `:73` — the model's strips
in processing order from the bank: their faders by `faderPos`, pans, mute / solo, arm, names; the master; playing,
recording) is encoded against what it was sent, and sent; HUI's ping once a second. The model: `settings.surface`,
`settings.surfacePort`, `surface` (protocol, connected, why, bank). Tests: L2 `test_the_surface_codecs`
(`core/tests/serviceTests.cpp:4022`: both codecs as tables) and `test_a_control_surface` (`:4038`: connected, the
keyboard deaf to the port, everything shown and then nothing while nothing changes; a fader to +6 dB, a pot, a mute
and their light and ring at once; the song moving a fader back, not one touched until let go; play and stop and the
play light; a channel on, the first fader then the song's second strip; none disconnects; HUI's fader pair and ping; a
refused protocol; a port that is not there said). Mutants (each red): the keyboard on the surface's port, a fader moved
linearly, a touched fader moved by the song, everything sent every time, another bank never taken, a let-go fader not
sent back, HUI's pot turned the wrong way, an empty slot left up and lit, HUI never pinged. Found while building it:
HUI's first ping waited out the second the Mackie session had started — a connection pings at once now; and the first
idle check was missing, so "everything every time" survived — added.

**Part 2 — Settings.** The MIDI page's first two rows (`SettingsSheet::kSurface`, `kSurfacePort`,
`app/widgets/SettingsSheet.h:85`) are combos like every setting's — Control surface: None, Mackie Control, HUI; Surface
port: None and the MIDI ports `devices list` names (`app/widgets/SettingsSheet.cpp:363`) — each choice one `settings
set` line; the mapped controls follow under a micro-label (`:1004`). The surface row's note (`rowNote`, `:1017`) says
what the model says: what a surface does, "connected · strips 1–8" (its bank), or "not connected — <why>"; a change
cross-fades from the old line (`:694`). The rig's host has a MIDI output that goes nowhere (`QuietOut`,
`app/tests/Rig.h:157`). UI `test_a_surface_chosen_in_settings` (`app/tests/ui/uiTests.cpp:4422`): Mackie Control
picked — its line, the note caught mid-fade, then "no port"; a port picked — its line (the whole `key=value` quoted, as
the sheet spells any), "connected · strips 1–8"; None — its line, disconnected. Shot settings-midi now shows a Mackie
Control surface connected over the mappings. Mutants (each red): the note snapping, the combo sending another key, the
port list without the machine's ports, the note blind to the connection; a fifth — Settings no longer asking for the
MIDI ports — SURVIVED, and showed the ask was redundant (`devices list` already lists them): removed.

### DR-PERF-1 A CPU meter (R-PERF-1)
**The block.** The player reads a clock as a block's work starts (`core/Player.cpp:540`) and again before it waits for
the device (`timed`, `:498`, called `:573`): the time over the block's duration is the block's LOAD — smoothed over
about a third of a second into an atomic, its worst kept in another (a compare-and-swap maximum the service takes and
holds for a second). No allocation, no lock — the audio thread's rules. **The strips.** The engine adds up each
strip's processing time only when asked (`measureTime`, `engine/Engine.h:101`; the player asks of every engine it
plays, `Player.cpp:112` and at a swap `:271`): a clock read either side of the strip's work in each piece
(`engine/Engine.cpp:538`, an RAII stamp so every `continue` counts), the sums restarted each render (`:882`); the
player smooths each strip's sum over the block's duration as its share. A render is never timed (it has no deadline)
and its bytes do not depend on it. **The model** (`pump`, `core/service/ServiceTransport.cpp:220`): `transport.cpu`
(smoothed), `transport.cpuPeak` (the worst single block in the last second — unsmoothed, over 1 a missed deadline),
`strips[].cpu` (mapped engine index → strip, `:250`); stopped, the transport's model is reset (the held worst too,
`:53`). **Drawn**: the song bar's readout before the master meter (`SongBar::cpuRect`, `app/widgets/SongBar.cpp:92`):
"CPU" tracked, the percentage in mono, a thin bar under them, muted and the destructive colour from 80 % (`:310`); the
drawn value is a value catching up (`:133`), so a stop eases it down. The learn pill now ends before it. L2
`test_the_cpu_meter` (`core/tests/serviceTests.cpp:4198`): playing, a load in (0, 10) and a worst block kept; a
chord's synth costs more than an empty bus, and no strip more than the whole (≈ 2 % of a block against ≈ 0 here);
stopped, all 0; a render before and after playing, the same bytes. UI `test_the_cpu_in_the_song_bar`
(`app/tests/ui/uiTests.cpp:4581`): the readout caught on its way up and on its way down. The meters-playing shot shows
it. Mutants (each red): the load not over the block's duration, the strips never timed, a strip's sums never
restarted, the worst block never kept, the readout snapping. Found while building it: two survived first — "a strip's
sums never restarted" (the test's bound was loose: a strip's share is now held to the whole load's) and "the worst
never kept" (the model reported `max(worst, load)`, which hid it: it reports the worst itself now); a third — the
load not zeroed at a stop — survived because a stop already resets the transport's model: that zeroing was dead, and
is gone. The held worst's reset at a stop is not covered by a test.

### DR-PERF-2 A multi-core engine, byte-identical (R-PERF-2)
**What may run apart.** At build the engine marks the strips NOTHING feeds — no route, send or key into them
(`engine/Engine.cpp:189`): what a strip's own work reads (its clips or notes, its live keys and input, its instrument
and rack, its zeroed input and key buffers) no other strip writes. **The split.** A strip's work is two functions:
its OWN (`ownWork`, `:533` — source, instrument, rack, into its input) and its ROUTING (`routeStrip`, `:673` — sends
pre and post, fader and pan, keys, output, meters, stems). A piece (`:794`) first hands the free strips' own work to
the engine's `IParallel` (`engine/Engine.h:88`, `setParallel` `:103`; `ownWorkOf`, `:519`), then walks the strips in
processing order doing every remaining own work and ALL routing — so every sum is added in the order one thread adds
it, and the bytes are one thread's. The choice is the piece's own (a local; no mark outlives it). **The pool**
(`core/WorkerPool.cpp`): the caller takes items too (`run`, `:91`) and never waits to START — no worker, one item, or a
worker still leaving an old job (`:94`): it works alone; else it fills the job buffer no worker can be reading,
publishes it by an atomic pointer, bumps a generation, notifies (without the mutex — a worker that misses it costs that
piece its help), takes items, and waits only for the items handed out. Workers (`loop`, `:70`) count themselves in
before reading the job and take the caller's floating-point mode (`:83`). `threadsFor` (`:34`): auto = the cores but
one, at most 8. **Where.** The player makes the pool for the threads the setting gives (`core/Player.cpp:113`; from
`ServiceTransport.cpp:562`) and sets it on every engine it plays, a swapped one too (`:259`); a render makes one for
its engine (`core/service/ServiceRender.cpp:190`). `settings set threads=auto|1…16` (`ServiceMachine.cpp:164`; kept,
`core/Settings.cpp:71`; `settings.threads` in the model). Tests: engine `test_the_strips_nothing_feeds_render_in_any_order`
(`engine/tests/engineTests.cpp:744`: two synths, two kits and a shared reverb — the free strips run reversed and
interleaved by a runner with no threads at all: byte-identical to one thread; the bus and the reverb never free); L2
`test_the_engine_on_several_threads` (`core/tests/serviceTests.cpp:4233`: five instruments sending to a reverb — a
render on one thread and on four byte-identical; live playback on four the render, frame for frame; auto; refusals);
every existing live-equals-render test now runs with the pool (auto). Mutants (each red): a fed strip taken for free, the
free strips' own work never done, a free strip's own work done twice, a fed strip's own work skipped when the pool
runs, the caller not waiting for its items. Found while building it: per-piece marks that a "never cleared" mutant
could not fail — the pool ran every piece, so a stale mark could only bite if the pool were removed: the choice is a
local now; my own test iterated the model while its commands rebuilt it and crashed one run in two (fixed as D-15
was); and a ThreadSanitizer build of the service tests found D-16 in `clip duplicate`.

### DR-PERF-3 Freeze and bounce (R-PERF-3)
**A strip's own sound** (`renderOwnSound`, `core/service/ServiceAudioEdit.cpp:370`): a copy of the song holding only
the strip — at 0 dB, centred, unmuted, to a master at 0 dB, not frozen (`:385`) — its clips, its rack, no sends, and
only the bindings of its own devices (`:390`: its automation plays with it); compiled, its plugins given instances of
their own (R-VST-6), rendered by an engine of its own on the pool the setting gives (DR-PERF-2) from the start to the
song's end and on, until its master peak falls under −90 dB or 10 s have passed (`:414`), its output latency trimmed
(`:416`). **The commands** (`freezeCommand`, `:420`; rows `core/service/Command.cpp:84`–`:90`; dispatched
`core/service/SolarisService.cpp:199`): `strip freeze <ch>` refuses an unknown strip, one frozen already (`:434`) and
one anything feeds — an output, a send or a key into it (`:437`: its sound would not be its own) — and writes
`Frozen/<strip> <id>.wav` beside the song, 32-bit float (`:447`; a refreeze replaces it, its old decode dropped,
`:449`), the strip's `frozen` the path relative to the song; `strip unfreeze <ch>` clears it (the file stays, an undo
may want it; `:428` refuses a strip not frozen); `strip bounce <ch>` writes the same render through `writeBounce`
(`:349`, `clip consolidate`'s: `Bounces/<strip> <n>.wav`, the next free number) and adds it as a clip at beat 0 —
a new file, so a NEW strip (R-MIX-2; `:456`); the strip itself untouched. Each one edit (one undo). **Playing a frozen
strip** (`core/Compile.cpp:324`): an audio strip with one region, its file from the start; its rack is not built (its
devices cost nothing; their edits are kept and unheard); its fader, pan, mute, solo, sends and output as ever; the
song's end still its clips' ends (`:338`), not the file's tail; a file that cannot be read leaves it silent, said by
the compile (`:337`). **The file**: `frozen="…"` on its `#atrack` line (`model/Project.cpp:269`, written `:797`).
**The model**: `strips[].frozen` (`core/service/ServiceModel.cpp:272`); `audit` names each frozen strip and the file it
plays (`:578`). Test: L2 `test_freeze_and_bounce` (`core/tests/serviceTests.cpp:4798`: an instrument through a chorus
and a lookahead limiter, faded, panned and sending to a reverb bus — frozen, the song renders within 3·10⁻⁸ of its own
work; a device edit unheard and in the audit; its fader live; unfrozen, the edit heard; undone, frozen again; refrozen,
the new file heard; refusals; a bounce onto a new strip; a file made unreadable opens silent and said). Mutants (each
red): compile ignoring `frozen`; the render keeping the strip's fader, or its pan; no tail; a strip fed by a send
frozen; unfreeze keeping the file; frozen twice; the bounce onto the strip itself; the audit silent; the latency not
trimmed; a refreeze's stale decode kept; the unreadable file unsaid. Found while building it: the unreadable-file
warning bound to the clip loop's `if` (a dangling `else`) — every other strip's clip said "cannot be read"; the
refusal for an unsaved song could never fire (an open song always has its file) and is gone; consolidate's naming
became `writeBounce`, one copy for both. **Drawn** (part 2): a strip's menu in the mixer
(`app/widgets/MixerDock.cpp:1274`) — "Unfreeze" on a frozen strip; "Freeze" (`:1275`) and "Bounce to a new strip"
(`:1276`) on a strip nothing feeds, read from the model's `fromStrips` (outputs, sends and keys — the service's rule);
each one line. A frozen strip's eased amount (`:651`, `frozenAmount` `:419`, the cross-fade's time) dims its rack
(`:1387`) under a chip-sized FROZEN pill in the accent (`:1412`, its word ellipsized on a narrow card). Test: UI
`test_a_strip_freezes_from_its_menu` (`app/tests/ui/uiTests.cpp:4461`: a fed bus offers neither; Freeze sends its line,
the amount caught mid-ease both ways; Unfreeze; Bounce makes a strip). Shots: mixer-frozen, mixer-frozen-mid. Mutants
(each red): the look snapping; never advancing; Freeze, or Bounce, offered on a fed strip; Unfreeze never offered;
Freeze sending the wrong line. Found while testing it: Escape closes a context menu only while renaming, so the Duck
Under test's last check ("no such item") passed whether or not its menu opened — it now asserts the menu opened and
closes it by a click away.

