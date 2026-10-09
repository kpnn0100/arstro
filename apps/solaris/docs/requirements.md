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
(`model/Project.cpp:346`) reads the suite grammar: header `key = value`, node lines, indented
continuation lines, whole-line and inline `;` comments (kept with the node they follow), unknown
keys (kept in order) and unknown nodes (kept verbatim with their indented lines). The suite's
inline `#note`s under an `#aclip` become a pattern of their own — the one normalisation, reported.
`serializeProject` (`model/Project.cpp:636`) writes §10's canonical form: `canonicalNumber`
(`model/Format.cpp:33`, shortest round-trip, always a point), `canonicalBeats` (`:52`, rounded to
1/960 beat, fewest decimals that read back to the tick), seconds to the microsecond; defaults of
optional fields are omitted. **Parse → serialize is a byte-exact fixed point** for canonical text.
Refused, each naming what and where (`validateProject`, `model/Project.cpp:824`): duplicate ids,
`master` as an id, unknown strip kinds, dangling references, an audio clip on a non-audio strip, a
note clip on a non-instrument strip, `in ≥ out`, a clip before the song, two racks for one strip,
an input port as a destination, a header that is not `app = solaris` / `timebase = beats` /
`ppq = 960`. Repaired and counted (`Reader::num`, `:188`): a non-finite or unreadable number → the
field's default; out-of-range pan, pitch and velocity clamp. Guarded by `solaris_model` (8 tests;
mutants checked: dropping unknown keys on write breaks the fixed point).

### DR-MIX-4 Routing only goes forward (R-MIX-4)
`validateProject`'s `checkTarget` (`model/Project.cpp:841`) accepts an `out` or a send target only when
it is `master`, an output port, or a strip `feedsForward` (`model/Project.cpp:766`) allows — one LATER IN
PROCESSING ORDER: on a later mixer, or after it on its own (strip order, then the file's order on a
tie) — the ONE copy of the rule, for outputs, sends and sidechain keys alike; anything else is refused
naming both ends, as `ch_1 (Main, on Buses) output → ch_2 (kick, on Sources): a strip can only feed a
strip LATER in processing order (a later mixer, or after it on its own), the master or a port (R-MIX-4)`.
A file that routes backward does not load. Because of this, `Project::stripsInOrder`
(`model/Project.cpp:119`) — mixer order, then strip order — is a topological order of the routing
graph with no cycle check at all. (**AMENDED (audit, 2026-10-09, R-MIX-4 amended):** `feedsForward` was
"a later MIXER", so a bus made on Buses could not feed Main and every return skipped it.) A reorder
that would point a route backward — `strip move`, `mixer move` — is refused by the same validator
after the command. A new strip whose output is a strip on its OWN mixer is placed just before it
(`placeBeforeItsTarget`, `core/service/ServiceEdit.cpp:72`), so `strip add --kind bus` on Buses feeds
Main: its default output is the bus that LEAVES the nearest later mixer with buses, else the one
leaving its own (`defaultOutFor`, `:100`). `strip move --order <n>` puts the strip at position n of its
mixer and renumbers the mixer 0…n−1 (`placeStrip`, `:60`; `case K::StripMove`, `:266`), as `mixer move`
renumbers mixers — an order never ties. A new project is `newProject` (`model/Project.cpp:157`): Sources (`mx_1`), Buses (`mx_2`)
holding the bus Main (`ch_1` → master), the port Main (`prt_1`) fed by the master (R-MIX-3).
Guarded by `test_routing_only_goes_forward` and `test_forward_is_processing_order_and_moves_renumber`
(`core/tests/serviceTests.cpp:1786`: a bus made on Buses goes before Main and feeds it; a same-mixer route
forward is taken, backward refused naming both ends; `strip move` that would point one back refused;
`--order 0` first and renumbered; mutant checked: the old later-mixer rule refuses the new bus's
default route). The same rule is PUBLISHED: `targetsOf` (`model/Project.cpp:797`) lists what a strip
may feed — later strips in processing order, `master`, the output ports — as `strips[].targets`
(`core/service/ServiceModel.cpp:214`), and `keyTargetsOf` (`:756`) its strips as `strips[].keyTargets`;
a front end offers exactly that list (the route picker, `MixerDock::openOut`, `app/widgets/MixerDock.cpp:785`),
and the test routes to every entry offered.

### DR-ENG-1 The engine renders a MixGraph through the DSP library's devices (R-MIX-1/5/6, R-DSP-1/5, R-RENDER-1)
`solaris_engine` knows no project and no file: it renders a `MixGraph` (`engine/MixGraph.h`) —
strips in processing order with their devices (registry types + values), audio regions in samples
over decoded PCM, note events in samples, an output and sends as forward indices, the master's rack
and gain, the ports. `Engine::build` (`engine/Engine.cpp:89`) refuses a target that is not LATER
(R-MIX-4, a second time — the graph might not come from the model), a missing port, an unknown
device type or parameter, an instrument strip whose rack does not start with an instrument; sets
the DSP library's process-wide sample rate (R-NFR-7); builds every device with
`DeviceRegistry::create`; and **warms** each with one block of silence (`warm`, `:64`), because the
library smooths every parameter write over a block and the first block of every render would
otherwise carry a ramp from the device's default; and sizes every latency-compensation delay
(`compensate`, `:198`, DR-MIX-17). `renderPiece` (`:436`) per strip in order: sum
what earlier strips routed/sent to it; add its regions (looped, offset, offline = silent) or play
its instrument with the block **split at every note event** (`:488`, R-DSP-5); run the rest of its
rack (bypass skips); tap pre-fader sends; fader + balance pan; post-fader sends; add into its
output (`route`, `:407` — master, a later strip, or a port, each through its connection's delay; a mono port gets the average of L and
R). A silent strip (muted or solo-silenced, decided by the core) sends nothing anywhere (`:546`).
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
with the grammar TABLE (`commandSpecs`, `core/service/Command.cpp:26`; `parseCommand`, `:309` —
longest verb match, flags checked against the row) and `dispatch` (`core/service/SolarisService.cpp:120`)
runs it; out come the `AppModel` (`core/service/AppModel.h`, refreshed after every command) and
`Event`s whose `formatEvent` text is the log line. **Every edit is all-or-nothing**: the project is
copied first, the command runs, `validateProject` runs, and any failure restores the copy
(`:223`) and refuses with the validator's sentence — a refused command changes nothing. An edit's
events (`project.changed` from `changed`, `:35`; a `set` line's `params.changed`) are HELD in
`mPending` and emitted only once the whole command has landed (`:278`), so a refused command
announces nothing either (D-1). An edit that made nodes prints the id it made first and `made:` with
the rest (DR-SVC-8). And a refusal always says WHY: a path that returns false without a
reason is caught in `dispatch` (`:206`) and named (`` `clip add` was refused without a reason``),
never silent (R-SVC-3) — B1's own wiring mistake showed the hole. The core
holds no codec and no device API: decoding and WAV writing are `Host` functions (R-SVC-4).

### DR-SVC-2 Unknown input is refused, naming it (R-SVC-3)
An unknown verb or flag fails in `parseCommand` with the nearest candidates (`strp add` →
`did you mean: strip add?`). An address goes through `setAddress` (`core/service/SolarisService.cpp:525`):
an unknown node, field or DEVICE PARAMETER is refused with the nearest name (`set
dv_1.filter.cutof=1` → `did you mean: filter.cutoff?`); a value out of range is refused with the
range; a device parameter's value is read against the DSP registry — a choice by name or index (listing
the choices on a miss), a number inside the spec's range (`:659`; **amended, R-SVC-8, 2026-10-09**: it was
clamped in silence, now a value outside is REFUSED naming the range with its unit, and a non-whole value
of an integer parameter is refused) — and stored as its canonical text. `get` (`getAddress`, `:752`) reads every field `set` writes plus derived ones (`<clip>.length` resolved, `<strip>.out`,
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
`clip add --src` (`core/service/ServiceEdit.cpp:548`): a file no clip uses yet gets a new audio strip
named after it on the first mixer, routed by `defaultOutFor` (`:100`) to "Main" — the bus that leaves the
nearest later mixer with buses — and, having no clips, a new lane; a file already used reuses its strip and, with no
`--lane`, its lane (`laneFor`, `:529`; **amended, R-SVC-8, 2026-10-09**: every clip got a new lane);
`--strip` overrides. The file
is decoded through the host to learn its length (refused if it cannot be read), and stored relative
to the song's folder when inside it (`relativePath`). A relative `--src` names a file in the SONG's
folder first when one is there (`:518`) — what the model stores and the browser's Song tab hands
back — else the caller's working directory.

### DR-MIX-7 Solo keeps the soloed path alive (R-MIX-7)
`silentStrips` (`core/Compile.cpp:34`): with any strip soloed, a strip is audible only if it is
soloed, reachable downstream of one (its buses, its sends' returns) or upstream of one (what feeds
it); muted strips are silent regardless. The model shows it as `strips[].audible`; the engine gets
it as `silent`.

### DR-MIX-8/9/10 Fed by, the matrix, the audit (R-MIX-8, R-MIX-9, R-MIX-10)
`refreshModel` (`core/service/ServiceModel.cpp:78`) computes each strip's `clipCount`, `fromLanes`
and `fromStrips`. `matrix print [--json]` (`matrixText`, `:491`): rows = strips in processing order,
columns = `matrixColumnsOf` (`model/Project.cpp:807`) — every strip some strip may reach or does reach
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
`strip relink <ch> --to <ch2>` (`core/service/ServiceEdit.cpp:305`) moves EVERY clip playing through
`<ch>` to `<ch2>` in one edit (one undo step, "strip relink <ch>"), printing how many moved; refused
onto a bus ("a bus plays no clips"), across kinds (audio clips need an audio strip, note clips an
instrument strip), onto itself, and when `<ch>` has no clips. A pattern's `strip` follows its first
clip. In the dock (`app/widgets/MixerDock.cpp`) every mixer page ends with "+ Line" after its last
card — placed from the LIVE card widths, so it slides as cards grow in and shrink out
(`MixerDock::addLineRect`, `MixerDock.cpp:357`; drawn at `:1379`) — whose menu is an audio line, a bus
and every instrument of the registry, each ONE `strip add --kind … [--instrument <type>] --mixer
<mx>` (`MixerDock::openAddLine`, `MixerDock.cpp:796`); the new card grows in. A strip's menu offers
"Move its clips to ▸" when it has clips and another strip of its kind exists — a second menu of those
strips → `strip relink` (`MixerDock.cpp:1083`). On the lanes a clip's menu (`Timeline.cpp:727`) offers
"Play through ▸" — the strips of its kind, the current one marked "now" → `clip move <ac> --strip
<ch>` — then Piano Roll (a note clip), Duplicate, Delete.

### DR-CLIP-2/3 Patterns and linked clips (R-CLIP-2, R-CLIP-3)
`compile` (`core/Compile.cpp:125`) expands a note clip: a clip longer than its pattern loops it, a
note is cut at the clip's end (`(C1)`, `:216`); events sort by time with note-offs before note-ons at
one sample (`(C2)`, `:230`). `clip duplicate` makes a second clip of the SAME pattern (`linked` = 2 in
the model) — `--count N` makes N, end to end, one edit (DR-SVC-8); `clip unique` copies the pattern.
`note add` replaces a note at the same pitch and tick; so does each note of `notes add`.

### DR-RENDER-1 Offline render (R-RENDER-1, R-RENDER-2, R-RENDER-3)
`render --out <file.wav>` (`core/service/ServiceRender.cpp:33`; `--out` is required — the grammar row says so,
`CommandSpec::required`, `core/service/Command.cpp:167`, and a line without it is refused at parse):
compile → build the engine → one pass capturing the
master bus (the mixdown, `--out`), any `--stems` (strips' post-fader outputs, `<out>.<ch>.wav`; `all` = every
strip, `:51`) and
with `--ports` every output port (`<out>.<port>.wav`); 24-bit PCM or `--bits 32f`; `--from`/`--to`
in beats (an explicit `--to` is exact). With no `--to` the tail runs past the song's end until the
first block the master spends below −90 dBFS, capped at 10 s (`:107`). Every file starts at `--from`
EXACTLY (R-MIX-17): the engine's `outputLatency()` more is rendered at the end and dropped at the start —
the mixdown and the ports by it, a stem by its own strip's latency (`:80`, `trim`, `:112`). The host writes the files
(`writeWav`, `host/AudioFiles.cpp:116`) and decodes sources through FFmpeg, resampled to the project
rate, mono at unity in both channels (`decodeAudio`, `:24`). Two renders of one song are
byte-identical (`test_render_writes_the_mix_deterministically`).

### DR-API-1 The API document is generated, committed and drift-tested (R-API-1)
`apiJson` / `apiMarkdown` (`core/service/ApiDoc.cpp:44`, `:100`) print, from the tables the code
runs on, every command (usage, summary, the R- tag that asked for it), every event with its fields,
every `AppModel` field (with `stable`), **every DSP registry device with every parameter's unit,
range, default and choices**, a kit's pads by name (`:80`), and the Notation — pitches, the note token,
step rows, every chord quality (`:127`, from `chordQualities()`, R-SVC-8). `docs/api.json` and `docs/API.md` are that output, committed;
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
`outputLatency`, `Player::heard`, `core/Player.cpp:64`, R-MIX-17), the latency and the peaks into the
model; the metronome clicks on the beat the music is on (`:223`), the bound values read are those of
the heard block (`:128`). A live `set` that moves a device's latency (a limiter's lookahead) builds a
new engine instead of a message (`latencyOf`, `core/service/ServiceTransport.cpp:63`, checked at `:312`),
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
(`keepLive`, `core/Player.cpp:105`), and the service suite counts allocations on every thread but its own
while a song with bound gains, a link and a device formula plays (`test_a_bound_value_is_published_live`: 0;
mutant: a copy of `bindValues()` per block → 1019).

### DR-PLAY-3 Meters in the model (R-PLAY-3, partly)
While playing, `pump()` copies the last block's peaks into `strips[].peak` and
`transport.masterPeak` (L/R, linear) — excluded from the stable dump. RMS, peak hold and the clip
latch are the UI's to draw from these numbers (U3).

### DR-UI-1 The window, Home and the song bar (R-UI-1, R-UI-2, R-UI-3, R-HOME-1, R-G-4)
`solaris_app` (`app/`) is a platform-free Artboard tree that links no service: `App` (`app/App.h`)
draws from `AppHooks::model()` and sends text lines through `AppHooks::dispatch` (`App::dispatch`,
`app/App.cpp:209` — a refusal becomes a toast with the service's sentence). `linux_main.cpp` is the
only OS code: GTK3, the frame clock, the pickers, the host functions, the service pumped every tick.
The screen follows `AppModel::screen` and CROSS-FADES (260 ms, `App::render`, `app/App.cpp:334`).
`installSolarisAccent()` moves cosmo's accent slot to teal `#159387` before any widget exists
(`app/Theme.h`); every neutral, radius, font and spacing token is cosmo's, aliased. `HomeScreen`
(`app/widgets/HomeScreen.cpp`): the `solaris.` wordmark sized to fit, New/Open, Settings, recent-song
cards (name, `bpm · length · strips` over a plate of bars from the song's name; a moved song says
"missing" in red and is named by its file) whose grid geometry eases on reflow (`layout`, `:86`);
right-click forgets a card. `SongBar` (`app/widgets/SongBar.cpp`): wordmark, Home (asking first with
cosmo's `ConfirmDialog` when unsaved, `App::requestHome`, `:124`), the song's name with an eased
unsaved dot, Play/Stop whose glyphs cross-fade, the position as bar.beat.tick in mono, the tempo,
the master meter (40 ms rise, 300 ms fall, `advance`, `:58`), Save, Settings. Keys: Space play/stop,
Ctrl+S save, Enter to the start, Ctrl+, settings. Every control's command line is in `app/NOTES.md`.

### DR-SET-2 The settings sheet (R-SET-1, amended)
`SettingsSheet` (`app/widgets/SettingsSheet.cpp`) in cosmo's modal style: the scrim, the popover
card, uppercase labels tracked +0.13 with a plain note, chips that size to their MEASURED text and
wrap, a filled accent when chosen — the fill EASES over 200 ms whoever chose it (`advance`, `:151`) —
the folder list with × per folder and "Add folder…", Done; 150 ms open / 120 ms close; Escape, Enter,
Done and the scrim close it; the card scrolls when the window is short. Opening sends `devices list`
first (`App::openSettings`, `app/App.cpp:228`). Every chip is `settings set …`, every folder control
`folder add|remove` (`handleGesture`, `:206`); the sheet draws the model, so a setting changed from a
shell shows at once.

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
to the visible edge when it begins off-screen (`paintClip`, `:802`); a note clip draws its
pattern's notes repeated where it loops, the seams marked, and "linked ×N" when it shares its
pattern. Ruler: bars from 1, the grid's ticks, every label faded in by its room (DR-UI-10). The
playhead is `destructive`; it follows the transport while playing and eases 140 ms on a seek.
Ctrl+wheel zooms ×1.25 a notch on a lattice about 28 px/beat — 4.7 to 637 px/beat — eased 220 ms
and anchored at the pointer (`:618`); the wheel scrolls, Shift+wheel sideways. The grid follows the
zoom and its finest level with room is THE snap step on the lanes (DR-UI-10). A clip dragged follows
the pointer exactly, on that step, between LANES only, and lands as `clip move <ac> --at <b> [--lane
<ln>]` (`:555`) where it was let go. A click selects (the teal ring cross-fades 200 ms between
clips); Delete/Backspace → `clip delete`, Ctrl+D → `clip duplicate` (`app/App.cpp:326`); a ruler
click → `transport seek <b>` on the grid you see (`Timeline.cpp:718`). A lane moved by `lane move` (DR-LANE-1)
slides to its new row through the same keyed rows. **The picture travels
(§1):** the rows are Interstellar's `AnimatedRows` keyed by lane; each clip keeps an eased beat, row
and opacity keyed by its id (`Timeline::advance`, `Timeline.cpp:329`) — it fades in when it arrives, fades out where it
was when it goes (taking no input), eases 200 ms with its row when moved from a shell; the zebra
follows the LIVE slot, a stripe's colour cross-fades, the empty-state words fade. Another song
places everything where it is. Empty, it says what to do in words.

### DR-BROWSE-2 The browser and drag and drop (R-BROWSE-1 amended, R-BROWSE-3)
`Browser` (`app/widgets/Browser.cpp`): tabs **Samples · Instruments · Song** under a highlight that
slides 220 ms. Samples lists the folders from Settings; a folder clicked is `browse "<path>"`
(`:216`) and shows its sub-folders and audio files (mono, the filename rule) under a row back up;
with no folders it says so and a click opens Settings. Instruments lists `AppModel::deviceTypes`
(the DSP registry, `core/service/ServiceModel.cpp:114`) — instruments, then effects. Song lists the
files the song plays. The list is `AnimatedRows` keyed by generation and content (`rebuild`, `:54`):
a tab or folder changed starts a new generation, so the old list fades where it was scrolled while
the new one fades in (`navigate`, `:96`); an inserted row fades in, a removed one out. A row is
dragged out: the browser reports the pointer and the drop; `ProjectScreen` draws the ghost (the
overlay pass) and the timeline's teal drop hint at the beat on the lanes' step (DR-UI-10; `overLanes`,
`app/widgets/ProjectScreen.cpp:95`), and on release sends ONE line (`place`, `:104`): a sample → `clip add --src "<file>" --at <b> [--lane
<ln>]`; an instrument → `clip add --instrument <type> --at <b> --length 4 [--lane <ln>]` (the new
strip and its empty note clip in one command, `core/service/ServiceEdit.cpp:571`); below the last
lane, `--lane new` — a new lane (`app/widgets/ProjectScreen.cpp:109`; **amended, R-SVC-8, 2026-10-09**: it sent no `--lane`, which
now means "its strip's lane", so the drop says what it wants); an effect → a notice that it goes on a strip (U3). A double-click
places at the playhead.

### DR-UI-7 A strip's colour (R-UI-7)
The model's `strips[].colour` is resolved by the service (`core/service/ServiceModel.cpp:186`): the
strip's own, else its id's number − 1 — never −1, and unchanged when other strips are added or
deleted. Every front end draws it as is.

### DR-UI-8 The mixer dock (R-UI-3, R-MIX-1/5/6/7/9/12, R-MIX-12 amended)
`MixerDock` (`app/widgets/MixerDock.cpp`) sits under the lanes (`ProjectScreen::dockTarget`,
`app/widgets/ProjectScreen.cpp:129`: 429 px wanted; its top edge dragged follows the pointer; the
chevron folds it to its tab bar, eased 220 ms; it gives way before the lanes, which keep 130 px).
Tabs: a mixer page each, "+" (`mixer add`), Matrix; keyed (`syncTabs`) so a tab added slides the
others along, measured in one weight so choosing a tab moves nothing; the highlight slides, the
pages cross-fade as layers. A page (`syncCards`, `:189`) is a card per strip in processing order,
the strips feeding one bus (two or more) gathered under its header; the master pinned right. A card:
colour, name, "audio · N clips" / "bus · fed by N"; rack chips (four slots: the devices and "+
Effect", or two, "+N more", "+ Effect"); two sends (→ name, dB, P = pre; a third says "in the
matrix"); pan; the fader (`faderPos`, `:91`: gain ∝ position², 0 dB at 0.708, +6 at the top) beside
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
(`build`, `:225`), kept — a row per parameter grouped by the name's prefix (`osc1.*` → OSC1): a number
is cosmo's `SliderRow` with the opt-in `formatValue` (Hz/kHz, ms, dB, st/ct/oct signed, a 0…1 amount in
%), a `logScale` parameter through a log taper, an `integer` one rounded; a choice is a row stepping
its names. It is the parameter LIST: the value column says what decides each row (`bindingText`,
`app/widgets/ParamMenu.cpp:33`) — its number, `auto au_1`, `= ch_2.gain`, `= <formula>` cut to the column — and the row
`DeviceModel::lastChanged` names is lit, the light easing from row to row (`ParamBody`, `:75`). A
right-click offers Create Automation, Formula… (cosmo's rename field), Clear Binding, Reset to Default
(`openParamMenu`, `:443`) — **AMENDED (R-MIX-16, R-UI-11, 2026-10-09):** built by the shared `ParamMenu`
(`paramMenuItems`, `:470`), the same menu the dock's numbers offer (DR-MIX-16), with Copy Address / Copy
Value (a bound row: its live value) / Copy as Formula (a choice row: no "as Formula", no binding items);
with View › Show IDs each row's full address fades in over its label (`ParamIds`, `:179`, DR-UI-11). The
App offers right-clicks to the windows first because a row's slider would
swallow them (`app/App.cpp:74`). Every change is one line: `set <dv>.<param>=…`, `auto create`,
`bind clear`, `set <dv>.bypass=…`, `device remove <dv>` (none for an instrument — it has "Piano Roll"
instead, DR-ROLL-1). While the pointer is
down `bind` re-seeds nothing (`:291`); the body scrolls with its own bar (`reveal` eases to a row).

### DR-AUTO-1 Automations and bindings in the `.slp` (R-AUTO-1, R-AUTO-4)
`Automation` (`model/Project.h`): id `au_n`, name, unit, min/max, `from`, points (beat, value, shape
`linear|hold|smooth|bezier`, sorted; a bezier point's handles — DR-AUTO-6). `Binding`: address +
formula (with its `=`), one per address. Written after the clips as `#aauto` with `#point` children
and `#abind` nodes (`serializeProject`); read by `parseProject` (`model/Project.cpp:436`), which also
reads the suite schema's earlier sketch — indented `<beats> = <value>` lines and
`node=/param=/interp=` — and normalises it once (`:484`). `validateProject` (`:884`) refuses two
bindings on one address, a formula without `=`, a binding on something that does not exist, an empty
range, an unknown shape and a bezier influence outside 0 < x ≤ 100 % (`:910`).

### DR-AUTO-2 Formulas: parsed, resolved, ordered (R-AUTO-2, R-AUTO-3)
`parseFormula` (`core/Formula.cpp:205`): recursive descent straight to the engine's postfix
`Expr`; numbers, `+ − * / ^` (right-assoc.), unary minus, parentheses, 17 functions with their
arities checked, `pi`, the clock words `beat bar bpm t`; every other name is a symbol, listed in first
use; errors name the text and its column; deeper than the evaluator's stack is refused.
`describeAddress` (`core/Bindings.cpp:11`) is the one answer to "is this a number a formula can
drive, and what are its range, unit, label and owner": a strip's gain (dB, −120…12) and pan, a send's
gain, `project.masterGain`, every numeric registry parameter (a choice or a switch is refused, saying
so). `orderBindings` (`:91`) resolves each symbol to an automation or a numeric address and orders the
bindings (Kahn, file order on ties) so a link reads one evaluated before it; a loop is reported naming
its members; an unreadable binding is left out — INERT, its own value plays. `checkBinding` (`:149`)
answers for one candidate beside the rest; `set` refuses on its answer.

### DR-AUTO-3 The engine evaluates them every 64 samples (R-AUTO-7)
`compile` (`core/Compile.cpp:251`) turns automations into `engine::Curve`s (`compileCurve`, `:330`:
Interstellar's keyframes in samples, speeds per sample, ranged as the automation — DR-AUTO-6) and
bindings into `engine::Bind`s — target (strip gain/pan, send gain, master gain, a device parameter by
registry index), clamp, integer, own value — with each symbol remapped: an automation → its curve's
slot, a bound address → its binding's slot, an unbound address → its own value as a constant.
`evaluateBinds` (`engine/Expr.h:217`) fills the clock, the curves (`Curve::valueAt`, `:181`, Interstellar's
`anim::eval`, clamped to the automation's range) and each
binding in order (`Expr::eval`, `:59`, a fixed stack, no allocation; a non-finite result keeps the last
good value). `Engine::render` (`engine/Engine.cpp:692`) cuts pieces at every multiple of `kControl` = 64
samples and evaluates there (`evalBinds`, `:286`), so the result is the same however time is chopped;
bound gains, pans and sends ramp linearly across the period, unbound strips render exactly as before.
Values at time zero are applied before the devices' warm-up (`:187`); a seek evaluates at once
(`:355`). Measured: a 0 → −20 dB curve renders −5.00 dB at beat 1 and a formula-stepped compressor makeup
+12 dB from beat 2, byte-identical at chunks of 77, 128 and 1000, no zipper, no allocation.

### DR-AUTO-4 The service: commands, `set`, `eval`, the model (R-AUTO-1…5, 8, 9)
`set <address>="=<formula>"` binds (`core/service/SolarisService.cpp:539`); a plain number clears the
binding and sets the value; `get` prints the formula; an unquoted formula with spaces is refused with
the quoting shown. `auto create <address>` (`core/service/ServiceAuto.cpp:125`) makes `au_n` named
"<owner> · <label>", ranged as the address, holding its value from beat 0 to the song's end (at least
four bars), and binds the address to `=au_n`; `auto add|delete` (refused while read, `--unbind`),
`auto point add|move|delete|shape` (a value outside the range is refused naming it, `inAutoRange`, `core/service/ServiceAuto.cpp:69` —
**amended, R-SVC-8, 2026-10-09**: it was clamped; `shape` takes a bezier point's handles, DR-AUTO-6), `bind clear`; deleting a strip, a
send or a device drops the bindings that drive them. `eval <address> [--at] [--explain]`
(`:261`) compiles and evaluates with the engine's own function and prints each name it reads.
`refreshModel` (`core/service/ServiceModel.cpp:277`) publishes `bindings[]` (reads, ok, problem),
`automations[]` (points with their handles, usedBy, `now` — DR-AUTO-7), `params[].formula`,
`strips[].gainFormula/panFormula`, `sends[].gainFormula`, `masterGainFormula`, `devices[].lastChanged` and — **AMENDED (R-MIX-16, 2026-10-09)** —
`bindings[].live`, each binding's value at the heard position (`evaluateLive`, `:353`; DR-MIX-16); `audit` (`:345`) names inert
bindings and automations no formula reads. While playing, a binding or curve edit — or a `set` on an
address a formula reads — swaps in a new engine (`core/service/ServiceTransport.cpp:267`).

### DR-EDM-1 Undo and redo (R-EDM-1)
After every edit that LANDED, `dispatch` keeps the song as it was before it (`core/service/SolarisService.cpp:281`),
at most 200 steps; consecutive `set`s of exactly the same addresses extend the newest step instead of
adding one, so a dragged fader is one step (a rule a script sees the same way). `undo` / `redo`
(`historyCommand`, `:328`) swap the song with the step, print its label (`set ch_2.gain`, `clip add`,
`strip delete ch_3`), emit `project.changed what=undo|redo node=<label>`, mark the song unsaved and,
while playing, swap in a new engine; any new edit clears redo. Not edits: machine settings, the
transport, a save, `get`/`eval`/`audit`. `project new|open|close` start a new history. The model names
both (`undoLabel`, `redoLabel`, `undoDepth`, `redoDepth`); the window's Ctrl+Z, Ctrl+Shift+Z and Ctrl+Y
send `undo` / `redo` (`app/App.cpp`).

### DR-UI-9 The song bar's menus and Settings beside Home (R-UI-3 amended)
`SongBar` (`app/widgets/SongBar.cpp`): wordmark · Home · Settings · cosmo's `MenuStrip` (laid out by
`SongBar::layout`, `:31`) · the song's name; the transport is centred when there is room and otherwise
starts right of the menus and `kNameMin` (120 px) of name (`transportX`, `:40`), so the bar fits at
1024; the master meter and Save on the right. `App::buildMenus` (`app/App.cpp:88`): **File** — New
Song…, Open…, Save, Save As…, Render…, Render Stems… (the host's save picker, `onPickSave`, then
`project save "<path>"`, `render --out "<path>" [--stems <every strip>]`), Home; **Edit** — Undo / Redo
naming what they would take back (from `undoLabel`/`redoLabel`), Duplicate / Delete the selected clip;
**Song** — Add Mixer, Add Bus, Add Audio Line, Add Lane, Quantize Clip (the selected note clip's pattern at the
lanes' snap step, 1/32 of a beat when nothing snaps: ONE line, `pattern quantize <pt> --grid <step>`,
`app/App.cpp:119`); **View** — Hide/Show Mixer (the dock eases
to its tab bar), Hide/Show Browser (its width eases to 0, everything right of it follows the live
width), Metronome On/Off (`settings set metronome=…`), Show/Hide IDs (`settings set showIds=…`, R-UI-11,
`app/App.cpp:158`), Settings…. Labels follow the state
(`refreshMenus`, `:137`). A press outside an open menu closes it first (`:279`), as cosmo's does.

### DR-SET-3 The settings sheet in sections (R-SET-3)
`SettingsSheet` (`app/widgets/SettingsSheet.cpp:29`): **Audio** (Output, Input, Sample rate, Buffer),
**Playback** (Metronome Off/On, Click level −18/−12/−6/0 dB), **New songs** (Tempo 100…174, Meter
4/4 3/4 6/8 7/8), **Sample folders**, **Interface** (Reduced motion) — a 12 px title per section, a
hairline between; every chip `settings set <key>=…`. The service keeps the new keys in the machine's
file (`metronome`, `metronomeLevel`, `newBpm`, `newSig`, `reducedMotion`, and `showIds` — R-UI-11, set from
View, not the sheet — `core/Settings.cpp`),
`project new` starts at `newBpm`/`newSig` when no flag says otherwise, and the App ORs `reducedMotion`
with the OS's own setting into `artboard::setReducedMotion` (design rule §2.6). The sheet scrolls when
it is taller than the window (R6); `revealRect` brings a control in.

### DR-WIN-1 Windows inside the song view (R-WIN-1, R-WIN-4)
`FloatWindow` (`app/widgets/FloatWindow.cpp`): a frame — a 26 px title bar (title, ×) over its content.
Dragged by the title it follows the pointer exactly, kept inside the layer; touched anywhere it is
raised; `open`/`close` record intent and `advance` fades it (150 / 120 ms); while closing it takes no
input (`handleGesture`, `:55`). `WindowLayer` covers the song view under the song bar (over the lanes,
dock and browser) and keeps windows keyed — `dev:<dv>` (`openDevice`, `:127`: a new one cascades from
the top right), `roll:<pt>` (`openRoll`, `:150`, DR-ROLL-1) and `auto:<au>` (`openAutomation`, `:167`, DR-AUTO-7) — reopening a closed one where it was; titles follow the model ("Basic Synth — Bass")
and a window whose device is removed by anyone closes (`bind`, `:221`). Rack chips and an instrument
strip's name (double-click) in the dock open them; a chip is outlined while its window is open.
Placement and stacking are the view's. The dock's floating panel is gone (R-UI-5 amended).

### DR-AUTO-5 Automation on the timeline (R-AUTO-6)
`Timeline` (`app/widgets/Timeline.cpp:175`) appends a row per automation after the lanes, keyed
`auto:<au>` in the same `AnimatedRows` (a lane added slides them down; a new one grows in), its header
the accent stripe, its name and the address that reads it. `TimelineAuto.cpp` draws the curve over the
beat grid by the ENGINE's keys (`keysOf`, `:44` → `engine::curveKeys`; `paintCurve`, `:296`, samples
each non-linear segment with `anim::segment`) — holding the first value before the first point and the
last after, linear / hold / smooth / bezier per segment, on a log scale for Hz and ms (`valueToY`,
`:107`) — with the points as handles and a bezier point's two handle stems (`paintAutomation`, `:347`).
`autoGesture` (`:389`): a click on the row adds a point at the
lanes' snap step under the pointer (DR-UI-10; `:496`) with the value under it (`auto point add`) once the double-click has had its chance — 350 ms, a ghost point
drawn at once and fading in meanwhile (`:503`, sent by `advanceAuto`, `:268`; **AMENDED (R-AUTO-11)**:
a double-click there opens the window instead); a point dragged is drawn under the pointer while held
(its beat on the same step, `:424`) and sends ONE `auto point move <au> --at <from> --to <to> --value <v>` on release,
staying where it was let go; a double-click ON a point deletes it; a right-click offers Linear / Hold /
Smooth / Bezier, Delete Point and Delete Automation (`auto delete --unbind`). A curve the model changes
eases there point by point over 220 ms — handles too; a point added or removed, or a shape changed,
cross-fades the two curves (`shownPoints`, `:84`).

### DR-ROLL-1 The piano roll (R-ROLL-1…5, R-EDM-6)
**Grammar.** `note move <pt> --pitch <p> --at <b> [--to-pitch <p>] [--to-at <b>] [--length <b>] [--vel
<1…127>]` (`core/service/ServiceEdit.cpp:815`; a pitch by number, name or pad, DR-SVC-8) edits the one note at that pitch and beat — a moved note
landing on another's place replaces it; `pattern quantize <pt> [--grid <b>] [--swing <0…0.75>]`
(`:687`) moves every start to `k·grid`, odd `k` delayed by `swing·grid`, two notes landing together
merging into the louder. Both are edits (undoable, all-or-nothing). The model gives a pattern the strip
its first clip plays through and that strip's instrument (`patterns[].strip`, `.instrument`), and a
device type its named keys (`deviceTypes[].noteNames`, `core/service/ServiceModel.cpp:117`) — the DSP
registry's (REQ-device-6: the Drum Machine's ten pads, Kick = 36 …).
**The window.** `PianoRoll` (`app/widgets/PianoRoll.cpp`) is the content of a `roll:<pt>` window
(`WindowLayer::openRoll`, `app/widgets/FloatWindow.cpp:150`), opened by double-clicking a note clip
(`Timeline.cpp:723`) or by an instrument window's "Piano Roll" (`DevicePanel.cpp:492` — the strip's
pattern; a menu when it plays several; `clip add --strip` and then its roll when it has none). Titled
"Piano Roll — <pattern> · <strip>"; a pattern gone closes it. A toolbar: snap 1/4 · 1/8 · 1/16
(default) · 1/32 · Off, Notes | Steps, Quantize… (a menu of three lines: straight, swing 25 %, 50 %, at
the snap). **Notes:** keys on the left (C named with its octave, C4 = 60; a kit's keys by its pads,
nothing else named), black-key rows shaded, bars stronger; notes in the strip's colour, brighter the
louder; the velocity lane below; past the pattern's end dimmed, the end a handle (`PianoRoll::onPaint`, `PianoRoll.cpp:512`).
A click on empty grid → `note add <pt> --pitch --at <the snapped cell> --length <the last length>`; a
note dragged is drawn where the POINTER has it (beat snapped) and lands as ONE `note move … --to-pitch
--to-at`; its right 5 px resize it (`note move … --length`, which becomes the last length); a
velocity stem dragged → `note move … --vel`; a double-click or right-click → `note delete`; the end
dragged → `set <pt>.length=` (`PianoRoll::handleGesture`, `PianoRoll.cpp:258`). **Steps:** a row per pad (a kit) or per pitch in
use and middle C, a cell per sixteenth, a click toggling a note there (`note add … --length 0.25` /
`note delete`) (`PianoRoll::paintSteps`, `PianoRoll.cpp:482`). **Nothing snaps:** notes are keyed by (pitch, beat) — a new one
fades in, a deleted one fades out where it was, a dragged one is the pointer's and lands without
re-fading (`PianoRoll::bind`, `PianoRoll.cpp:68`; `PianoRoll::advance`, `PianoRoll.cpp:173`); the two modes cross-fade; Steps zooms to fit the pattern
(64–160 px a beat) and Notes gets its own zoom back, eased; Ctrl+wheel zooms about the pointer, the
wheel scrolls, eased; a length changed from a shell moves the end, eased.

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
**One text:** Solaris's `parseParam` / `paramText` (`core/Compile.cpp:30`, `:32`) ARE the DSP library's
`paramFromText` / `paramToText`, so a `.slp`'s device values and a plugin's state are the same text by
construction (the model and service suites, unchanged, still pass on it). The device panel's log taper
is the same formula as `normalizedFromValue`'s (N2) but its own copy: the front end does not link the
DSP library (law 13), and every value it sends is parsed by the service.
**Verified** (root `ctest`): `vst3_validate_synth`, `vst3_validate_drums` — the SDK's validator, 47
tests, 0 failed, each; `vst3_equivalence` — each bundle hosted offline equals the device rendered
Solaris's way, 0 of 96 000 samples differing, not silence, its state read back as text.

### DR-MIX-15 The sidechain: a key into a later strip's compressor (R-MIX-15, R-EDM-3)
`send add <ch> --to <ch2> --sidechain [--pre] [--gain]` makes a KEY (`#asend … sidechain=true`,
`<send>.sidechain` settable): `feedsForward` (`model/Project.cpp:766`) — the one rule, later in processing
order, its own mixer included, as for every route since R-MIX-4's amendment — and the validator refuses a
key to the master or a port (`:850`); `strips[].keyTargets` (`keyTargetsOf`, `:777`) is what a
picker offers. The engine sums a key into the target's KEY buffers instead of its input (`routeKey`,
`engine/Engine.cpp:399`, through its latency-compensating delay, DR-MIX-17) and hands them to every device
whose type `takesKey` before it runs (`:527`) —
the DSP Compressor with Sidechain on detects on it (DSP REQ-fx-sidechain-1); silence when nothing keys
it. A strip silenced by another's SOLO still keys (`keyLive`, `core/Compile.cpp:161`; `engine/Engine.cpp:554`) and
a key does not pull its source into a solo (`Compile.cpp:52`), so a soloed bass keeps its pump while the
kick stays silent; a MUTED strip keys nothing (live, a mute in a song with keys swaps the engine,
`core/service/ServiceTransport.cpp:297`). The audit names a key that no compressor with Sidechain on
hears (`core/service/ServiceModel.cpp:464`). In the dock a key reads "key <target>" in the solo amber,
and a strip's menu offers "Sidechain to ▸" = its `keyTargets` (`app/widgets/MixerDock.cpp:1090`).
Measured (L2): a ghost kick (−120 dB fader, pre-fader key) through −30 dB at 4:1 dips a −6 dBFS bass
6–22.5 dB 15–35 ms after each kick, back within 1 dB before the next; identical with the bass soloed;
nothing with the kick muted.

### DR-EDM-2 The metronome (R-EDM-2, R-TIME-4)
`settings set metronome=on|off`, `metronomeLevel=<dB>` (machine settings, DR-SET-3). The click is the
`Player`'s (`core/Player.cpp:159`), after the engine, so it is in what is heard and never in a render: its
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

### DR-EDM-7 The loop region on the ruler (R-EDM-7, R-TIME-4)
`Timeline` draws the model's `transport.loopFrom … loopTo` as a brace on the ruler, its ends marked, in
the accent, and tints the region on the lanes under the clips (`app/widgets/Timeline.cpp:964`, `:892`;
`loopRect`, `:452`). It is eased (`:330`): it fades in where it is when a loop appears, fades out when
it goes, and a loop changed by anyone moves there over 220 ms. `loopGesture` (`:460`): Shift-drag on the
ruler draws the brace under the pointer (both ends on the lanes' snap step, like a seek — DR-UI-10,
`:469`) and sends ONE `transport loop <from> <to>` on release, the brace staying where it was let go
(both ends on one line is a Shift-click: no region, `:483`); a click inside the brace sends `transport
loop off`; a plain click elsewhere on the ruler still seeks. The engine already loops (DR-PLAY-1), so
the playhead wraps at the brace's end while playing.

### DR-EDM-8 A sampler (R-EDM-8)
The DSP library's `sampler` (REQ-inst-sampler-1: chromatic or one-shot, a span, reverse, an ADSR) is a
registry instrument that `takesSample`: the host decodes, the device copies. Solaris stores its sound
on the device node (`#aeffect … sample=<path>`, relative to the song as a clip's `src`), sets it with
`strip add --kind instrument --instrument sampler --sample <file>`, `clip add --instrument sampler
--sample <file>` (one drop, one command) or `set <dv>.sample=<file>` (`""` takes it away) — resolved and
refused unread by `resolveSample` (`core/service/ServiceEdit.cpp:123`; `:186`; `SolarisService.cpp:694`),
refused on a type that plays none. Compile decodes it through the same cache as clips and puts it on the
device's description (`core/Compile.cpp:102`); the engine hands it in at build, never on the audio thread
(`engine/Engine.cpp:56`); a change of sound is a structural live update (an engine swap). The model
gives `devices[].takesSample`, `.sample` and `deviceTypes[].takesSample`; the audit names a sampler with
no sound or an unreadable one (`core/service/ServiceModel.cpp:443`). In the UI a sampler's window names
its sound where others name their type (`DevicePanel::sampleText`, `app/widgets/DevicePanel.cpp:417`),
and a browser sample dragged over it lights the window (eased) and drops as ONE `set <dv>.sample=`
(`WindowLayer::samplerAt`, `app/widgets/FloatWindow.cpp:275`; `app/widgets/ProjectScreen.cpp:89`).
Measured (L2): at the root the render IS the decoded file sample for sample (a mutant whose engine skips
`setSample` fails it), an octave up is 2 kHz and over in half the time.

### DR-EDM-9 Audition in the browser (R-EDM-9, R-BROWSE-2)
`audition <file>` / `audition stop` — a machine command (not an edit: no undo, the song stays saved),
with or without a song open. The file is decoded through the clips' cache and played by an
`Auditioner` (`core/Auditioner.cpp:40`) — its OWN output stream and thread, so a preview is heard
whether the song plays or not, at the `auditionLevel` setting (dB, default −6), to its end, then it
stops itself; it never allocates on its thread and is never in a render (a render does not know it).
`auditionCommand` (`core/service/ServiceTransport.cpp:116`) refuses a file it cannot read naming it; the
service's `pump` (`:65`) carries its progress into `audition.{file, playing, progress}` and announces its
end (`audition.changed … playing=0`); the model keeps it across refreshes, and the App re-binds while it
plays and once when it ends (`app/App.cpp:252`). In the browser a click on a sample hears it, a click on
the one being heard stops it (`app/widgets/Browser.cpp:246`); its row fills as it plays with an accent
bar along its foot, keyed by the file and eased in and out (`:165`, `:312`) — a double-click still places
it. Settings › Playback has PREVIEW LEVEL. Measured (L2): the live stream carries the decoded file at
the audition level, sample for sample; it ends itself, said.


### DR-UI-10 The grid follows the zoom (R-UI-10, R-TIME-5)
**The levels.** The lanes' grid has seven: a bar (the song's meter), a beat, and 1/2, 1/4, 1/8, 1/16
and 1/32 of a beat (`Timeline::gridSpan`, `app/widgets/Timeline.cpp:59`). How much of a level is drawn
is a smoothstep of its line spacing at the EASED zoom — nothing closer than 6 px, all of it from 16 px
apart (`kGridHidePx`, `kGridFullPx`, `app/widgets/Timeline.h:81`; `room`, `Timeline.cpp:30`;
`gridAlpha`, `:64`) — recomputed every frame, so a Ctrl+wheel zoom (eased 220 ms) fades a level in or
out with it and never pops one. A line is drawn once, by the coarsest level it belongs to: bars
strongest (white 7 %), beats next (4 %), the divisions faintest (2.2 %); every fourth bar is always
drawn, so a far-out song keeps its phrases (`paintGrid`, `:862`). The ruler repeats the levels as ticks
— long for a bar, shorter as they get finer — and its labels are MEASURED against their room: a bar's
number fades in as its every-1/2/4/… bars get room for the widest number in view, and zoomed in the
beats are named `bar.beat` (`2.3`), fading in as a beat gets room for one (`paintRuler`, `:884`; the
labels `:751`). The old fixed thresholds, which swapped the labels in one frame mid-zoom, are gone.

**The step.** THE snap step is the finest level whose lines are at least 11 px apart (`kSnapPx` — past
half drawn: you snap to lines you can see), never coarser than a bar; at the deepest zoom it is 0 and
nothing snaps (`snapStep`, `:73`; `atDeepestZoom`, `:70`). `snap()` (`:80`) is the ONE rounding of
every gesture on the lanes: a ruler click → `transport seek` (`:575`), a clip drag (`:538`), the loop's
Shift-drag (`:469`), an automation point added or dragged (`app/widgets/TimelineAuto.cpp:496`, `:222`),
the browser's drop and its hint (`app/widgets/ProjectScreen.cpp:99`) — at the deepest zoom the tick
under the pointer. Every gesture is still ONE command, its beats printed to the tick: rounded to 1/960
and spelled with the fewest decimals that keep it (`Timeline::beatText`, `Timeline.cpp:44` — the `%g`
it replaces kept six significant digits, so past beat 1000 a line lost ticks). The step is published
LIVE for tests (`snapStep`, `snapLabel`, `gridAlpha`, `snapLabelAmount`, `Timeline.h:116`) and named in
the ruler's corner over the lane headers — `Snap Bar`, `1/4` … `1/128`, `Off`, in the piano roll's note
values (a beat = 1/4) — cross-faded 200 ms when it changes, older names fading out from where they are
however fast the wheel turns (`stepName`, `Timeline.cpp:88`; `advance`, `:310`; `paintRuler`, `:723`).

**The zoom.** A lattice of ×1.25 notches about 28 px/beat, 8 out (4.7 px/beat: a 4/4 bar is 18.8 px)
and 14 in (637 px/beat), so a notch in and a notch out are exact inverses and the deepest zoom is one
place (`:620`). **Decision:** the deepest zoom rose from 320 px/beat so that 1/32 of a beat gets room —
20 px apart at 637, fully drawn, and THE step one notch before snapping turns off. The step by zoom:
a bar below 11.5 px/beat, a beat from 11.5, 1/2 from 22.4 (a song opens at 28: bars, beats and halves
drawn, the step 1/2), 1/4 from 55, 1/8 from 107, 1/16 from 209, 1/32 from 408, nothing at 637.

**Proof.** `test_the_grid_follows_the_zoom` (`app/tests/ui/uiTests.cpp:1656`) drives the real service:
at the default zoom a ruler click at beat 3.3 seeks 3.5 (a fixed quarter says 3.25); a level's alpha
is caught mid-fade during an eased two-notch zoom, between its two ends; at 1/8 a seek, a clip drag
(on the step while held, then `clip move ac_1 --at 3.125`) and an instrument dropped from the browser
land on the same step, the step's name caught mid cross-fade; at the deepest zoom a click seven ticks
past beat 2 is exactly `transport seek 2.007`; all the way out a click at 9.9 is `transport seek 8`.
Mutants — a fixed 1/4 snap, a level's alpha taken from the TARGET zoom (it pops), a deepest zoom that
snaps — each fail it. Shots `lanes-zoomed-in`, `lanes-zoomed-out`, `lanes-zoom-mid` (mid Ctrl+wheel)
(`app/tests/shots/renderShots.cpp:165`).
### DR-MIX-16 The mixer's numbers bound (R-MIX-16)
**One menu.** `paramMenuItems` (`app/widgets/ParamMenu.cpp:43`) builds the right-click menu of every number a
formula can drive from a `ParamTarget` (address, formula, value as shown, reset, bindable): Create Automation
unless it already IS one (`auto create <address>`), Formula… (cosmo's rename field seeded with the formula or
"=", → `set <address>="=<typed>"`, quoted when it has a space), Clear Binding when bound (`bind clear
<address>`), Reset to Default (`set <address>=<default>`), and Copy Address / Copy Value / Copy as Formula
(DR-UI-11). The device window (`DevicePanel::openParamMenu`, `app/widgets/DevicePanel.cpp:443`) and the dock
(`MixerDock::openParam`, `app/widgets/MixerDock.cpp:837`, offered first on a right-click, `:1051`) both call
it, so the menus cannot drift; a hosted third-party VST3's parameter list (R-VST-6, not built yet) will call
it the same way. In the dock: a fader → `<ch>.gain`, a pan → `<ch>.pan`, a send's row on a card or its cell
in the matrix → `<sd>.gain`, the master fader → `project.masterGain` (the addresses that already bind,
R-AUTO-1); Reset is 0 (unity, the centre); any other right-click on a card is still the strip's menu.

**The value at the heard position, published.** `bindings[].live` (`AppModel.h`). Stopped: `evaluateLive`
(`core/service/ServiceModel.cpp:353`) compiles the song as `eval` does and evaluates every binding with the
engine's own `evaluateBinds` at the transport's position (an inert binding: its own value) — so it equals
`eval <address> --at <position>`. Playing: after every block the `Player` copies `Engine::bindValues()` into a
ring of 64 slots of pre-sized atomics (`keepLive`, `core/Player.cpp:105` — stores only; a sequence number
brackets each write), each stamped with the frames handed to the device and the generation of the engine
that played it; `pump` (`core/service/ServiceTransport.cpp:108`) reads the slot the listener hears now —
the newest at or before the newest minus the device's latency, of the engine the service last built
(`liveValues`, `core/Player.cpp:123`; `buildLive` bumps the generation, `ServiceTransport.cpp:58`, and a
`Swap` carries it, so a new bind order is never read with the old one) — and maps the engine's bind
order to addresses. While playing, `refreshModel` keeps the pumped values and evaluates only a binding new
since (`ServiceModel.cpp:87`, `:340`). At most 256 bindings report (`Player::kMaxBinds`). Not in the stable
dump: it moves with the transport.

**The dock draws it** (`MixerDock::advance`, `app/widgets/MixerDock.cpp:516`): a bound fader or pan takes its
live value; while playing it is SET each frame — it follows the audio like the playhead — and stopped a change
(a seek, a curve edited) eases 220 ms; a new driver (the formula changed) or play pressed mid-ease is a
catch-up ease that lands when it ends and then follows (`follow`, `:551`). A bound send shows its live
level. What drives each: a tag over the fader — `auto au_1`, `= ch_3.gain`, `= -2 + 2 * s…` (`paintTag`,
`:1107`) — "ƒ" after a bound pan and before a bound send's level, the readout in the accent; every tag fades
in and out (200 ms) and its text cross-fades when the formula changes (`:597`).

Measured. L2 `test_a_bound_value_is_published_live` (`core/tests/serviceTests.cpp:1237`): stopped, `live` is
`eval`'s number at the position; playing (a fake clock device), a −2 dB-a-beat ramp is within 0.1 dB of the
curve at the heard position at beats 2…12 and falling, a link (`=ch_2.gain / 2`) exactly half of it, a device
formula `1000 + 500 * sin(beat)` within 25 Hz; after a structural edit while playing the new engine's order is
read; 0 allocations on the audio thread. Mutants: `pump` ignoring the player → stuck at 0 dB at beat 2;
`bindValues()` copied on the audio thread → 1019 allocations. UI `test_the_mixers_numbers_bind_from_the_dock`
(`app/tests/ui/uiTests.cpp:1439`): a fader's right-click is the parameter menu, Create Automation is `auto create
ch_2.gain` and the tag fades in (caught mid-tween); Formula… on a pan sends `set ch_3.pan="=0.25 - 0.75"` and
the knob EASES onto −0.5; a send copies `-8.0 dB` and `=sd_1.gain`; the master copies `project.masterGain`;
Clear Binding fades the tag out; playing, the fader equals `faderPos(live)` every frame and moves. Mutants:
the dock ignoring `live` → the pan never reaches −0.5; easing instead of following while playing → fader
0.707946 against live 0.705559. Shots `mixer-bound` (a fader on an automation, one linked to it, an auto-pan,
a send and the master on formulas) and `copied-toast`.

### DR-UI-11 IDs shown and copied (R-UI-11)
**The setting.** `showIds = on|off` is a MACHINE setting (`core/Settings.cpp:24`), `settings set showIds=…`
(`core/service/ServiceMachine.cpp:106`), published as `settings.showIds` — so an agent turns the ids on as the
View menu does (View › Show IDs / Hide IDs, `app/App.cpp:158`).

**Drawn, faded.** `ProjectScreen` eases ONE amount from the model's setting (`app/widgets/ProjectScreen.cpp:178`,
200 ms; `idsAmount()` is the live value) and hands it to every widget that draws ids; `drawNameWithId`
(`app/widgets/ParamMenu.cpp:84`) draws a name with its id right-aligned in the accent's mono, the name giving
way by the EASED amount. Where: lane headers (the lane's id; a strip's own row, the strip's) and automation
rows (`au_n`) (`app/widgets/Timeline.cpp:1028`, `:715`); clips (`ac_n`, and a note clip's pattern `pt_n`;
`:558` — "linked ×N" fades out for them); in the dock each strip card (`ch_n`), the master (`master`), each
rack chip (`dv_n`) and each send (`sd_n`, cross-fading with "→ name") (`paintCard`,
`app/widgets/MixerDock.cpp:1162`); a device window's rows show their FULL address (`dv_1.filter.cutoff`) in a
pill over the label (`ParamIds`, `app/widgets/DevicePanel.cpp:179`); its header already names its id.

**Copied, by the host.** Copy Address / Copy Value / Copy as Formula on a device parameter row and on the
dock's faders, pans, sends and master (DR-MIX-16; a choice row has no "as Formula" — no formula reads a
choice); Copy ID on a clip (`app/widgets/Timeline.cpp:775`), a strip's and a mixer tab's menu in the dock; Copy ID
and Copy as Formula (`=au_1`) on an automation row (`app/widgets/TimelineAuto.cpp:560`). The clipboard is the
HOST's (the core has none, R-SVC-4): `App::onCopy`, which `linux_main.cpp:211` gives GTK's CLIPBOARD and
PRIMARY selections and the test rig records (`Rig::copied`). A copy says so: `App::copy` (`app/App.cpp:200`)
toasts "Copied dv_1.filter.cutoff", outlined in the accent (a refusal's toast stays red). When hosting lands,
a third-party VST3's parameter list (R-VST-6) builds a `ParamTarget` per row and offers this same menu.

Measured. L2 (`test_the_machine_settings_folders_devices_and_recents`): `settings set showIds=on` reaches the
model and the file and a second process reads it; `showIds=yes` is refused. UI `test_ids_shown_and_copied`
(`app/tests/ui/uiTests.cpp:1537`): View › Show IDs sends `settings set showIds=on`, the ids' amount caught between 0
and 1, then 1, the item reads Hide IDs; a shell's `showIds=off` fades them out the same way; a parameter row
copies `dv_1.filter.cutoff`, `900 Hz`, `=dv_1.filter.cutoff` (toasted); a choice row offers no "as Formula";
a clip copies `ac_1`, an automation `=au_1` and `au_1`. Mutant: the ids SET instead of eased → the mid-tween
assertion fails. Shots `show-ids` (lanes, clips, an automation, the dock and a device window's rows) and
`show-ids-mid` (mid-fade).

### DR-AUTO-6 Bezier automation (R-AUTO-10)
ONE curve model in the suite (law 16): a curve IS Interstellar's keyframe list, and Interstellar's
header-only evaluator (`apps/interstellar/model/Anim.h` — After Effects' key, each side linear | bezier |
hold with a speed and an influence) is INCLUDED in place by the engine (`engine/Expr.h:22`), not ported:
it allocates nothing (a fixed 48-step bisection on the Bézier's time component, doubles only), so the
audio thread runs it every 64 samples as it runs the formulas. A `#point` (`AutoPoint`,
`model/Project.h:151`) keeps `shape=` and gains Interstellar's spelling for a bezier point's handles —
`speedIn inflIn speedOut inflOut`, speeds in the automation's unit PER BEAT (the `.slp`'s timebase),
influences in % (0 < x ≤ 100), read always (`model/Project.cpp:325`), written only for a bezier point
(`:735`), so a file of linear / hold / smooth points reads and writes byte for byte as before.
`engine::curveKeys` (`engine/Expr.h:139`) is the ONE mapping of a point onto a key's two sides, used by
the compiler and the timeline's drawing alike: linear → both sides linear; hold → its out side holds;
smooth → After Effects' Ease on the segment after it (speed 0, influence ⅓ on both ends — a Bézier
whose time is exactly linear, so its value is exactly R-AUTO-4's smoothstep, to 1e-9 measured) unless
the next point is bezier, whose own handle wins on its side; bezier → both sides bezier with its
handles. `compileCurve` (`core/Compile.cpp:330`) converts beats to samples (speeds ÷ samples per beat)
and ranges the curve: `Curve::valueAt` (`engine/Expr.h:181`) clamps to the automation's min/max, so a
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
calls `onOpenAutomation` (`app/widgets/ProjectScreen.cpp:54`) → `WindowLayer::openAutomation`
(`app/widgets/FloatWindow.cpp:167`): an `auto:<au>` FloatWindow (fading in and out as every window does)
around an `AutomationPanel`, titled "Automation — <name>" and closed when anyone deletes the automation
(`WindowLayer::bind`, `app/widgets/FloatWindow.cpp:249`). Every line is the model's (`AutomationPanel::bind`,
`app/widgets/AutomationPanel.cpp:56`): name, id, range in its unit, the address it was made from, its
points (beat, value, shape — a bezier point's speeds and influences), every formula that reads it
(`usedBy`, each with its binding's formula from `bindings[]`), and its value at the playhead —
`automations[].now`, which the service computes with the engine's own compiled curve
(`compileCurve`) at `transport.position` in `refreshModel` (`core/service/ServiceModel.cpp:322`,
`refreshNow`, `:338`) and again in `pump` while playing (`core/service/ServiceTransport.cpp:113`); it is
left out of `state print --stable`, as the position is. The rows are an `AnimatedRows` keyed by what
they say — a point added slides the rows below and fades in, an edit or a rename cross-fades its row;
the value at the playhead follows the transport continuously while playing and eases to a seek or an
edit (`app/widgets/AutomationPanel.cpp:105`). Rename is cosmo's field over the header's button and ONE
`set <au>.name=…` (an address that already existed, R-AUTO-4; `app/widgets/AutomationPanel.cpp:185`). A double-click ON a point still deletes it
(R-AUTO-6). Measured (L2): `now` equals `eval` at the sought beat and the cubic while playing; (UI) the
window caught mid-fade, the facts present, `now` and a new row caught mid-tween, the rename one line.

### DR-SVC-8 Composition is agent-sized (R-SVC-8)
Built from the audit of an agent's 16-bar song (110 commands, 64 single `note add`s, every id guessed,
edits lost between calls, comment lines reprinting ids). The guide is `docs/AGENTS.md`.
- **A session that lasts.** `solaris-cc shell [--song <f>]` (`cli/main.cpp:151`) reads stdin lines into
  ONE service — the open song, its ids and its undo history last the session; `--song` opens the file
  or makes it when it is not there; a refusal is reported and the session goes on (exit 3 at the end).
  Every run ends in `finish` (`:141`): a song left with unsaved edits is named on stderr and the run
  exits **4**, unless `--discard`.
- **Scripts that say where.** A refusal prints `refused: line N: …` — the script's own line number, or
  the chained command's (`run`, `:129`); `--keep-going` runs past refusals and still exits 3 (`:198`).
  `parseCommand` drops a `#` that starts a word outside quotes (`withoutComment`,
  `core/service/Command.cpp:315`) — `F#3` stays a pitch; a comment or blank line clears the output
  (`core/service/SolarisService.cpp:59`), so nothing is printed twice.
- **Everything made is said.** `dispatch` diffs the song's ids around an edit (`:232`): the first line
  stays the command's own id, then `made: strip=… device=… pattern=… lane=… clip=…` lists the rest by
  kind. Each made node is announced by its own `project.changed` — the instrument of a new instrument
  strip (`core/service/ServiceEdit.cpp:236`), an auto pattern (`:515`), every new lane (`:381`).
- **Notes in bulk, by name.** `core/service/Notation.cpp` is the notation, pure: `parsePitchName`
  (`:40`, C4 = 60, `#`/`b`, c-1 = 0), `chordQualities` (`:79`, 21 qualities — maj, m, 5, dim, aug,
  sus2, sus4, 6, m6, 7, maj7, m7, mmaj7, dim7, m7b5, 7sus4, add9, madd9, 9, maj9, m9), `chordPitches`
  (`:107`, `--octave`, `--inversion` raising the lowest notes), `parseNoteToken` (`:135`,
  `<pitch>@<beat>[:<length>[:<vel>]]`), `parseSteps` (`:167`). `SolarisService::pitchOf`
  (`core/service/ServiceCompose.cpp:80`) resolves a pitch anywhere — `--pitch`, `--to-pitch`, a note
  token, a step row — as a number, a name, or a pad of the kit that plays the pattern (the registry's
  note names, case/space/hyphen-blind; every kit's when no clip plays it yet), refusing an unknown one
  with the pads listed. `notes add` (`:153`) parses every token before touching the pattern and refuses
  the first bad one by number and text — ONE edit, ONE undo step. `note add --chord` (`ServiceEdit.cpp:770`)
  adds a chord as one edit and prints its notes. `pattern steps` (`ServiceCompose.cpp:190`): x / X (127) /
  `.` per `--step` from `--at`, replacing that pitch in the span. `pattern duplicate | clear [--pitch] |
  delete` (refused while a clip plays it, naming the clips, `:248`) `| transpose --semi` (refused whole if a
  note would leave 0–127, `:259`). A note starting at or past its pattern's end is warned at once
  (`pastEndWarning`, `:122`) and audited (`core/service/ServiceModel.cpp:466`, with empty and unused patterns).
- **Arrangement.** `clip duplicate --count N` makes N copies end to end in one edit
  (`ServiceEdit.cpp:704`). A `clip add` with no `--lane` goes on the lane of its strip's newest clip; a new
  lane only for a strip with none; `--lane new` asks for one (`laneFor`, `:529`) — the GUI's drop below the
  last lane says `--lane new` (`app/widgets/ProjectScreen.cpp:109`), so R-BROWSE-3 holds.
- **Reading back** (read-only: in `mutates()`, `SolarisService.cpp:112` — no undo step, still saved):
  `ls` (`lsText`, `ServiceCompose.cpp:329`), `show <id>` (`showText`, `:404` — a device's non-default
  parameters with unit and default, a formula where one drives it), `pattern print` (`:294`, the notation
  with the pitch's number and name after `#`, a kit's pads by name), `state print --json --compact`
  (`compactJson`, `core/service/AppModelCodec.cpp:270`; `Json::dumpCompact`, `core/service/Json.cpp:118`):
  the song only, one line, a device's non-default parameters as `name=value`, notes in the notation.
- **Honest values.** A device parameter outside its registry range is refused naming the range and unit
  (`SolarisService.cpp:713`), as is an automation point (`inAutoRange`, `core/service/ServiceAuto.cpp:69` — a value within printing's
  rounding of a bound is that bound, so a dragged point at the top edge still lands).
- The API document gains the Notation section and each kit's pads (`core/service/ApiDoc.cpp:127`, `:80`).

Guarded by `solaris_service` (`core/tests/serviceTests.cpp:1313` … `:1666`: the notation tables; comment
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

### DR-SVC-5 The control channel and attach (R-SVC-5, R-SVC-6)
**The window.** `solaris --control <socket> [song.slp]` (`linux_main.cpp:240`) builds a `ControlServer`
on the window's own service after any song opened and polls it from its own 16 ms GLib timeout
(`onControl`, `:143`; `:254`) — not the frame clock, so a minimised window is still drivable. The
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

**attach** (`cli/Attach.cpp:55`, hooked at `cli/main.cpp:85` before any service is built — it builds
none): lines from `--script`, chained ` : ` args, or stdin; sends ONE line, waits for its `[ok]`
(matched by echo) or `[refused]`, then the next (`:141`–`:175`), so a script ends when its last answer
arrives, never on a quiet-period guess. It prints what solaris-cc prints where solaris-cc prints it:
output on stdout, event lines on stderr (always — watching is the point), `refused: <why>` and exit 3
on a refusal (`:206`); `--follow` keeps printing events; `--timeout` (300 s). Empty lines are not sent:
the channel delivers none, so none would be answered (`:170`).

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

**`solaris-cc ntwb`** (`cli/NtwbMain.cpp:178`): `api`; `install [--data-dir D] [--web-dir W]` (`:77`)
writes `D/solaris/{ntwb.json, api.json, web/}` (D defaults to `$XDG_DATA_HOME/ntwb/apps`) — the web
UI plus what it borrows, copied not forked: cosmo's `js/core/signal.js` + `dom.js` (`:93`) and cosmo's
typeface (`:99`); the manifest is `single: true` (`:130`) — one engine on the clock device, its pages
sharing the song; `uninstall [--data-dir D]`; `serve` (`:142`) — the service with the window's host
(files, devices, the clock device, the machine's settings and recents), the adapter, and the loop that
owns the clock: `client.poll(8)`, `pump()`, `tick()` (`:166`).

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
`compensate` (`engine/Engine.cpp:198`) once the bindings' values at time zero are in (`:191`), and walks
the graph in processing order (every target is later): a strip's INPUT is aligned to the latest arrival —
its own sound (an instrument's latency), earlier strips' outputs, sends and sidechain keys; its OUTPUT is
that plus its effects' latencies (`stripLatency(i)`, `engine/Engine.h:116`). The master's input is
aligned the same way, its output adds its rack's, and every port and the captured master meet at the
latest of all (`outputLatency()`, `engine/Engine.h:114`). Then each connection — a main output, a send, a
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
(`core/service/ServiceRender.cpp:80`, `trim`, `:112`). `Player::heard` subtracts it from the position
(`core/Player.cpp:64`, set at `start` and on every swap, `:97`); the metronome clicks on the music's beat
(`:223`) and the published bound values are the heard block's (`:128`). A live `set` that moves a
device's latency (a lookahead) builds a new engine instead of a message (`latencyOf`,
`core/service/ServiceTransport.cpp:63`, checked at `:312`; the live engine's latencies recorded by
`buildLive`, `:57`, via `Engine::deviceLatency`, `engine/Engine.cpp:375`).

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
`core/tests/serviceTests.cpp:1711`): two strips of one tone, a limiter on one — each stem from beat 0 and
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
`lane move <ln> --to <index>` (`core/service/Command.cpp:90`; `case K::LaneMove`,
`core/service/ServiceEdit.cpp:476`) puts the lane at row `index` (0 = the top) and renumbers every lane
0…n−1 in the new order — one edit, one undo step; an index past the last row or an unknown lane is
refused. The lanes draw it through Interstellar's `AnimatedRows`, keyed by lane, so the rows slide
(DR-UI-3). Guarded by `test_lanes_are_reordered` (`core/tests/serviceTests.cpp:1822`: to the top and back,
renumbered, refused out of range, undone) and, drawn, by `test_instrument_drop_and_clip_drag`
(`app/tests/ui/uiTests.cpp:289`: a lane moved from a shell slides to the top with its clip, caught mid-tween).

### DR-TIME-6 The ruler dragged (R-TIME-6)
`Timeline::rulerGesture` (`app/widgets/Timeline.cpp:559`, called first in `handleGesture`, `:654`) takes a
press on the ruler without Shift and waits: a click is still the seek (R-TIME-5) or clears the loop
(R-EDM-7). A drag decides ONCE, by where it began: within `kBraceGrip` (6.5 px) of an end of the loop's
brace, or on its body, in the ruler's lower half (`:579`), it moves the loop; anywhere else it scrubs.
Scrubbing, the playhead is the pointer's on the snap step (`advance`, `:333`, overrides both the eased
seek and following the audio while it is held) and a `transport seek` goes at each new line — a fader
sends each step the same way; a seek is transport, not an edit, so no undo step and the song stays
saved — and once more on release if the last line was not sent; the playhead is `set` there, so
nothing eases back. The brace is the pointer's while held (`loopSpan`, `loopRect`), moved with its length
kept or resized never shorter than one step, and ONE `transport loop <a> <b>` goes on release (`:640`),
the brace placed where it was let go so the model's echo moves nothing.

Guarded by `test_the_ruler_is_dragged` (`app/tests/ui/uiTests.cpp:1262`): held, the playhead equals the
snapped pointer beat exactly and four or more seeks were sent; let go, it stays; PLAYING, it stays the
pointer's frame after frame and the song plays on from there; the brace moved 4–8 → 6–10, nothing sent
until release, then one line; resized by its right end, clamped at its left; a click still seeks. Mutants
seen red, then restored: the playhead following the audio while scrubbing, a seek only on release, a body
drag keeping the brace's end. Shots `ruler-scrubbing` and `loop-brace-moving`
(`app/tests/shots/renderShots.cpp:429`, `:438`), both sizes, looked at.

### DR-LANE-3 Instrument tracks (R-LANE-3; R-LANE-1 amended)
**The model.** `Lane::track` (`model/Project.h`) names the instrument strip a lane is the TRACK of; the
file writes it `#alane … track=ch_2` (the suite's word for a strip, as a clip's `track=`), and every
save writes the header line `tracks = on` (`model/Project.cpp:656`; read at `:488`, any other value
refused). `validateProject` refuses a track of no strip or of a non-instrument (`:897`) and a clip on a
track that does not play through it — audio named as audio (`:904`). `laneTitle` (`:48`) is the name a
lane shows: its own, else a track's strip's — so renaming the instrument renames its tracks (D-2's
lane half).

**The service.** A lane made for an instrument is its track with no name of its own (`newLane`,
`core/service/ServiceEdit.cpp:504`); an instrument's clip with no `--lane` goes on its track (`laneFor`,
`:529`; `trackOf`, `:518` — the track its newest clip is on, else its newest, else a new one), and only
an instrument with no track and clips on plain lanes keeps R-SVC-8's rule. `clip add --lane <a track>`
(`:554`) implies the strip and places `--pattern` there; audio, `--instrument` and a different `--strip`
are refused there by name. `clip move` (`:680`): onto a track a note clip is re-routed in the same
edit (one undo step) and the output says `<ac> plays through <ch> (<name>)`; `--strip <other>` on a
track moves it to that one's track; audio and `--lane` with a different `--strip` are refused. `lane add
--strip <ch>` (`:450`); `set <ln>.strip=<ch>|none` (`core/service/SolarisService.cpp:635` — refused while
the lane holds another strip's or an audio clip; `none` keeps the name it showed). `strip relink` takes
the strip's tracks with its clips (`ServiceEdit.cpp:324`); `strip delete` leaves them plain lanes named
as they were (`:267`). A song saved before tracks (no `tracks` line) is adopted on opening
(`adoptTracks`, `SolarisService.cpp:75`, called at `:453`): a lane whose clips all play through one
instrument becomes its track, a name it was made with (its strip's, or its instrument's registry
label) cleared so it shows the strip's, each said as an `info` event; opening stays clean (not dirty).
Published: `lanes[].name` (what it shows), `lanes[].strip`, `lanes[].ownName`
(`core/service/ServiceModel.cpp:224`); `ls` marks `· track of <ch>` (`core/service/ServiceCompose.cpp:375`).

**The view.** A track's row takes its strip's colour unless it has its own and names its instrument
under its name (`Timeline::bind`, `app/widgets/Timeline.cpp:131`), that line fading in and out as the
lane becomes and stops being a track (`advance`, `:359`). A lane header's right-click (`:731`): Track
of ▸ (the instrument strips) → `set <ln>.strip=<ch>`, Plain Lane → `…=none`, Copy ID. A sample or an
instrument dropped from the browser onto a track asks for a lane of its own (`--lane new`,
`app/widgets/ProjectScreen.cpp:109`) — it could not play through another's instrument.

Guarded by `solaris_model` (`model/tests/modelTests.cpp:369`: the four refusals; `tracks` read, refused
other than `on`, written in the canonical text), `solaris_service` (`test_instrument_tracks`,
`core/tests/serviceTests.cpp:1554`; `test_a_song_from_before_tracks_adopts_them`, `:1626`) and
`solaris_app_ui` (`test_a_lane_is_an_instruments_track`, `app/tests/ui/uiTests.cpp:220`: a rename
followed, a clip dragged onto another track re-routed with its colour caught mid-ease, Track of ▸ and
Plain Lane caught mid-fade, a browser drop onto a track → `--lane new`). Mutants seen red, then
restored: no re-route onto a track, no adoption, a track not showing its strip's name, the line placed
instead of faded, a drop onto a track not redirected.
