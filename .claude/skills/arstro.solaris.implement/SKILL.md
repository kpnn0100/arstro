---
name: arstro.solaris.implement
description: Use to implement or resume ANY work in Solaris, the Arstro DAW — its instruments and effects (whose sound is built IN the DSP library, never in the app), the Device registry, the .slp model, the mixer (every sample and instrument its own strip, mixers as ordered pages, forward-only routing, sends, the matrix, the audit), lanes, clips and shared patterns, the engine and offline render, live playback (one clock device now, drift-corrected followers next), the SolarisService grammar/events/model and its GENERATED API document, the solaris-cc CLI, versions, automation and parameter formulas, VST3 (wrapping our instruments as plugins, hosting other people's), AND the UI (Home, Settings, the song view: browser, lanes, the docked mixer, device panels, piano roll; cosmo/interstellar family, teal accent). Runs the V-model with both requirement tiers in sync, verifies headlessly by driving the real service through text command lines, by MEASURING rendered audio and by rendering real frames, keeps the API document in step, records progress in the committed ledger, and commits. Invoke for "add an instrument/effect", "the mixer does X", "add a command/address", "render to Y", "add automation", "make it a VST3", "add a panel/window", "continue solaris", "/arstro.solaris.implement". NOT for diagnosing a reported problem without fixing it — that is arstro.solaris.debug.
---

# arstro.solaris.implement

> **Invoke `arstro.rule` first; `arstro.design.rule` when the task touches a pixel; `arstro.dsp.implement`
> when it touches a sample of sound.** They own the core/front-end split, requirements-first, the
> V-model doc sync, the ledger/defect/commit conventions, the design law and the DSP law. This file
> is Solaris's map, laws and recipes on top of them.

**Solaris is a DAW whose sound lives in the DSP library.** The user's words when they asked for the
first instruments: *"make sure all the core of those instruments and basic filters is in dsp."* If a
change puts an oscillator, a filter, an envelope, a compressor or a reverb into `apps/solaris/`, it is
the wrong change — however small, however convenient. The same holds for a VST3 build of our
instruments: the plugin WRAPS the DSP class, it never carries a copy of the sound.

Why the app is shaped as it is — one strip per source, mixers as pages, forward-only routing,
versions as Interstellar's — is `apps/solaris/docs/discussion.md`. Read it once.

**You are the only skill that changes product code.** `arstro.solaris.debug` reproduces, files and
recommends; when a task names a `D-n`, read its entry in `docs/DEFECTS.md` before re-deriving
anything — the reproduction and the guarding test are already written there.

---

## 0. Orient — read these, in this order, before touching code

1. `apps/solaris/docs/PROGRESS.md` — **NEXT** is the task unless the user named another. If the ledger
   disagrees with the tree, reconcile it first and say so (`arstro.rule` §0).
2. `apps/solaris/REQUIREMENTS.md` — intent (R-). `docs/requirements.md` — as built (DR-, with
   `file:line` anchors). A behaviour without a DR entry does not ship.
3. `apps/solaris/docs/API.md` — **generated**: every command with its grammar, every event, every
   model field, every address, every device parameter with unit and range. What the code accepts
   TODAY — build scripts from it, not from memory.
4. `docs/project-format.md` (the `.slp`), `docs/architecture.md` (layers + module map),
   `../../docs/audio-format.md` (the suite audio schema shared with Interstellar — `#aauto` lives
   there), `docs/DEFECTS.md`.
5. For the UI: `apps/solaris/app/NOTES.md` — every control's command line, the shot list, the
   borrowed widgets, the gotchas.
6. For sound: `core/DigitalSignalProcessing/docs/requirements.md` (REQ- ids) and the README of every
   module you will touch (each has a `## Math`).

### The code map

| directory | library | depends on | what lives there |
|---|---|---|---|
| `core/DigitalSignalProcessing/src/` | `arstro_dsp` (submodule, `feature/1.0.0`) | — | **all sound**: `Oscillator`, `Biquad`/`ParametricEQ`, `StateVariableFilter`, `Noise`, `DecayEnvelope`, `ADSREnvelope`, `BasicSynth`, `DrumMachine`, the effects, `Device` + `DeviceRegistry` (one description per parameter) |
| `core/DigitalSignalProcessing/apps/` | demos | `arstro_dsp` | `kitchen_sink`, `piano_demo`, `wav_demo` — the place for a plugin wrapper (§3, VST3) |
| `apps/solaris/model/` | `solaris_model` | — | `Project` (.slp parse / serialize fixed point / validate / repair / `newProject`), `feedsForward` + `targetsOf` (the forward-only rule, ONCE), `Format` |
| `apps/solaris/engine/` | `solaris_engine` | `arstro_dsp` | `MixGraph` (plain data), `Engine` (build → warm → render pieces split at note events; meters, stems, live setters), `MixLaws` — **knows no project and no file** |
| `apps/solaris/core/` | `solaris_core` | model, engine | `Compile` (.slp → MixGraph: solo, patterns, registry-checked params), `Settings`, `AudioOut` (the output seam), `Player` (the engine on a thread, lock-free messages, engine swaps) |
| `apps/solaris/core/service/` | (in core) | | `Command` (the grammar TABLE), `Event`, `AppModel` (frozen UI contract) + `AppModelCodec`, `ApiDoc`, `Json`, `SolarisService` + `ServiceEdit` (mixer/clip edits, `setAddress`/`getAddress`), `ServiceModel` (`refreshModel`), `ServiceRender`, `ServiceMachine` (settings, folders, devices, browse, recents), `ServiceTransport` (play/stop/seek/loop/wait, live updates) |
| `apps/solaris/host/` | `solaris_host` | FFmpeg, libpulse | `AudioFiles` (decode, WAV), `Machine` (folders, PulseAudio devices, XDG paths), `AudioOutPulse` |
| `apps/solaris/cli/` | `solaris-cc` | host | argv/stdout only — every verb is the service grammar |
| `apps/solaris/app/` | `solaris_app` | Artboard, cosmo widgets, Interstellar's header helpers | `App`, `Theme` (aliases + teal), `AppHooks`; widgets `HomeScreen`, `SettingsSheet`, `SongBar`, `ProjectScreen`, `Browser`, `Timeline`, `MixerDock`, `DevicePanel`; `tests/` (`Rig`, shots, UI tests) |
| `apps/solaris/linux_main.cpp` | `solaris` | app, host, GTK3 | the window — the only OS code in the GUI |

**Do not run a command this file names for a directory or target that does not exist yet** — check
the ledger; build it (it is probably NEXT) or say it is missing.

---

## 1. The laws (each one is a requirement, a decision or a defect already paid for)

1. **The sound is the DSP library's** (R-DSP-1). A new instrument or effect is built in
   `core/DigitalSignalProcessing` under `arstro.dsp.implement` (compose existing primitives first,
   derive the math, `## Math` in its README, unit + integration tests, a `REQ-` id), then given a
   `DeviceRegistry` entry there. Solaris then has it with **no Solaris code**: the registry drives
   the `.slp` keys, the address space, the API document and the generated device panel (R-DSP-2,
   R-UI-5).
2. **The registry is the only description of a parameter** — name, label, unit, range, default,
   choices, `logScale`, `integer`, once, in the DSP library. Never restate a range in Solaris or in a
   plugin wrapper; read it. A VST3 parameter's normalised 0…1 is a MAPPING of that description, done
   by one shared function, so the plugin, the service and the panel agree to the last digit.
3. **A channel IS a strip** (R-MIX-1); **every sample file gets its own strip** (R-MIX-2) and the same
   file again reuses it. No insert number between a sound and its strip.
4. **Routing only goes forward** (R-MIX-4) — to a strip on a LATER mixer, the master, or a port.
   `feedsForward` is the one copy of the rule; `targetsOf` publishes it as `strips[].targets`, and
   every picker or matrix offers exactly that list. Never add a cycle check; never allow a same-mixer
   route "just this once" — processing order is mixer order then strip order because of this.
5. **Lanes are organisation only** (R-LANE-1): a clip's `lane` decides where it is drawn, its `track`
   what it sounds through. Never derive one from the other.
6. **Patterns are shared** (R-CLIP-3): a duplicate is a second clip of the same pattern; notes are
   edited on the pattern. A piano roll or a step grid is a VIEW of a pattern (R-INST-3), never a
   second data model.
7. **The project names ports, never devices** (R-DEV-3). Device names live in the machine's settings;
   a missing device makes a port offline, never the project unopenable.
8. **The grammar is a table** — a row in `commandSpecs()` plus a case in `dispatch`. Unknown verbs,
   flags, addresses, device types and parameter names are REJECTED naming the nearest (R-SVC-3).
   **Every gesture is ONE command** (R-BROWSE-3): if a drop or a click needs two lines, the grammar is
   missing a flag (as `clip add --instrument` was).
9. **Every edit is all-or-nothing and announces only what landed** — `dispatch` copies the project,
   validates after, restores on failure, and emits an edit's events (`changed()` → `mPending`) only
   once the command has landed (D-1). A new command needs no rollback code of its own; it must never
   call `emit` directly for a project change.
10. **The API document is generated and committed** (R-API-1, rung 4). Change a command, an event, a
    model field, an address or a registry entry → regenerate `docs/api.json` + `docs/API.md` in the
    same commit. The **Addresses** table in `ApiDoc.cpp` is the one hand-kept part — the drift test
    cannot see an address missing from it, so you must.
11. **Render is pure** (R-RENDER-1): the same project, version and range give the same bytes — a
    fixed block, seeded noise, no clock, no thread-order dependence, byte-identical however time is
    chopped. Live playback IS the offline render on a thread (DR-PLAY-1), and the audio thread never
    allocates, locks or touches a file (R-PLAY-2, a counting `operator new` proves it).
12. **The core carries no codec, no device API, no OS path, no window** (R-SVC-4) — the host injects
    them through `SolarisService::Host`. A plugin's editor window, a second top-level window, a file
    dialog: all host.
13. **The UI dispatches TEXT** through `AppHooks::dispatch`, and draws ONLY from the model. A second
    front end must give the same answer, so anything it would have to re-derive (a strip's colour,
    which routes are legal, "fed by") is computed in the service and published in `AppModel`.
14. **Nothing on screen changes in one frame** (`arstro.design.rule` §1). Every value the model sets
    is drawn through an eased copy keyed by identity; a dragged control follows the pointer exactly;
    lists are keyed (`AnimatedRows`, per-id live state) so things grow in, shrink out and slide.
15. **Mix laws are Interstellar's** (R-MIX-11): balance pan with unity at centre, linear-amplitude
    fades. One project sounds the same in either app.
16. **One suite.** Where Interstellar already decided something Solaris also needs — versions (R-VER),
    keyframes and the key lane (Interstellar R-ANIM), `eval --explain`, the `#aauto` breakpoint
    syntax — Solaris follows it, or amends BOTH with a reason. Two models of the same idea in one
    suite is a defect with a delay fuse.

---

## 2. The loop (V-model, one task per commit)

1. **Requirement check — MANDATORY, before any code, for EVERY item of a request.** A table in your
   reply and in the commit: *request item → R- tag(s) → covered / new / changes / conflicts*.
   - **covered** — cite the R- line and build to it;
   - **new** — add the R- line to `REQUIREMENTS.md` (dated, "user request") and conflict-check it
     against its area;
   - **changes** — amend the line in place, `**AMENDED (<what forced it>, <date>)**`, with why;
   - **conflicts with another item of the same request** — say so, pick the reading that honours
     both, write the resolution into the requirement, tell the user.
   A large brief (a new area — VST3, automation, a piano roll) starts with a **discussion round** in
   `docs/discussion.md` and the requirement lines, committed BEFORE the first line of code, and is then
   cut into ledger tasks (one per commit). The user asked to discuss before building once already;
   assume they want to see the plan for anything that changes the app's shape.
2. **Sound first, in the DSP repo.** A task that needs new sound is first a DSP-library task (its own
   `REQ-`, README `## Math`, tests, commit, push) — then Solaris bumps the submodule pointer
   (`arstro.rule` §7's order).
3. **Lowest level that proves it:** the `.slp` → `solaris_model_tests`; the mix → `solaris_engine_tests`
   (measure samples: RMS in a window, a spectral peak, silence where muted, byte equality across chunk
   sizes); anything through the service → `solaris_service_tests` (L2, `dispatchText`, fake decoder and
   WAV writer); anything visible → `solaris_app_ui_tests` + `solaris_app_shots`.
4. **Watch it fail first** — a scratch mutant (route to master instead of the bus, ignore `pre`, emit
   before validating, a 0 ms tween instead of an eased one), see red, restore, say so in the commit.
5. **Docs in the same commit:** the DR entry with live anchors (re-check anchors you did not touch
   but whose file you edited — they drift), the module-map row, the R- status line, `PROGRESS.md`
   (tick, NEXT, decisions), regenerated API docs, `app/NOTES.md` rows for every new control, a
   DEFECTS entry for anything you found.
6. **Verify (§4), commit, `git pull --rebase --autostash`, re-run ctest, push** — the submodule first
   when it moved. Never stage other people's work in the tree (`apps/launcher/android-shell/*`,
   `Testing/`, `core/ImageProcessing/lib/LibRaw`) — `git add` named files only.

---

## 3. Recipes

### A new instrument or effect (the commonest task)
In `core/DigitalSignalProcessing` (`arstro.dsp.implement`): search `src/*/README.md` and compose
before writing math; derive and document; implement; unit + integration tests; a `REQ-` id; the
`DeviceRegistry` entry (type, kind, every parameter with unit / range / default / choices / taper) and
its `Device` adapter. The registry's tests build every entry, render finite samples at both ends of
every range and count allocations after warm-up — keep them green. Push the submodule, then in the
umbrella: bump the pointer, regenerate the API document, add the DR line, commit, push. The device
panel, the `.slp` keys and the addresses follow with no Solaris code — if they don't, that is the bug.

### A new command
Row in `commandSpecs()` (`core/service/Command.cpp`: verb, positional hint, min/max, flags as `name`
or `name=<hint>`, summary, R- tag) → case in `SolarisService::dispatch` → the group function
(`mixCommand`, `clipCommand`, …) → an `Event` row if it reports something new → an L2 test through
`dispatchText`, including its refusals → regenerate API. **`mutates()` lists the commands that do NOT
edit the song; anything not listed is treated as an edit** (it needs an open song, is snapshotted,
validated, and marks the song unsaved) — a new read-only or machine command must be added there.

### A new address (`set` / `get`)
Route it in `setAddress` / `getAddress` (`core/service/SolarisService.cpp`), refusing an unknown field
by name; add its row to the Addresses table in `ApiDoc.cpp` (hand-kept — law 10); an L2 test that sets,
gets, and is refused out of range. If it is live-adjustable while playing, add it to `liveUpdate`
(`ServiceTransport.cpp`) as a message, not an engine swap.

### A new model field the UI needs
`AppModel.h` is frozen: **add**, never rename or remove. Fill it in `refreshModel` (`ServiceModel.cpp`),
write it in `modelToJson`, document it in `appModelFields()` (the drift test fails otherwise),
regenerate API. If it is a computed answer (legal targets, a resolved colour, "fed by"), compute it in
the service once, so the widget only draws it.

### A routing or mixer feature
The rule is `feedsForward`; the list is `targetsOf`; both in `model/Project.cpp`. Adding a line to the
mixer and re-linking sources to it already exists as grammar — `strip add --kind bus --mixer <mx>`,
`route <ch> --to <bus>`, `clip move <ac> --strip <ch>` — a UI for it is a gesture over those lines.

### A widget
Read `app/NOTES.md`. Self-draw inside a `Segment`; take values from `Theme.h` tokens only; every model
value through an eased copy (see `MixerDock::Live`, `Timeline::ClipLive`); keyed lists through
Interstellar's `AnimatedRows`; pages through `FadePage`; menus through cosmo's `ContextMenu` (hosted by
`App`, reached via the `onMenu` / `onRename` hooks); sliders through cosmo's `SliderRow` (opt-in
`formatValue` for units). Publish the geometry a test aims at (`…Rect(id)`) and the LIVE eased values a
test reads (`…Live(id)`, `…Amount(id)`). Add: a UI test that clicks the published geometry, asserts the
line sent and the model that came back, and catches each transition MID-TWEEN; shots at both sizes,
at rest and mid-transition; NOTES rows.

### Planned areas — specify first, then build (each is a requirement round before code)

**VST3 (R-INST-4 reserved).** Two halves, never confused:
- *Our instruments as plugins* (wrapping `BasicSynth` / `DrumMachine`): a thin VST3 processor +
  controller around the DSP class, in `core/DigitalSignalProcessing/apps/vst3/` behind a CMake option,
  so `arstro_dsp` itself stays dependency-free. Parameter ids, names, ranges and normalisation come
  from the `DeviceRegistry` (law 2) — an id, once shipped, is frozen like a model field (hosts store
  it). Validate with the SDK's `validator`; the plugin's audio must equal the device's render through
  Solaris sample for sample at the same parameters.
- *Hosting other people's plugins* in Solaris: loading a `.vst3` module, scanning, processing, and the
  plugin's editor window are HOST work (`apps/solaris/host/`, law 12); the core sees a device-like
  interface and the model a device whose parameters are read from the plugin, not the registry. A
  plugin is untrusted code on the audio path — budget, crash and latency behaviour are requirements,
  not afterthoughts.
- *The SDK*: `git clone --recursive https://github.com/steinbergmedia/vst3sdk` to a fixed place the
  build finds through a CMake variable (never vendored into this repo without a decision); check its
  `LICENSE.txt` at install time and record the version in the requirement. Linux needs the X11/xcb
  development packages for VSTGUI and the editor (`IPlugView` on `kPlatformTypeX11EmbedWindowID`, with
  a host-provided `IRunLoop`). Install plugins to `~/.vst3/<Name>.vst3/Contents/x86_64-linux/`.

**Automation and parameter formulas (R-AUTO reserved).** Follow Interstellar's R-ANIM (curves with
linear / bezier / hold keys, a key lane under the timeline, a graph editor) and the suite's `#aauto`
breakpoint syntax (`docs/audio-format.md` §2.5) — law 16. Whatever binding model is chosen (a value, a
link to another address, a formula over automations and addresses), it must be: **evaluated by the
engine from compiled data** (the engine knows no project — the service compiles the formula and the
curves into the `MixGraph`); **deterministic** (law 11 — a formula reads beats, never the clock);
**sample-accurate at block granularity or better, stated as a requirement with a test that measures
it**; **acyclic** (a link loop is refused as a backward route is); **explainable** — give Solaris
Interstellar's `eval <address> --explain` before or with it, or nobody can debug a formula.

**Device and instrument windows, the piano roll.** A window is the host's (law 12): the App draws a
root per window and the host owns each top-level surface. A synth's window shows the generated panel
(or the plugin's own editor); "highlight the last change" is a model-published fact (which address
changed last), not widget state, so a script can read it too. The piano roll edits the PATTERN (law 6)
with `note add` / `note delete` (and whatever `note move` / `note set` the round specifies).

**The song bar's menus.** R-UI-3 already asks for cosmo's menu strip next to the wordmark; settings
and the full option set belong there, as cosmo's and Interstellar's do — reuse cosmo's `MenuStrip`,
opt-in only, never a fork.

---

## 4. Build, test, verify

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARSTRO_BUILD_SOLARIS=ON && cmake --build build -j
export SOLARIS_TEST_DIR=$SCRATCH/svc SOLARIS_UI_TEST_DIR=$SCRATCH/ui
ctest --test-dir build -R 'solaris'                # model · engine · service · api_current · app_shots · app_ui
ctest --test-dir build -R '^unit$|^integration$'   # the DSP library's suites, when the submodule moved
ctest --test-dir build -R cosmo                    # when a cosmo widget changed (SliderRow, ContextMenu, …)
# the committed API document — after ANY command, event, model field, address or registry change:
build/apps/solaris/cli/solaris-cc api --json > apps/solaris/docs/api.json
build/apps/solaris/cli/solaris-cc api --md   > apps/solaris/docs/API.md
```

Suites: `solaris_model` (fixed point, refusals, repairs) · `solaris_engine` (laws to 1e-7, notes on
their sample, byte-identical across chunk sizes) · `solaris_service` (L1 tables + L2: the real service,
fake decoder/WAV/devices/output; live = offline sample for sample; no allocation on the audio path) ·
`solaris_api_current` (drift) · `solaris_app_shots` (every named state at 1440×900 and 1024×640,
`--check` fails a blank frame) · `solaris_app_ui` (the real App over the real service, transitions
caught mid-tween). A binary prints `[PASS]` lines; a failed assert aborts after the last one.

### End to end from a shell — for any change a user would hear
```bash
cd $SCRATCH && ffmpeg -loglevel error -f lavfi -i "sine=frequency=330:duration=1" -ac 1 tone.wav
export SOLARIS_SETTINGS=$SCRATCH/settings.txt SOLARIS_RECENTS=$SCRATCH/recents   # never the user's own files
CC=$REPO/build/apps/solaris/cli/solaris-cc
$CC --watch project new song.slp --bpm 120 \
  : clip add --instrument drums --at 0 --length 8 \
  : note add pt_1 --pitch 36 --at 0 : note add pt_1 --pitch 38 --at 1 : note add pt_1 --pitch 42 --at 0.5 \
  : clip add --instrument synth --at 0 --length 8 : note add pt_2 --pitch 36 --at 0 --length 1.5 \
  : set dv_2.filter.cutoff=600 : clip add --src tone.wav --at 4 \
  : strip add --kind bus --name Verb : device add ch_5 --type reverb : send add ch_3 --to ch_5 --gain -10 \
  : project save : audit : matrix print : render --out mix.wav --stems ch_2,ch_3
```
Then **measure the files** (python `wave` + `struct`: RMS just after each beat vs just before, pitch by
zero crossings or an FFT peak, the tail, the peak against 0 dBFS) — a render that "succeeded" with
silence is a bug. `state print --json --stable` is the diffable state. Live: `transport play` then
`wait 1.5` then `state print` — the transport moved, the meters read.

### For anything visible
```bash
build/apps/solaris/app/solaris_app_shots --outdir $SCRATCH/shots [--only <name>]   # then READ the PNGs
build/apps/solaris/app/solaris_app_ui_tests
DISPLAY=:1 timeout 5 build/apps/solaris/solaris $SCRATCH/song.slp; echo $?          # 124 = ran 5 s, no crash
```

---

## 5. Gotchas found the hard way (keep this list growing)

Sound and engine:
- **`arstro::AudioConfig` is a process singleton** (R-NFR-7): set the rate before building devices.
- **Devices smooth every parameter write over a block**; the engine warms each device with a block of
  silence at build, so time zero starts at the project's values. A test that writes and reads the very
  next samples sees the ramp.
- **`Oscillator` owns an ADSR**: inside an instrument with its own amp envelope give the oscillators a
  gate envelope (attack 0, sustain 1) and release them when the amp envelope ends.
- **Per-channel state per channel.** A generator shared by both channels of a block makes the output
  depend on the block size — the engine's chunking test (128 vs 77) caught the synth's noise
  (DSP `a16e972`). Keep that test.
- **Pre-size everything the audio thread touches** in the constructor; `Engine::prepare` sizes the
  capture buffers. The counting `operator new` test is the only proof; a mutant made 880 allocations.
- **In a block-rendered test an event lands on a block boundary** — schedule test notes at multiples of
  the block (4864, not 4800, in 128-blocks).
- **DSP objects register in a static list**: build and destroy them on the service thread, never the
  audio thread.

Service and model:
- **A refused command must announce nothing**: project events go through `changed()` (held in
  `mPending`), never `emit` (D-1).
- **A reference into `model()` dies at the next dispatch** (`refreshModel` rebuilds it): copy a list
  before iterating it while sending commands.
- **The model lists strips in PROCESSING order** — Main (Buses) comes after Sources' strips; the newest
  strip is not `strips.back()`. Look strips up by id.
- **A relative `--src` names a file in the song's folder first** (`ServiceEdit` ClipAdd), else the
  working directory.
- **A path with a space must be quoted** in a command line — the grammar tokenizes on spaces.
- **`core/DigitalSignalProcessing` is a submodule on `feature/1.0.0`**: commit, test, push it BEFORE the
  umbrella records the pointer.
- **Tests: `#ifdef NDEBUG / #undef NDEBUG / #endif` before `<cassert>`** or Release passes everything.

UI:
- **The embedded Roboto has no "→" and no "●"** — draw the arrow (`arrowText` in `MixerDock.cpp`); "·",
  "…" and "×" are there.
- **Measure geometry in ONE weight.** A tab drawn Medium when current moved its neighbours, retargeted
  the highlight's tween and stalled it a frame — a mid-tween test caught it.
- **Build lazily in `advance`, not `bind`**: `bind` runs only when the model's revision moves, and a
  click that only SHOWS something moves nothing (the device panel opened empty).
- **Send a command AFTER iterating your own live state** — the model it brings back re-keys it.
- **A drag's release is direct manipulation**: `set` the live value AND the target to where the
  pointer let go before sending the line, or the next `advance` eases it back for a frame.
- **A culled row takes no input** — `reveal` a row before a test aims at it.
- **After a click changes a list, render a frame** before aiming at a control below it — geometry is
  measured in the paint.
- **After a zoom a clip may begin off-screen** — aim at `max(clip.x, kHeaderW)`.
- **Never delete with a shell variable path** (`rm -f $S/*`) — the harness refuses it; overwrite shots
  instead.

---

## 6. The UI (`apps/solaris/app/`)

Design law: `arstro.design.rule`; values: cosmo's. Solaris **aliases** cosmo's token namespaces and
forks ONE token, the accent — teal `#159387` (R-UI-2: cosmo blue's luminance, so white on it reads at
3.8:1) — installed first thing with `palette::setAccent`. Solo is amber and record red, never the
accent. Cosmo's widgets (`ConfirmDialog`, `ContextMenu`, `SliderRow`, `Icons`, `Theme`, fonts) are
compiled from `apps/cosmo/`; Interstellar's header helpers (`TextFit`, `EasedScroll`, `Glyphs`,
`AnimatedRows`, `FadePage`) are included in place — never copied (R-UI-4; their Artboard home is T1).
A change to a cosmo widget is OPT-IN only and cosmo's own tests must still pass.

The App sees the service only through `AppHooks` (`model`, `dispatch(text)`), binds when the model's
revision moves, and routes input through one recognizer whose sink gives a modal (menu, confirm,
settings) all input while open. `tests/Rig.h` drives the REAL `SolarisService` behind the hooks with
fake devices, folders and decoder and a fixed 16 ms clock, and publishes `world()`, `cx()`, `cy()`.

---

## 7. Definition of done

- The requirement check (§2.1) is in the reply and the commit; every R- line touched re-read against
  what was built.
- Any new sound is in `core/DigitalSignalProcessing`, documented and tested there, pushed first.
- The R- status is honest; the DR entry exists with live anchors.
- A test at the lowest sufficient level fails without the change (checked with a mutant) and passes.
- `docs/api.json` + `docs/API.md` regenerated if the grammar, events, model, addresses or registry
  changed — and the hand-kept Addresses table updated.
- `ctest` green; for anything audible the rendered samples were MEASURED; for anything visible the
  PNGs were LOOKED AT and the transition was caught mid-tween.
- Ledger, NOTES, DEFECTS updated; one commit; pulled, re-tested, pushed.
