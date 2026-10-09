# Solaris — architecture

The layers of `arstro.rule` §1, applied to a DAW. **The sound is the DSP library's** (R-DSP-1):
Solaris hosts devices and routes their output; it implements no DSP.

```
   host          GTK window · FFmpeg decode · WAV writer · PulseAudio devices · settings/recents files
                 · the control socket · the NTWB bridge
   ───────────────────────────────────── seam: SolarisService::Host ──────────────────────────────
   front ends    GUI (app/) · solaris-cc · control channel (attach) · web (NTWB) · tests
                                                                          (peers — all send TEXT)
   ───────────────────────────────────── seam: dispatch(text) / model() / events ─────────────────
   service       core/   SolarisService: grammar table, events, AppModel, compile(project → graph)
   model         model/  the .slp document: parse, serialize, validate — no sound, no I/O
   engine        engine/ MixGraph → strips, racks, sends, ports, meters — knows no project, no file
   DSP           core/DigitalSignalProcessing: every instrument, every effect, Device + registry
```

## Module map

| directory | target | depends on | holds | requirement |
|---|---|---|---|---|
| `core/DigitalSignalProcessing/src/` | `arstro_dsp` | — | `Biquad`, `ParametricEQ`, `StateVariableFilter`, `Noise`, `DecayEnvelope`, `BasicSynth`, `DrumMachine`, `Device` + `DeviceRegistry`, the existing `Compressor`/`Reverb`/`Repeater`/`Chorus`/`Overdrive` | R-DSP, R-INST, R-FX |
| `apps/solaris/model/` | `solaris_model` | — | `Project` (.slp: parse, serialize, validate, `newProject`; `feedsForward` + `targetsOf` + `matrixColumnsOf`, the forward-only rule once — later in processing order; `Automation` + `Binding`, R-AUTO), `Format` (canonical numbers/beats/seconds, quoting, line tokens) | R-FMT, R-MIX-3/4 |
| `apps/solaris/engine/` | `solaris_engine` | `arstro_dsp` | `MixGraph` (plain data), `Engine` (build → compensate latency → warm → render pieces split at note events; meters, stems, live params, seek), `MixLaws` (Interstellar's pan/fade), `Expr` (a formula's postfix program, `Curve` — Interstellar's keyframes, its `model/Anim.h` included in place, `curveKeys` the one mapping of a point's shape onto them — `Bind`, `evaluateBinds` — evaluated every 64 samples) | R-MIX, R-DSP-5, R-PLAY, R-RENDER-1, R-AUTO-10 |
| `apps/solaris/core/` | `solaris_core` | model, engine | `RegistryModel` (`deviceModelOf`: the ONE registry type → DeviceModel mapping, shared with the plugins' editor, R-VST-7); `Settings` (the machine's, R-SET-2); `AudioOut` (the output seam); `Player` (the engine on a thread: Live messages, engine swaps, atomics; the metronome, after the engine — heard, never rendered; the bindings' evaluated values, block by block, in a seqlocked ring of atomics read for the HEARD position, R-MIX-16); `Auditioner` (the browser's preview: its own stream and thread, R-EDM-9); `Compile` (.slp → MixGraph: solo, patterns, registry-checked params, curves (`compileCurve`, also the model's `automations[].now`) and bindings); `Formula` (a formula's text → `Expr`); `Bindings` (`describeAddress`, `orderBindings`, `checkBinding`); `service/`: `SolarisService` (+ `ServiceEdit`, `ServiceModel`, `ServiceRender`, `ServiceMachine`, `ServiceTransport`, `ServiceAuto` — automation, formulas, `eval`; `ServiceCompose` — notes in bulk and by name, step rows, pattern edits, `ls`/`show`/`pattern print`, R-SVC-8), `Command` (the grammar table), `Notation` (pitch names, chords, the note token, step rows — pure, R-SVC-8), `Event`, `AppModel` + `AppModelCodec`, `ApiDoc`, `Json` | R-SVC, R-API, R-MIX, R-CLIP, R-RENDER |
| `apps/solaris/host/` | `solaris_host` | core, FFmpeg + libpulse (optional) | `AudioFiles` (FFmpeg decode → stereo float at the project rate; WAV 24/32f), `Machine` (folder listing, PulseAudio device list, XDG paths), `AudioOutPulse` (the clock device's stream) | R-SVC-4, R-DEV, R-SET, R-BROWSE-1 |
| `apps/solaris/host/` | `solaris_control` (POSIX) | core; cosmo's `ControlChannel.cpp` compiled in place | `ControlServer` (the window's control channel: each line to `dispatchText`, answered `[out]`/`[ok]`/`[refused]` after its `[evt]` lines; `wait` holds the queue, never the thread), `ControlLog` (the one log symbol cosmo's channel needs) — GTK-free, so it is tested with no window | R-SVC-5 |
| `apps/solaris/cli/` | `solaris-cc` | host, `arstro_ntwb` | argv/stdin/stdout only — every verb is the grammar; `--script`, `shell` (one session on stdin), `refused: line N`, exit 4 on unsaved edits; `Faces.h`: `attach` (`Attach.cpp`, a terminal on a running window — builds NO service), `ntwb serve\|install\|uninstall\|api` (`NtwbMain.cpp`, `NtwbAdapter` — the service on the NTWB bridge); `solaris_web_tokens` (`webTokens.cpp`: the web's tokens from Theme.h); drift tests `solaris_api_current`, `solaris_ntwb_api_current`, `solaris_web_tokens_current`; `solaris_cli_session` (`tests/cli_session.cmake`), `solaris_demo_canon` (`tests/demo_canon.cmake`) | R-SVC-1, R-SVC-5, R-SVC-7, R-SVC-8, R-SVC-9, R-API-1 |
| `apps/solaris/demo/canon/` | — | `solaris-cc` | a song made with the CLI alone: `make_script.py` → `song.txt` (committed), `measure.py` | R-SVC-9 |
| `apps/solaris/web/` | — (copied by `ntwb install`) | `/ntwb/ntwb.js` (Arstro Remote); cosmo's `js/core/signal.js` + `dom.js` and fonts, copied beside it | the web face, MVVM, no build step: `js/model/session.js` (NTWB → signals), `js/vm/song.js` (shared shapes, view state, one command line per intent), `js/views/` (song bar, Home, lanes, mixer, console), `css/tokens.css` (GENERATED) + `base.css` | R-SVC-7 |
| `apps/solaris/tests/` | `solaris_control_tests`, `solaris_ntwb_tests`, ctest `solaris_equivalence` | the binaries above | `faces/` (the channel with no window; attach = solaris-cc byte for byte; the adapter over `ntwb::MemoryTransport`; `ntwb install` into a scratch dir), `acceptance/run.sh` + `a-song-made-twice.txt` (R-SVC-6: one script through solaris-cc and a LIVE window — skipped without a display), `api_current.cmake` | R-SVC-5…7, R-API-1 |
| `apps/solaris/app/` | `solaris_app` | Artboard, cosmo widgets, Interstellar's header helpers | `App` (screens, toast, modals, keys), `Theme` (aliases + teal), `AppHooks`, widgets `HomeScreen`, `SettingsSheet`, `SongBar`, `ProjectScreen` (lays out the song view and the dock's height; turns a browser drop into one command line), `Browser` (Samples · Instruments · Song; drags out), `Timeline` (lanes, clips, ruler, playhead; zoom, clip drag), `MixerDock` (mixer pages, the master, the matrix, folds; bound numbers drawn at their live values, R-MIX-16), `DevicePanel` (a device's parameter list, generated from the model's registry data — a window's content), `ParamMenu` (the ONE right-click menu of every number a formula can drive — a device row, a fader, a pan, a send, the master; Create Automation, Formula…, Clear Binding, Reset, Copy Address / Value / as Formula — and `drawNameWithId`, how every widget draws an id, R-MIX-16, R-UI-11), `FloatWindow` + `WindowLayer` (windows inside the song view), `PianoRoll` (a pattern's notes and steps — a window's content), `AutomationPanel` (an automation's facts — its window's content, R-AUTO-11); cosmo's `ContextMenu`, `SliderRow` compiled from `apps/cosmo`; Interstellar's `TextFit`, `EasedScroll`, `Glyphs`, `AnimatedRows`, `FadePage` included in place; `tests/` (Rig over the real service, shots, UI tests); `NOTES.md` | R-UI, R-HOME, R-SET, R-BROWSE, R-LANE, R-MIX (view), R-WIN, R-ROLL |
| `apps/solaris/plugins/` | `solaris_instrument_editor` (PIC); `X11View.cpp` compiled INTO the DSP repo's plugin modules `arstro_vst3_synth`, `arstro_vst3_drums` | `solaris_app` (DevicePanel), `arstro_dsp`, the VST3 SDK, X11, cairo-xlib | the plugins' own editor (R-VST-7): `InstrumentEditor` (platform-free: a model of one device from the host's values by `deviceModelOf`, Solaris's DevicePanel with `songControls` off, the panel's lines → the host's begin/perform/end through `ParamAccess`), `PluginAccess.h` (`ControllerAccess`: the controller's edit calls), `X11View.cpp` (`createEditor`: an XEmbed child window driven by the host's IRunLoop); `tests/editorTests.cpp` (`solaris_plugin_editor`) | R-VST-7, R-VST-8 |
| `apps/solaris/linux_main.cpp` | `solaris` | app, host, `solaris_control`, GTK3 | the window: the only OS code in the GUI; `--control <socket>` polls a `ControlServer` from its own 16 ms timeout | R-UI-1, R-SVC-5 |

*(Rows land with the code; a row whose directory does not exist yet is the plan, and says so in
`PROGRESS.md`.)*

## The faces (R-SVC-5…7, arstro.rule §1)

One service, four ways to reach it, all sending the same TEXT and reading the same events and model:

| face | where the service lives | how a line arrives | what comes back |
|---|---|---|---|
| the GUI (`solaris`) | in the window's process | a gesture → `AppHooks::dispatch` | the model it draws |
| the CLI (`solaris-cc`) | in the CLI's process | argv, ` : `, `--script` | output on stdout, `--watch` events on stderr |
| the control channel (`solaris --control` + `solaris-cc attach`) | in the window's process — attach builds none | a Unix line socket (cosmo's `ControlChannel`), polled on the UI thread | `[evt]` lines, then `[out]` + `[ok]`/`[refused]` per line; attach prints them as solaris-cc does |
| the web (`solaris-cc ntwb serve` + `web/`) | in the `serve` process, one per Arstro Remote session | an NTWB call `command {line}` | NTWB state `model` (= `state print --json`) and `transport`, events by name |

The equivalence test (`tests/acceptance/run.sh`) runs one committed script through the CLI and a live
window and requires the same events, outputs, stable state, song and mix (DR-SVC-5).

## Data flow

1. A text line arrives (GUI, CLI, script) → `parseCommand` against the table → `dispatch`.
2. The service edits the `Project` (model), re-validates, and **compiles** it into a `MixGraph`:
   strips in processing order (mixer order, then strip order — forward-only routing makes this a
   topological order for free), each strip's devices (registry type + parameter values), its
   audio regions (decoded PCM from the host, at the project rate), its note events in samples, its
   output and sends as indices.
3. The engine renders blocks of the graph: per strip, sum its inputs, add its clips or play its
   instrument (split at note events), run its rack, tap pre-fader sends, apply fader and pan, tap
   post-fader sends, add into its output. Master → rack → gain → ports. **Latency is compensated**
   (R-MIX-17, DR-MIX-17): at build the engine reads every device's `latency()` (the DSP library's —
   a limiter's lookahead) and sizes a delay per connection so every signal meets in time at each
   strip, bus, the master and the ports; what comes out is `outputLatency()` behind the song.
4. Offline render writes ports/stems through the host's writer, trimming `outputLatency()` (a stem
   its strip's own) so every file starts at its first beat exactly. Live playback runs the same
   render on the clock device's callback (P1); the heard position subtracts the device's latency
   and the engine's.

## Threading (live — DR-PLAY-1/2)

The service owns the project and compiles graphs on its thread. Devices are constructed there too.
A **parameter** edit reaches the audio thread as a message on a lock-free queue; a **structural**
edit hands over a whole new engine state the same way, and the old one is destroyed back on the
service thread. The audio thread never allocates, locks or does I/O (R-PLAY-2).

## Known limits

- `arstro::AudioConfig` is a process singleton (R-NFR-7): one sample rate per process.
- Bound values (automation, formulas) are evaluated at the engine's position, so one applied after a
  latent device lands up to that path's latency early (≤ 10 ms); a latency an automation moves is
  compensated at its starting value (DR-MIX-17).
