# Solaris — architecture

The layers of `arstro.rule` §1, applied to a DAW. **The sound is the DSP library's** (R-DSP-1):
Solaris hosts devices and routes their output; it implements no DSP.

```
   host          GTK window · FFmpeg decode · WAV writer · PulseAudio devices · settings/recents files
   ───────────────────────────────────── seam: SolarisService::Host ──────────────────────────────
   front ends    GUI (app/, over AppHooks) · solaris-cc · tests          (peers — all send TEXT)
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
| `apps/solaris/model/` | `solaris_model` | — | `Project` (.slp: parse, serialize, validate, `newProject`; `feedsForward` + `targetsOf`, the forward-only rule once; `Automation` + `Binding`, R-AUTO), `Format` (canonical numbers/beats/seconds, quoting, line tokens) | R-FMT, R-MIX-3/4 |
| `apps/solaris/engine/` | `solaris_engine` | `arstro_dsp` | `MixGraph` (plain data), `Engine` (build → warm → render pieces split at note events; meters, stems, live params, seek), `MixLaws` (Interstellar's pan/fade), `Expr` (a formula's postfix program, `Curve`, `Bind`, `evaluateBinds` — evaluated every 64 samples) | R-MIX, R-DSP-5, R-PLAY, R-RENDER-1 |
| `apps/solaris/core/` | `solaris_core` | model, engine | `Settings` (the machine's, R-SET-2); `AudioOut` (the output seam); `Player` (the engine on a thread: Live messages, engine swaps, atomics); `Compile` (.slp → MixGraph: solo, patterns, registry-checked params, curves and bindings); `Formula` (a formula's text → `Expr`); `Bindings` (`describeAddress`, `orderBindings`, `checkBinding`); `service/`: `SolarisService` (+ `ServiceEdit`, `ServiceModel`, `ServiceRender`, `ServiceMachine`, `ServiceTransport`, `ServiceAuto` — automation, formulas, `eval`), `Command` (the grammar table), `Event`, `AppModel` + `AppModelCodec`, `ApiDoc`, `Json` | R-SVC, R-API, R-MIX, R-CLIP, R-RENDER |
| `apps/solaris/host/` | `solaris_host` | core, FFmpeg + libpulse (optional) | `AudioFiles` (FFmpeg decode → stereo float at the project rate; WAV 24/32f), `Machine` (folder listing, PulseAudio device list, XDG paths), `AudioOutPulse` (the clock device's stream) | R-SVC-4, R-DEV, R-SET, R-BROWSE-1 |
| `apps/solaris/cli/` | `solaris-cc` | host | argv/stdout only — every verb is the grammar; `solaris_api_current` drift test | R-SVC-1, R-API-1 |
| `apps/solaris/app/` | `solaris_app` | Artboard, cosmo widgets, Interstellar's header helpers | `App` (screens, toast, modals, keys), `Theme` (aliases + teal), `AppHooks`, widgets `HomeScreen`, `SettingsSheet`, `SongBar`, `ProjectScreen` (lays out the song view and the dock's height; turns a browser drop into one command line), `Browser` (Samples · Instruments · Song; drags out), `Timeline` (lanes, clips, ruler, playhead; zoom, clip drag), `MixerDock` (mixer pages, the master, the matrix, folds), `DevicePanel` (a device's parameters, generated from the model's registry data); cosmo's `ContextMenu`, `SliderRow` compiled from `apps/cosmo`; Interstellar's `TextFit`, `EasedScroll`, `Glyphs`, `AnimatedRows`, `FadePage` included in place; `tests/` (Rig over the real service, shots, UI tests); `NOTES.md` | R-UI, R-HOME, R-SET, R-BROWSE, R-LANE, R-MIX (view) |
| `apps/solaris/linux_main.cpp` | `solaris` | app, host, GTK3 | the window: the only OS code in the GUI | R-UI-1 |

*(Rows land with the code; a row whose directory does not exist yet is the plan, and says so in
`PROGRESS.md`.)*

## Data flow

1. A text line arrives (GUI, CLI, script) → `parseCommand` against the table → `dispatch`.
2. The service edits the `Project` (model), re-validates, and **compiles** it into a `MixGraph`:
   strips in processing order (mixer order, then strip order — forward-only routing makes this a
   topological order for free), each strip's devices (registry type + parameter values), its
   audio regions (decoded PCM from the host, at the project rate), its note events in samples, its
   output and sends as indices.
3. The engine renders blocks of the graph: per strip, sum its inputs, add its clips or play its
   instrument (split at note events), run its rack, tap pre-fader sends, apply fader and pan, tap
   post-fader sends, add into its output. Master → rack → gain → ports.
4. Offline render writes ports/stems through the host's writer. Live playback runs the same render
   on the clock device's callback (P1).

## Threading (live — DR-PLAY-1/2)

The service owns the project and compiles graphs on its thread. Devices are constructed there too.
A **parameter** edit reaches the audio thread as a message on a lock-free queue; a **structural**
edit hands over a whole new engine state the same way, and the old one is destroyed back on the
service thread. The audio thread never allocates, locks or does I/O (R-PLAY-2).

## Known limits

- `arstro::AudioConfig` is a process singleton (R-NFR-7): one sample rate per process.
