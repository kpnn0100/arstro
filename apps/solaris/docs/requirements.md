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
when fresh. `filter` is the synth's SVF with a mix (`src/device/DeviceRegistry.cpp:328`). The rest
wrap `Compressor`, `Reverb`, `Repeater`, `Chorus` and `Overdrive` unchanged
(`EffectDevice`, `src/device/DeviceRegistry.cpp:221`). R-FX-5 (a strip's rack) is Solaris's and is
not built yet.

### DR-FMT-1 The `.slp` document (R-FMT-1, R-FMT-2, R-FMT-4)
`solaris_model` (`model/Project.h`) holds the document as typed data — header, `#aport`,
`#amixer`, `#atrack` (strip), `#asend`, `#arack`/`#aeffect`, `#alane`, `#apattern`/`#note`,
`#aclip` — and links nothing but the standard library: a device is its registry `type` plus its
parameters as text, which the core checks against the DSP registry. `parseProject`
(`model/Project.cpp:335`) reads the suite grammar: header `key = value`, node lines, indented
continuation lines, whole-line and inline `;` comments (kept with the node they follow), unknown
keys (kept in order) and unknown nodes (kept verbatim with their indented lines). The suite's
inline `#note`s under an `#aclip` become a pattern of their own — the one normalisation, reported.
`serializeProject` (`model/Project.cpp:619`) writes §10's canonical form: `canonicalNumber`
(`model/Format.cpp:33`, shortest round-trip, always a point), `canonicalBeats` (`:52`, rounded to
1/960 beat, fewest decimals that read back to the tick), seconds to the microsecond; defaults of
optional fields are omitted. **Parse → serialize is a byte-exact fixed point** for canonical text.
Refused, each naming what and where (`validateProject`, `model/Project.cpp:781`): duplicate ids,
`master` as an id, unknown strip kinds, dangling references, an audio clip on a non-audio strip, a
note clip on a non-instrument strip, `in ≥ out`, a clip before the song, two racks for one strip,
an input port as a destination, a header that is not `app = solaris` / `timebase = beats` /
`ppq = 960`. Repaired and counted (`Reader::num`, `:182`): a non-finite or unreadable number → the
field's default; out-of-range pan, pitch and velocity clamp. Guarded by `solaris_model` (8 tests;
mutants checked: dropping unknown keys on write breaks the fixed point).

### DR-MIX-4 Routing only goes forward (R-MIX-4)
`validateProject`'s `checkTarget` accepts an `out` or a send target only when it is `master`, an
output port, or a strip `feedsForward` (`model/Project.cpp:745`) allows — one whose mixer's `order`
is GREATER than the source strip's — the ONE copy of the rule; anything else is refused as `ch_1 (Main, on Buses) output → ch_2 (kick, on Sources): a
strip can only feed a strip on a LATER mixer, the master or a port (R-MIX-4)`. A file that routes
backward does not load. Because of this, `Project::stripsInOrder` (`model/Project.cpp:113`) —
mixer order, then strip order — is a topological order of the routing graph with no cycle check
at all. A new project is `newProject` (`model/Project.cpp:151`): Sources (`mx_1`), Buses (`mx_2`)
holding the bus Main (`ch_1` → master), the port Main (`prt_1`) fed by the master (R-MIX-3).
Guarded by `test_routing_only_goes_forward` (mutant checked: `<=` → `<` lets a same-mixer route
through and the test fails). The same rule is PUBLISHED: `targetsOf` (`:636`) lists what a strip
may feed — later strips in processing order, `master`, the output ports — as `strips[].targets`
(`core/service/ServiceModel.cpp:210`); a front end offers exactly that list, and the test routes
to every entry offered.

### DR-ENG-1 The engine renders a MixGraph through the DSP library's devices (R-MIX-1/5/6, R-DSP-1/5, R-RENDER-1)
`solaris_engine` knows no project and no file: it renders a `MixGraph` (`engine/MixGraph.h`) —
strips in processing order with their devices (registry types + values), audio regions in samples
over decoded PCM, note events in samples, an output and sends as forward indices, the master's rack
and gain, the ports. `Engine::build` (`engine/Engine.cpp:82`) refuses a target that is not LATER
(R-MIX-4, a second time — the graph might not come from the model), a missing port, an unknown
device type or parameter, an instrument strip whose rack does not start with an instrument; sets
the DSP library's process-wide sample rate (R-NFR-7); builds every device with
`DeviceRegistry::create`; and **warms** each with one block of silence (`warm`, `:57`), because the
library smooths every parameter write over a block and the first block of every render would
otherwise carry a ramp from the device's default. `renderPiece` (`:327`) per strip in order: sum
what earlier strips routed/sent to it; add its regions (looped, offset, offline = silent) or play
its instrument with the block **split at every note event** (`:372`, R-DSP-5); run the rest of its
rack (bypass skips); tap pre-fader sends; fader + balance pan; post-fader sends; add into its
output (`route`, `:303` — master, a later strip, or a port; a mono port gets the average of L and
R). A silent strip (muted or solo-silenced, decided by the core) sends nothing anywhere (`:409`).
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
longest verb match, flags checked against the row) and `dispatch` (`core/service/SolarisService.cpp:85`)
runs it; out come the `AppModel` (`core/service/AppModel.h`, refreshed after every command) and
`Event`s whose `formatEvent` text is the log line. **Every edit is all-or-nothing**: the project is
copied first, the command runs, `validateProject` runs, and any failure restores the copy
(`:207`) and refuses with the validator's sentence — a refused command changes nothing. An edit's
events (`project.changed` from `changed`, `:35`; a `set` line's `params.changed`) are HELD in
`mPending` and emitted only once the whole command has landed (`:243`), so a refused command
announces nothing either (D-1). And a refusal always says WHY: a path that returns false without a
reason is caught in `dispatch` (`:189`) and named (`` `clip add` was refused without a reason``),
never silent (R-SVC-3) — B1's own wiring mistake showed the hole. The core
holds no codec and no device API: decoding and WAV writing are `Host` functions (R-SVC-4).

### DR-SVC-2 Unknown input is refused, naming it (R-SVC-3)
An unknown verb or flag fails in `parseCommand` with the nearest candidates (`strp add` →
`did you mean: strip add?`). An address goes through `setAddress` (`core/service/SolarisService.cpp:459`):
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
`clip add --src` (`core/service/ServiceEdit.cpp:415`): a file no clip uses yet gets a new audio strip
named after it on the first mixer, routed by `defaultOutFor` (`:70`) to the first bus on a later
mixer ("Main"), and a new lane; a file already used reuses its strip; `--strip` overrides. The file
is decoded through the host to learn its length (refused if it cannot be read), and stored relative
to the song's folder when inside it (`relativePath`). A relative `--src` names a file in the SONG's
folder first when one is there (`:385`) — what the model stores and the browser's Song tab hands
back — else the caller's working directory.

### DR-MIX-7 Solo keeps the soloed path alive (R-MIX-7)
`silentStrips` (`core/Compile.cpp:34`): with any strip soloed, a strip is audible only if it is
soloed, reachable downstream of one (its buses, its sends' returns) or upstream of one (what feeds
it); muted strips are silent regardless. The model shows it as `strips[].audible`; the engine gets
it as `silent`.

### DR-MIX-8/9/10 Fed by, the matrix, the audit (R-MIX-8, R-MIX-9, R-MIX-10)
`refreshModel` (`core/service/ServiceModel.cpp:78`) computes each strip's `clipCount`, `fromLanes`
and `fromStrips`. `matrix print [--json]` (`matrixText`, `:431`): rows = strips in processing order,
columns = the buses, master and output ports; `●` = the main output, `-6.0pre` = a send's dB and tap.
`audit` (`:193`): unused strips, strips that reach no output port, single-input and empty buses,
clips on silent strips, offline files, unknown device types and parameters, and any strip or the
master that went over 0 dBFS in the last render.

### DR-MIX-13 A line added from the mixer; sources relinked (R-MIX-13, R-MIX-14)
`strip relink <ch> --to <ch2>` (`core/service/ServiceEdit.cpp:242`) moves EVERY clip playing through
`<ch>` to `<ch2>` in one edit (one undo step, "strip relink <ch>"), printing how many moved; refused
onto a bus ("a bus plays no clips"), across kinds (audio clips need an audio strip, note clips an
instrument strip), onto itself, and when `<ch>` has no clips. A pattern's `strip` follows its first
clip. In the dock (`app/widgets/MixerDock.cpp`) every mixer page ends with "+ Line" after its last
card — placed from the LIVE card widths, so it slides as cards grow in and shrink out
(`MixerDock::addLineRect`, `MixerDock.cpp:317`; drawn at `:1213`) — whose menu is an audio line, a bus
and every instrument of the registry, each ONE `strip add --kind … [--instrument <type>] --mixer
<mx>` (`MixerDock::openAddLine`, `MixerDock.cpp:709`); the new card grows in. A strip's menu offers
"Move its clips to ▸" when it has clips and another strip of its kind exists — a second menu of those
strips → `strip relink` (`MixerDock.cpp:961`). On the lanes a clip's menu (`Timeline.cpp:584`) offers
"Play through ▸" — the strips of its kind, the current one marked "now" → `clip move <ac> --strip
<ch>` — then Piano Roll (a note clip), Duplicate, Delete.

### DR-CLIP-2/3 Patterns and linked clips (R-CLIP-2, R-CLIP-3)
`compile` (`core/Compile.cpp:125`) expands a note clip: a clip longer than its pattern loops it, a
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
`pump()` brings the heard position (rendered minus the device's latency), the latency and the peaks
into the model. Guarded by `test_live_playback_is_the_offline_render` (a fake clock device: 4 beats
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
→ 880). Every DSP registry device allocates nothing once warm (DSP REQ-device-5, `317bed2`).

### DR-PLAY-3 Meters in the model (R-PLAY-3, partly)
While playing, `pump()` copies the last block's peaks into `strips[].peak` and
`transport.masterPeak` (L/R, linear) — excluded from the stable dump. RMS, peak hold and the clip
latch are the UI's to draw from these numbers (U3).

### DR-UI-1 The window, Home and the song bar (R-UI-1, R-UI-2, R-UI-3, R-HOME-1, R-G-4)
`solaris_app` (`app/`) is a platform-free Artboard tree that links no service: `App` (`app/App.h`)
draws from `AppHooks::model()` and sends text lines through `AppHooks::dispatch` (`App::dispatch`,
`app/App.cpp:180` — a refusal becomes a toast with the service's sentence). `linux_main.cpp` is the
only OS code: GTK3, the frame clock, the pickers, the host functions, the service pumped every tick.
The screen follows `AppModel::screen` and CROSS-FADES (260 ms, `App::render`, `app/App.cpp:305`).
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
first (`App::openSettings`, `app/App.cpp:199`). Every chip is `settings set …`, every folder control
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
to the visible edge when it begins off-screen (`paintClip`, `:637`); a note clip draws its
pattern's notes repeated where it loops, the seams marked, and "linked ×N" when it shares its
pattern. Ruler: bars from 1, the grid's ticks, every label faded in by its room (DR-UI-10). The
playhead is `destructive`; it follows the transport while playing and eases 140 ms on a seek.
Ctrl+wheel zooms ×1.25 a notch on a lattice about 28 px/beat — 4.7 to 637 px/beat — eased 220 ms
and anchored at the pointer (`:618`); the wheel scrolls, Shift+wheel sideways. The grid follows the
zoom and its finest level with room is THE snap step on the lanes (DR-UI-10). A clip dragged follows
the pointer exactly, on that step, between LANES only, and lands as `clip move <ac> --at <b> [--lane
<ln>]` (`:555`) where it was let go. A click selects (the teal ring cross-fades 200 ms between
clips); Delete/Backspace → `clip delete`, Ctrl+D → `clip duplicate` (`app/App.cpp:297`); a ruler
click → `transport seek <b>` on the grid you see (`Timeline.cpp:575`). **The picture travels
(§1):** the rows are Interstellar's `AnimatedRows` keyed by lane; each clip keeps an eased beat, row
and opacity keyed by its id (`Timeline::advance`, `Timeline.cpp:301`) — it fades in when it arrives, fades out where it
was when it goes (taking no input), eases 200 ms with its row when moved from a shell; the zebra
follows the LIVE slot, a stripe's colour cross-fades, the empty-state words fade. Another song
places everything where it is. Empty, it says what to do in words.

### DR-BROWSE-2 The browser and drag and drop (R-BROWSE-1 amended, R-BROWSE-3)
`Browser` (`app/widgets/Browser.cpp`): tabs **Samples · Instruments · Song** under a highlight that
slides 220 ms. Samples lists the folders from Settings; a folder clicked is `browse "<path>"`
(`:216`) and shows its sub-folders and audio files (mono, the filename rule) under a row back up;
with no folders it says so and a click opens Settings. Instruments lists `AppModel::deviceTypes`
(the DSP registry, `core/service/ServiceModel.cpp:110`) — instruments, then effects. Song lists the
files the song plays. The list is `AnimatedRows` keyed by generation and content (`rebuild`, `:54`):
a tab or folder changed starts a new generation, so the old list fades where it was scrolled while
the new one fades in (`navigate`, `:96`); an inserted row fades in, a removed one out. A row is
dragged out: the browser reports the pointer and the drop; `ProjectScreen` draws the ghost (the
overlay pass) and the timeline's teal drop hint at the beat on the lanes' step (DR-UI-10; `overLanes`,
`app/widgets/ProjectScreen.cpp:85`), and on release sends ONE line (`place`, `:94`): a sample → `clip add --src "<file>" --at <b> [--lane
<ln>]`; an instrument → `clip add --instrument <type> --at <b> --length 4 [--lane <ln>]` (the new
strip and its empty note clip in one command, `core/service/ServiceEdit.cpp:421`); below the last
lane, no `--lane` — a new lane; an effect → a notice that it goes on a strip (U3). A double-click
places at the playhead.

### DR-UI-7 A strip's colour (R-UI-7)
The model's `strips[].colour` is resolved by the service (`core/service/ServiceModel.cpp:182`): the
strip's own, else its id's number − 1 — never −1, and unchanged when other strips are added or
deleted. Every front end draws it as is.

### DR-UI-8 The mixer dock (R-UI-3, R-MIX-1/5/6/7/9/12, R-MIX-12 amended)
`MixerDock` (`app/widgets/MixerDock.cpp`) sits under the lanes (`ProjectScreen::dockTarget`,
`app/widgets/ProjectScreen.cpp:115`: 429 px wanted; its top edge dragged follows the pointer; the
chevron folds it to its tab bar, eased 220 ms; it gives way before the lanes, which keep 130 px).
Tabs: a mixer page each, "+" (`mixer add`), Matrix; keyed (`syncTabs`) so a tab added slides the
others along, measured in one weight so choosing a tab moves nothing; the highlight slides, the
pages cross-fade as layers. A page (`syncCards`, `:149`) is a card per strip in processing order,
the strips feeding one bus (two or more) gathered under its header; the master pinned right. A card:
colour, name, "audio · N clips" / "bus · fed by N"; rack chips (four slots: the devices and "+
Effect", or two, "+N more", "+ Effect"); two sends (→ name, dB, P = pre; a third says "in the
matrix"); pan; the fader (`faderPos`, `:89`: gain ∝ position², 0 dB at 0.708, +6 at the top) beside
L/R meters (−60…+6 dBFS, green → amber at −12 → red at −3); M and S (solo amber); "→ <out>".
Commands (`handleGesture`, `:750`): a fader or pan dragged — `set <ch>.gain|pan=…` (master:
`set project.masterGain=…`) at each step, double-click → 0; M/S → `set <ch>.mute|solo=…`; "→ out"
→ cosmo's `ContextMenu` of `strips[].targets` → `route`; "+ Effect" → the registry's effects →
`device add` (`openAddEffect`, `:726`); a send → pre/post, make it the main output, remove; a send
dragged sideways → `set <sd>.gain=…`; right-click → rename (`set <ch|mx>.name=…`) or delete. A
fold header click folds its group (`:861`, the view's, eased 260 ms: members narrow to nothing, the
header widens to "→ Main · N strips"). The Matrix (`paintMatrix`, `:1236`): rows by mixer, columns
= strips off the first mixer, master, out ports; ● the main output, a send's dB; a cell routing
refuses is hatched; a click on an open cell → `send add`, on a send → its menu, a double-click →
`route`, a vertical drag → its level. **Nothing snaps** (`advance`, `:482`): every model value is
drawn through an eased copy keyed by strip (fader and pan 220 ms, M/S/dim 200, colour cross-fade
200 from what is shown, meters 40 up / 300 down); a dragged control follows the pointer exactly;
cards and tabs are keyed — arriving grows, leaving shrinks, a moved card shrinks where it was and
grows where it is. A strip a solo silences dims.

### DR-UI-5 Device panels, generated (R-UI-5, R-WIN-2/3)
`DevicePanel` (`app/widgets/DevicePanel.cpp`) is the content of a device's WINDOW (DR-WIN-1). It knows no
device: per `DeviceModel::params` it builds — once, the first time the model has the device
(`build`, `:210`), kept — a row per parameter grouped by the name's prefix (`osc1.*` → OSC1): a number
is cosmo's `SliderRow` with the opt-in `formatValue` (Hz/kHz, ms, dB, st/ct/oct signed, a 0…1 amount in
%), a `logScale` parameter through a log taper, an `integer` one rounded; a choice is a row stepping
its names. It is the parameter LIST: the value column says what decides each row (`bindingText`,
`:45`) — its number, `auto au_1`, `= ch_2.gain`, `= <formula>` cut to the column — and the row
`DeviceModel::lastChanged` names is lit, the light easing from row to row (`ParamBody`, `:90`). A
right-click offers Create Automation, Formula… (cosmo's rename field), Clear Binding, Reset to Default
(`openParamMenu`, `:417`); the App offers right-clicks to the windows first because a row's slider would
swallow them (`app/App.cpp:72`). Every change is one line: `set <dv>.<param>=…`, `auto create`,
`bind clear`, `set <dv>.bypass=…`, `device remove <dv>` (none for an instrument — it has "Piano Roll"
instead, DR-ROLL-1). While the pointer is
down `bind` re-seeds nothing (`:258`); the body scrolls with its own bar (`reveal` eases to a row).

### DR-AUTO-1 Automations and bindings in the `.slp` (R-AUTO-1, R-AUTO-4)
`Automation` (`model/Project.h`): id `au_n`, name, unit, min/max, `from`, points (beat, value, shape
`linear|hold|smooth`, sorted). `Binding`: address + formula (with its `=`), one per address. Written
after the clips as `#aauto` with `#point` children and `#abind` nodes (`serializeProject`); read by
`parseProject` (`model/Project.cpp:425`), which also reads the suite schema's earlier sketch —
indented `<beats> = <value>` lines and `node=/param=/interp=` — and normalises it once (`:480`).
`validateProject` (`:847`) refuses two bindings on one address, a formula without `=`, a binding on
something that does not exist, an empty range and an unknown shape.

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
`compile` (`core/Compile.cpp:251`) turns automations into `engine::Curve`s (points in samples) and
bindings into `engine::Bind`s — target (strip gain/pan, send gain, master gain, a device parameter by
registry index), clamp, integer, own value — with each symbol remapped: an automation → its curve's
slot, a bound address → its binding's slot, an unbound address → its own value as a constant.
`evaluateBinds` (`engine/Expr.h:156`) fills the clock, the curves (`Curve::valueAt`, `:109`) and each
binding in order (`Expr::eval`, `:51`, a fixed stack, no allocation; a non-finite result keeps the last
good value). `Engine::render` (`engine/Engine.cpp:531`) cuts pieces at every multiple of `kControl` = 64
samples and evaluates there (`evalBinds`, `:190`), so the result is the same however time is chopped;
bound gains, pans and sends ramp linearly across the period, unbound strips render exactly as before.
Values at time zero are applied before the devices' warm-up (`:180`); a seek evaluates at once
(`:259`). Measured: a 0 → −20 dB curve renders −5.00 dB at beat 1 and a formula-stepped compressor makeup
+12 dB from beat 2, byte-identical at chunks of 77, 128 and 1000, no zipper, no allocation.

### DR-AUTO-4 The service: commands, `set`, `eval`, the model (R-AUTO-1…5, 8, 9)
`set <address>="=<formula>"` binds (`core/service/SolarisService.cpp:473`); a plain number clears the
binding and sets the value; `get` prints the formula; an unquoted formula with spaces is refused with
the quoting shown. `auto create <address>` (`core/service/ServiceAuto.cpp:83`) makes `au_n` named
"<owner> · <label>", ranged as the address, holding its value from beat 0 to the song's end (at least
four bars), and binds the address to `=au_n`; `auto add|delete` (refused while read, `--unbind`),
`auto point add|move|delete|shape` (values clamped to the range), `bind clear`; deleting a strip, a
send or a device drops the bindings that drive them. `eval <address> [--at] [--explain]`
(`:213`) compiles and evaluates with the engine's own function and prints each name it reads.
`refreshModel` (`core/service/ServiceModel.cpp:272`) publishes `bindings[]` (reads, ok, problem),
`automations[]` (points, usedBy), `params[].formula`, `strips[].gainFormula/panFormula`,
`sends[].gainFormula`, `masterGainFormula` and `devices[].lastChanged`; `audit` (`:365`) names inert
bindings and automations no formula reads. While playing, a binding or curve edit — or a `set` on an
address a formula reads — swaps in a new engine (`core/service/ServiceTransport.cpp:239`).

### DR-EDM-1 Undo and redo (R-EDM-1)
After every edit that LANDED, `dispatch` keeps the song as it was before it (`core/service/SolarisService.cpp:216`),
at most 200 steps; consecutive `set`s of exactly the same addresses extend the newest step instead of
adding one, so a dragged fader is one step (a rule a script sees the same way). `undo` / `redo`
(`historyCommand`, `:263`) swap the song with the step, print its label (`set ch_2.gain`, `clip add`,
`strip delete ch_3`), emit `project.changed what=undo|redo node=<label>`, mark the song unsaved and,
while playing, swap in a new engine; any new edit clears redo. Not edits: machine settings, the
transport, a save, `get`/`eval`/`audit`. `project new|open|close` start a new history. The model names
both (`undoLabel`, `redoLabel`, `undoDepth`, `redoDepth`); the window's Ctrl+Z, Ctrl+Shift+Z and Ctrl+Y
send `undo` / `redo` (`app/App.cpp`).

### DR-UI-9 The song bar's menus and Settings beside Home (R-UI-3 amended)
`SongBar` (`app/widgets/SongBar.cpp`): wordmark · Home · Settings · cosmo's `MenuStrip` (laid out by
`SongBar::layout`, `:31`) · the song's name; the transport is centred when there is room and otherwise
starts right of the menus and `kNameMin` (120 px) of name (`transportX`, `:40`), so the bar fits at
1024; the master meter and Save on the right. `App::buildMenus` (`app/App.cpp:86`): **File** — New
Song…, Open…, Save, Save As…, Render…, Render Stems… (the host's save picker, `onPickSave`, then
`project save "<path>"`, `render --out "<path>" [--stems <every strip>]`), Home; **Edit** — Undo / Redo
naming what they would take back (from `undoLabel`/`redoLabel`), Duplicate / Delete the selected clip;
**Song** — Add Mixer, Add Bus, Add Audio Line, Add Lane; **View** — Hide/Show Mixer (the dock eases
to its tab bar), Hide/Show Browser (its width eases to 0, everything right of it follows the live
width), Metronome On/Off (`settings set metronome=…`), Settings…. Labels follow the state
(`refreshMenus`, `:122`). A press outside an open menu closes it first (`:251`), as cosmo's does.

### DR-SET-3 The settings sheet in sections (R-SET-3)
`SettingsSheet` (`app/widgets/SettingsSheet.cpp:29`): **Audio** (Output, Input, Sample rate, Buffer),
**Playback** (Metronome Off/On, Click level −18/−12/−6/0 dB), **New songs** (Tempo 100…174, Meter
4/4 3/4 6/8 7/8), **Sample folders**, **Interface** (Reduced motion) — a 12 px title per section, a
hairline between; every chip `settings set <key>=…`. The service keeps the new keys in the machine's
file (`metronome`, `metronomeLevel`, `newBpm`, `newSig`, `reducedMotion` — `core/Settings.cpp`),
`project new` starts at `newBpm`/`newSig` when no flag says otherwise, and the App ORs `reducedMotion`
with the OS's own setting into `artboard::setReducedMotion` (design rule §2.6). The sheet scrolls when
it is taller than the window (R6); `revealRect` brings a control in.

### DR-WIN-1 Windows inside the song view (R-WIN-1, R-WIN-4)
`FloatWindow` (`app/widgets/FloatWindow.cpp`): a frame — a 26 px title bar (title, ×) over its content.
Dragged by the title it follows the pointer exactly, kept inside the layer; touched anywhere it is
raised; `open`/`close` record intent and `advance` fades it (150 / 120 ms); while closing it takes no
input (`handleGesture`, `:54`). `WindowLayer` covers the song view under the song bar (over the lanes,
dock and browser) and keeps windows keyed — `dev:<dv>` (`openDevice`, `:126`: a new one cascades from
the top right) and `roll:<pt>` (`openRoll`, `:147`, DR-ROLL-1) — reopening a closed one where it was; titles follow the model ("Basic Synth — Bass")
and a window whose device is removed by anyone closes (`bind`, `:193`). Rack chips and an instrument
strip's name (double-click) in the dock open them; a chip is outlined while its window is open.
Placement and stacking are the view's. The dock's floating panel is gone (R-UI-5 amended).

### DR-AUTO-5 Automation on the timeline (R-AUTO-6)
`Timeline` (`app/widgets/Timeline.cpp:161`) appends a row per automation after the lanes, keyed
`auto:<au>` in the same `AnimatedRows` (a lane added slides them down; a new one grows in), its header
the accent stripe, its name and the address that reads it. `TimelineAuto.cpp` draws the curve over the
beat grid — holding the first value before the first point and the last after, linear / hold / smooth
(smoothstep) per segment, on a log scale for Hz and ms (`valueToY`, `:43`) — with the points as
handles (`paintAutomation`, `:161`). `autoGesture` (`:196`): a click on the row adds a point at the
lanes' snap step under the pointer (DR-UI-10; `:263`) with the value under it (`auto point add`); a
point dragged is drawn under the pointer while held (its beat on the same step, `:222`) and sends ONE `auto point move <au> --at <from> --to <to>
--value <v>` on release, staying where it was let go; a double-click deletes it; a right-click offers
Linear / Hold / Smooth, Delete Point and Delete Automation (`auto delete --unbind`). A curve the model
changes eases there point by point over 220 ms; a point added or removed cross-fades the two curves
(`shownPoints`, `:30`).

### DR-ROLL-1 The piano roll (R-ROLL-1…5, R-EDM-6)
**Grammar.** `note move <pt> --pitch <p> --at <b> [--to-pitch <p>] [--to-at <b>] [--length <b>] [--vel
<1…127>]` (`core/service/ServiceEdit.cpp:611`) edits the one note at that pitch and beat — a moved note
landing on another's place replaces it; `pattern quantize <pt> [--grid <b>] [--swing <0…0.75>]`
(`:614`) moves every start to `k·grid`, odd `k` delayed by `swing·grid`, two notes landing together
merging into the louder. Both are edits (undoable, all-or-nothing). The model gives a pattern the strip
its first clip plays through and that strip's instrument (`patterns[].strip`, `.instrument`), and a
device type its named keys (`deviceTypes[].noteNames`, `core/service/ServiceModel.cpp:113`) — the DSP
registry's (REQ-device-6: the Drum Machine's ten pads, Kick = 36 …).
**The window.** `PianoRoll` (`app/widgets/PianoRoll.cpp`) is the content of a `roll:<pt>` window
(`WindowLayer::openRoll`, `app/widgets/FloatWindow.cpp:147`), opened by double-clicking a note clip
(`Timeline.cpp:580`) or by an instrument window's "Piano Roll" (`DevicePanel.cpp:464` — the strip's
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
`<send>.sidechain` settable): `keysForward` (`model/Project.cpp:751`) allows any strip later in processing
order, its own mixer included — the validator holds a key to it and refuses the master or a port
(`:822`), an audible send stays `feedsForward`; `strips[].keyTargets` (`keyTargetsOf`, `:759`) is what a
picker offers. The engine sums a key into the target's KEY buffers instead of its input (`routeKey`,
`engine/Engine.cpp:296`) and hands them to every device whose type `takesKey` before it runs (`:396`) —
the DSP Compressor with Sidechain on detects on it (DSP REQ-fx-sidechain-1); silence when nothing keys
it. A strip silenced by another's SOLO still keys (`keyLive`, `core/Compile.cpp:161`; `Engine.cpp:410`) and
a key does not pull its source into a solo (`Compile.cpp:52`), so a soloed bass keeps its pump while the
kick stays silent; a MUTED strip keys nothing (live, a mute in a song with keys swaps the engine,
`core/service/ServiceTransport.cpp:269`). The audit names a key that no compressor with Sidechain on
hears (`core/service/ServiceModel.cpp:418`). In the dock a key reads "key <target>" in the solo amber,
and a strip's menu offers "Sidechain to ▸" = its `keyTargets` (`app/widgets/MixerDock.cpp:968`).
Measured (L2): a ghost kick (−120 dB fader, pre-fader key) through −30 dB at 4:1 dips a −6 dBFS bass
6–22.5 dB 15–35 ms after each kick, back within 1 dB before the next; identical with the bass soloed;
nothing with the kick muted.

### DR-EDM-2 The metronome (R-EDM-2, R-TIME-4)
`settings set metronome=on|off`, `metronomeLevel=<dB>` (machine settings, DR-SET-3). The click is the
`Player`'s (`core/Player.cpp:97`), after the engine, so it is in what is heard and never in a render: its
own Drum Machine, made in `start`, triggers the rim on every beat and the cowbell on the bar's first, on
the beat's exact sample, into the clock device's first two channels at the level; on/off, level and
tempo are atomics the service sets (at play, on every live edit, on `settings set`), so a change is
heard at once and the audio thread never allocates. Measured (L2): on an empty song a click starts on
each beat's sample (24 000 frames apart at 120 bpm), the bar's first differs, off silences the next
beats, and a render with it on equals one with it off byte for byte.

### DR-EDM-4 The limiter (R-EDM-4)
The DSP library's `limiter` (REQ-fx-limiter-1: gain, ceiling, release, lookahead — its latency) is a
registry effect, so it is in "+ Effect", on the master or a strip, with no Solaris code; the output
never exceeds its ceiling by construction (measured in the DSP suites).

### DR-EDM-7 The loop region on the ruler (R-EDM-7, R-TIME-4)
`Timeline` draws the model's `transport.loopFrom … loopTo` as a brace on the ruler, its ends marked, in
the accent, and tints the region on the lanes under the clips (`app/widgets/Timeline.cpp:799`, `:892`;
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
refused unread by `resolveSample` (`core/service/ServiceEdit.cpp:83`; `:186`; `SolarisService.cpp:605`),
refused on a type that plays none. Compile decodes it through the same cache as clips and puts it on the
device's description (`core/Compile.cpp:102`); the engine hands it in at build, never on the audio thread
(`engine/Engine.cpp:49`); a change of sound is a structural live update (an engine swap). The model
gives `devices[].takesSample`, `.sample` and `deviceTypes[].takesSample`; the audit names a sampler with
no sound or an unreadable one (`core/service/ServiceModel.cpp:397`). In the UI a sampler's window names
its sound where others name their type (`DevicePanel::sampleText`, `app/widgets/DevicePanel.cpp:391`),
and a browser sample dragged over it lights the window (eased) and drops as ONE `set <dv>.sample=`
(`WindowLayer::samplerAt`, `app/widgets/FloatWindow.cpp:239`; `app/widgets/ProjectScreen.cpp:79`).
Measured (L2): at the root the render IS the decoded file sample for sample (a mutant whose engine skips
`setSample` fails it), an octave up is 2 kHz and over in half the time.

### DR-EDM-9 Audition in the browser (R-EDM-9, R-BROWSE-2)
`audition <file>` / `audition stop` — a machine command (not an edit: no undo, the song stays saved),
with or without a song open. The file is decoded through the clips' cache and played by an
`Auditioner` (`core/Auditioner.cpp:40`) — its OWN output stream and thread, so a preview is heard
whether the song plays or not, at the `auditionLevel` setting (dB, default −6), to its end, then it
stops itself; it never allocates on its thread and is never in a render (a render does not know it).
`auditionCommand` (`core/service/ServiceTransport.cpp:88`) refuses a file it cannot read naming it; the
service's `pump` (`:62`) carries its progress into `audition.{file, playing, progress}` and announces its
end (`audition.changed … playing=0`); the model keeps it across refreshes, and the App re-binds while it
plays and once when it ends (`app/App.cpp:223`). In the browser a click on a sample hears it, a click on
the one being heard stops it (`app/widgets/Browser.cpp:246`); its row fills as it plays with an accent
bar along its foot, keyed by the file and eased in and out (`:165`, `:312`) — a double-click still places
it. Settings › Playback has PREVIEW LEVEL. Measured (L2): the live stream carries the decoded file at
the audition level, sample for sample; it ends itself, said.


### DR-UI-10 The grid follows the zoom (R-UI-10, R-TIME-5)
**The levels.** The lanes' grid has seven: a bar (the song's meter), a beat, and 1/2, 1/4, 1/8, 1/16
and 1/32 of a beat (`Timeline::gridSpan`, `app/widgets/Timeline.cpp:58`). How much of a level is drawn
is a smoothstep of its line spacing at the EASED zoom — nothing closer than 6 px, all of it from 16 px
apart (`kGridHidePx`, `kGridFullPx`, `app/widgets/Timeline.h:73`; `room`, `Timeline.cpp:29`;
`gridAlpha`, `:64`) — recomputed every frame, so a Ctrl+wheel zoom (eased 220 ms) fades a level in or
out with it and never pops one. A line is drawn once, by the coarsest level it belongs to: bars
strongest (white 7 %), beats next (4 %), the divisions faintest (2.2 %); every fourth bar is always
drawn, so a far-out song keeps its phrases (`paintGrid`, `:697`). The ruler repeats the levels as ticks
— long for a bar, shorter as they get finer — and its labels are MEASURED against their room: a bar's
number fades in as its every-1/2/4/… bars get room for the widest number in view, and zoomed in the
beats are named `bar.beat` (`2.3`), fading in as a beat gets room for one (`paintRuler`, `:719`; the
labels `:751`). The old fixed thresholds, which swapped the labels in one frame mid-zoom, are gone.

**The step.** THE snap step is the finest level whose lines are at least 11 px apart (`kSnapPx` — past
half drawn: you snap to lines you can see), never coarser than a bar; at the deepest zoom it is 0 and
nothing snaps (`snapStep`, `:72`; `atDeepestZoom`, `:70`). `snap()` (`:80`) is the ONE rounding of
every gesture on the lanes: a ruler click → `transport seek` (`:575`), a clip drag (`:538`), the loop's
Shift-drag (`:469`), an automation point added or dragged (`app/widgets/TimelineAuto.cpp:263`, `:222`),
the browser's drop and its hint (`app/widgets/ProjectScreen.cpp:89`) — at the deepest zoom the tick
under the pointer. Every gesture is still ONE command, its beats printed to the tick: rounded to 1/960
and spelled with the fewest decimals that keep it (`Timeline::beatText`, `Timeline.cpp:43` — the `%g`
it replaces kept six significant digits, so past beat 1000 a line lost ticks). The step is published
LIVE for tests (`snapStep`, `snapLabel`, `gridAlpha`, `snapLabelAmount`, `Timeline.h:102`) and named in
the ruler's corner over the lane headers — `Snap Bar`, `1/4` … `1/128`, `Off`, in the piano roll's note
values (a beat = 1/4) — cross-faded 200 ms when it changes, older names fading out from where they are
however fast the wheel turns (`stepName`, `Timeline.cpp:87`; `advance`, `:310`; `paintRuler`, `:723`).

**The zoom.** A lattice of ×1.25 notches about 28 px/beat, 8 out (4.7 px/beat: a 4/4 bar is 18.8 px)
and 14 in (637 px/beat), so a notch in and a notch out are exact inverses and the deepest zoom is one
place (`:620`). **Decision:** the deepest zoom rose from 320 px/beat so that 1/32 of a beat gets room —
20 px apart at 637, fully drawn, and THE step one notch before snapping turns off. The step by zoom:
a bar below 11.5 px/beat, a beat from 11.5, 1/2 from 22.4 (a song opens at 28: bars, beats and halves
drawn, the step 1/2), 1/4 from 55, 1/8 from 107, 1/16 from 209, 1/32 from 408, nothing at 637.

**Proof.** `test_the_grid_follows_the_zoom` (`app/tests/ui/uiTests.cpp:1089`) drives the real service:
at the default zoom a ruler click at beat 3.3 seeks 3.5 (a fixed quarter says 3.25); a level's alpha
is caught mid-fade during an eased two-notch zoom, between its two ends; at 1/8 a seek, a clip drag
(on the step while held, then `clip move ac_1 --at 3.125`) and an instrument dropped from the browser
land on the same step, the step's name caught mid cross-fade; at the deepest zoom a click seven ticks
past beat 2 is exactly `transport seek 2.007`; all the way out a click at 9.9 is `transport seek 8`.
Mutants — a fixed 1/4 snap, a level's alpha taken from the TARGET zoom (it pops), a deepest zoom that
snaps — each fail it. Shots `lanes-zoomed-in`, `lanes-zoomed-out`, `lanes-zoom-mid` (mid Ctrl+wheel)
(`app/tests/shots/renderShots.cpp:165`).
