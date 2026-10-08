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
longest verb match, flags checked against the row) and `dispatch` (`core/service/SolarisService.cpp:84`)
runs it; out come the `AppModel` (`core/service/AppModel.h`, refreshed after every command) and
`Event`s whose `formatEvent` text is the log line. **Every edit is all-or-nothing**: the project is
copied first, the command runs, `validateProject` runs, and any failure restores the copy
(`:188`) and refuses with the validator's sentence — a refused command changes nothing. An edit's
events (`project.changed` from `changed`, `:34`; a `set` line's `params.changed`) are HELD in
`mPending` and emitted only once the whole command has landed (`:197`), so a refused command
announces nothing either (D-1). The core
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
`clip add --src` (`core/service/ServiceEdit.cpp:353`): a file no clip uses yet gets a new audio strip
named after it on the first mixer, routed by `defaultOutFor` (`:70`) to the first bus on a later
mixer ("Main"), and a new lane; a file already used reuses its strip; `--strip` overrides. The file
is decoded through the host to learn its length (refused if it cannot be read), and stored relative
to the song's folder when inside it (`relativePath`). A relative `--src` names a file in the SONG's
folder first when one is there (`:374`) — what the model stores and the browser's Song tab hands
back — else the caller's working directory.

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
`app/App.cpp:99` — a refusal becomes a toast with the service's sentence). `linux_main.cpp` is the
only OS code: GTK3, the frame clock, the pickers, the host functions, the service pumped every tick.
The screen follows `AppModel::screen` and CROSS-FADES (260 ms, `App::render`, `app/App.cpp:202`).
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
wrap, a filled accent when chosen — the fill EASES over 200 ms whoever chose it (`advance`, `:101`) —
the folder list with × per folder and "Add folder…", Done; 150 ms open / 120 ms close; Escape, Enter,
Done and the scrim close it; the card scrolls when the window is short. Opening sends `devices list`
first (`App::openSettings`, `app/App.cpp:118`). Every chip is `settings set …`, every folder control
`folder add|remove` (`handleGesture`, `:158`); the sheet draws the model, so a setting changed from a
shell shows at once.

### DR-UI-6 Shots and UI tests over the real service (R-UI-6)
`solaris_app_shots` renders fourteen named states — Home empty and with cards, Settings at rest,
mid-fade and mid-chip-ease, the song, Home→song mid-cross-fade, a refusal toast, the browser's
instruments and a browsed folder, a sample dragged mid-way, a clip mid-drag, a clip selected while
zoomed, the unsaved confirm — at 1440×900 and 1024×640, over the REAL `SolarisService` (`app/tests/Rig.h`: fake devices,
folders and decoder; a fixed 16 ms clock); `--check` fails a blank frame. `solaris_app_ui` (9
tests) clicks and drags the geometry the widgets publish and asserts the lines sent, the model that came back,
and a LIVE value caught mid-tween for every transition (the cross-fade, the sheet's fade, a chip's
fill, the close). The window ran on this machine's display (`solaris song.slp`, 4 s, no crash).

### DR-UI-3 The lanes (R-UI-3, R-LANE-1, R-CLIP-1…4, R-TIME-4, R-UI-7)
`ProjectScreen` (`app/widgets/ProjectScreen.cpp`): the song bar on top, the browser on the left
(234 px), `Timeline` (`app/widgets/Timeline.cpp`) filling the rest. A row per lane in lane order,
then a row per strip whose clips have no lane ("its strip's row"); a lane with no colour wears its
first clip's. A clip is drawn on its row, coloured by its STRIP (`surface::track`), its name pinned
to the visible edge when it begins off-screen (`paintClip`, `:355`); a note clip draws its
pattern's notes repeated where it loops, the seams marked, and "linked ×N" when it shares its
pattern. Ruler: bars from 1, labels thinned as it zooms out. The playhead is `destructive`; it
follows the transport while playing and eases 140 ms on a seek. Ctrl+wheel zooms 4–320 px/beat,
eased 220 ms and anchored at the pointer (`:331`); the wheel scrolls, Shift+wheel sideways. A clip
dragged follows the pointer exactly, snapped to 1/4 beat, between LANES only, and lands as `clip
move <ac> --at <b> [--lane <ln>]` (`:303`) where it was let go. A click selects (the teal ring
cross-fades 200 ms between clips); Delete/Backspace → `clip delete`, Ctrl+D → `clip duplicate`
(`app/App.cpp:200`); a ruler click → `transport seek <b>`, snapped (`Timeline.cpp:323`). **The picture travels
(§1):** the rows are Interstellar's `AnimatedRows` keyed by lane; each clip keeps an eased beat, row
and opacity keyed by its id (`Timeline::advance`, `Timeline.cpp:197`) — it fades in when it arrives, fades out where it
was when it goes (taking no input), eases 200 ms with its row when moved from a shell; the zebra
follows the LIVE slot, a stripe's colour cross-fades, the empty-state words fade. Another song
places everything where it is. Empty, it says what to do in words.

### DR-BROWSE-2 The browser and drag and drop (R-BROWSE-1 amended, R-BROWSE-3)
`Browser` (`app/widgets/Browser.cpp`): tabs **Samples · Instruments · Song** under a highlight that
slides 220 ms. Samples lists the folders from Settings; a folder clicked is `browse "<path>"`
(`:216`) and shows its sub-folders and audio files (mono, the filename rule) under a row back up;
with no folders it says so and a click opens Settings. Instruments lists `AppModel::deviceTypes`
(the DSP registry, `core/service/ServiceModel.cpp:97`) — instruments, then effects. Song lists the
files the song plays. The list is `AnimatedRows` keyed by generation and content (`rebuild`, `:54`):
a tab or folder changed starts a new generation, so the old list fades where it was scrolled while
the new one fades in (`navigate`, `:96`); an inserted row fades in, a removed one out. A row is
dragged out: the browser reports the pointer and the drop; `ProjectScreen` draws the ghost (the
overlay pass) and the timeline's teal drop hint at the snapped beat, and on release sends ONE line
(`place`, `app/widgets/ProjectScreen.cpp:66`): a sample → `clip add --src "<file>" --at <b> [--lane
<ln>]`; an instrument → `clip add --instrument <type> --at <b> --length 4 [--lane <ln>]` (the new
strip and its empty note clip in one command, `core/service/ServiceEdit.cpp:359`); below the last
lane, no `--lane` — a new lane; an effect → a notice that it goes on a strip (U3). A double-click
places at the playhead.

### DR-UI-7 A strip's colour (R-UI-7)
The model's `strips[].colour` is resolved by the service (`core/service/ServiceModel.cpp:164`): the
strip's own, else its id's number − 1 — never −1, and unchanged when other strips are added or
deleted. Every front end draws it as is.
