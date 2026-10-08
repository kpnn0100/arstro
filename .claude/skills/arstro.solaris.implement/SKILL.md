---
name: arstro.solaris.implement
description: Use to implement or resume ANY work in Solaris, the Arstro DAW — its instruments and effects (built IN the DSP library, never in the app), the Device registry, the .slp model, the mixer (every sample/instrument its own strip, mixers as ordered pages, forward-only routing, sends, the matrix, mix audit), lanes/clips/patterns, the engine and offline render, live playback on one or several audio devices (clock + drift-corrected followers, logical ports), the SolarisService grammar/events/model and its GENERATED API document, the solaris-cc CLI, versions, AND the UI (Home, Settings, Project view: browser + lanes + docked mixer; cosmo/interstellar family, teal accent). Runs the V-model with both requirement tiers in sync, verifies headlessly by driving the real service through text lines and by measuring rendered audio, records progress in the committed ledger, and commits. Invoke for "add an instrument", "add an effect", "the mixer does X", "add a command", "render to Y", "add a panel", "continue solaris", "/arstro.solaris.implement".
---

# arstro.solaris.implement

> **Invoke `arstro.rule` first, then `arstro.design.rule` when the task touches a pixel, and
> `arstro.dsp.implement` when it touches a sample of sound.** They own the core/front-end split,
> requirements-first, the V-model doc sync, the ledger/defect/commit conventions, the design law
> and the DSP law. This file is Solaris's map and checklist on top of them.

**Solaris is a DAW whose sound lives in the DSP library.** The user's own words, when they asked for
the first instruments: *"make sure all the core of those instruments and basic filters is in dsp."*
If a change puts an oscillator, a filter, an envelope, a compressor or a reverb into `apps/solaris/`,
it is the wrong change — however small, however convenient.

Where the shape of the app came from — the mixer, the devices, the versions — is
`apps/solaris/docs/discussion.md`. Read it once; it is why the rules below are not Cubase's or FL
Studio's.

---

## 0. Orient — read these, in this order, before touching code

1. `apps/solaris/docs/PROGRESS.md` — **NEXT** is the task unless the user named another.
2. `apps/solaris/REQUIREMENTS.md` — intent (R-). `docs/requirements.md` — as built (DR-, with
   `file:line` anchors). A behaviour without a DR entry does not ship.
3. `apps/solaris/docs/API.md` — **generated** (from V3 on): every command, event, model field and
   every device parameter with its unit and range. What the code accepts today.
4. `docs/project-format.md` (the `.slp`), `docs/architecture.md` (layers + module map),
   `../../docs/audio-format.md` (the suite audio schema Solaris shares with Interstellar).
5. `docs/DEFECTS.md`.
6. For sound: `core/DigitalSignalProcessing/docs/requirements.md` (REQ- ids) and the README of every
   module you will touch.

### The code map

| directory | library | depends on | what lives there |
|---|---|---|---|
| `core/DigitalSignalProcessing/src/` | `arstro_dsp` | — | **all sound**: oscillators, filters, envelopes, `BasicSynth`, `DrumMachine`, effects, `Device` + `DeviceRegistry`, the adaptive resampler |
| `apps/solaris/model/` | `solaris_model` | — | the `.slp`: parse, serialize, validate, repair |
| `apps/solaris/engine/` | `solaris_engine` | `arstro_dsp` | `MixGraph` → strips, racks, sends, ports, meters; **knows no project and no file** |
| `apps/solaris/core/` | `solaris_core` | model, engine | `SolarisService`, `Command` table, `Event`, `AppModel` + codec, `ApiDoc`, compile |
| `apps/solaris/host/` | `solaris_host` | FFmpeg, libpulse | decoder, WAV writer, audio devices, settings + recents files |
| `apps/solaris/cli/` | `solaris-cc` | host | argv/stdout only |
| `apps/solaris/app/` | `solaris_app` | Artboard, cosmo widgets | the UI over `AppHooks` |

Rows land phase by phase; `PROGRESS.md` says which exist (as of V3: model, engine, core, host's
file I/O, cli — not yet devices, settings, the UI). **Do not run a command this file names for a
directory that does not exist yet** — build it (it is probably NEXT), or say it is missing.

---

## 1. The laws

1. **The sound is the DSP library's** (R-DSP-1). A new instrument or effect is built in
   `core/DigitalSignalProcessing` under `arstro.dsp.implement` (compose existing primitives first,
   derive the math, `## Math` in its README, unit + integration tests, a `REQ-` id), then given a
   `DeviceRegistry` entry there. Solaris then has it with **no Solaris code**: the registry drives
   the `.slp` keys, the address space, the API document and the UI's knobs (R-DSP-2, R-UI-5).
2. **The registry is the only description of a parameter.** Name, unit, range, default, choices —
   once, in the DSP library. Never restate a range in Solaris; read it.
3. **A channel IS a strip** (R-MIX-1). No insert number between a sound and its strip. **Every
   sample file gets its own strip** (R-MIX-2); the same file again reuses it.
4. **Routing only goes forward** (R-MIX-4): to a strip on a LATER mixer, the master, or a port. The
   processing order is therefore mixer order then strip order — never add a cycle check, never
   allow a same-mixer route "just this once"; that is the property everything else leans on.
5. **Lanes are organisation only** (R-LANE-1). A clip's `lane` decides where it is drawn; its
   `track` decides what it sounds through. Never derive one from the other.
6. **Patterns are shared** (R-CLIP-3). A duplicate is a second clip of the same pattern. Notes are
   edited on the pattern, so every copy changes; "make unique" is the only way to diverge.
7. **The project names ports, never devices** (R-DEV-3). Device names live in the machine's
   settings. A missing device makes its port offline, never the project unopenable.
8. **The grammar is a table** — a command is a row in `commandSpecs()` plus a case in `dispatch`.
   Unknown verbs, flags, addresses, device types and parameter names are REJECTED naming the
   nearest candidates (R-SVC-3) — including a typo'd parameter inside a `set`.
9. **The API document is generated and committed** (R-API-1). Change a command, an event, a model
   field or a registry entry → regenerate `docs/api.json` + `docs/API.md` in the same commit.
10. **Render is pure** (R-RENDER-1): the same project, version and range give the same bytes. A fixed
    engine block, seeded noise, no clock, no thread-order dependence in a sum.
11. **The core carries no codec, no device API, no OS path** (R-SVC-4) — the host injects them.
12. **The UI dispatches TEXT** through `AppHooks::dispatch`. A drag-and-drop is a command line.
13. **Mix laws are Interstellar's** (R-MIX-11): balance pan with unity at centre, linear-amplitude
    fades. A project must sound the same opened in either app.

---

## 2. The loop (V-model, one task per commit)

1. **Requirement check — before any code, for EVERY item of a request**, as a table in your reply
   and the commit: *item → R- tag(s) → covered / new / changes / conflicts*. Add or amend the R-
   line (dated, "user request") in the same commit as the code (`arstro.rule` §2).
2. **Sound first, in the DSP repo.** If the task needs a sample of new sound, that is a DSP-library
   task done first, with its own `REQ-` id, README `## Math`, tests, and its own commit — pushed —
   before Solaris records the submodule pointer (`arstro.rule` §7's order).
3. **Lowest level that proves it:** model logic → `solaris_model_tests`; graph/engine →
   `solaris_engine_tests` (measure the samples: RMS at a known place, a spectral peak, silence where
   a strip is muted, byte equality of two renders); anything through the service → the L2 suite
   driving `dispatchText`.
4. **Watch the test fail first** — break the code with a scratch mutant (route to master instead of
   the bus, ignore `pre`, forget the pattern loop) and see it go red. Say in the commit you did.
5. **Docs in the same commit:** DR entry with live anchors, module-map row, R- status, `PROGRESS.md`
   (tick, NEXT, decisions), regenerated API docs, a DEFECTS entry for anything found.
6. **Verify, commit, pull --rebase, re-run ctest, push** (`arstro.rule` §7). Never stage other
   people's work in the tree (`apps/launcher/android-shell/*`, `Testing/`).

---

## 3. Recipes

### A new instrument or effect (the commonest task)
In `core/DigitalSignalProcessing` (`arstro.dsp.implement`):
1. Search `src/*/README.md` — compose from `Oscillator`, `ADSREnvelope`, `StateVariableFilter`,
   `Biquad`, `Noise`, `DecayEnvelope`, `Delay` before writing math.
2. Derive and document the math; implement; unit + integration tests; a `REQ-` id.
3. Add the registry entry (type name, kind, every parameter with unit/range/default/choices) and the
   `Device` adapter. The registry's own test checks every entry builds and renders finite samples.
4. Commit + push the submodule. Then in the umbrella: bump the pointer, regenerate the API
   document, add the DR line naming the new type, commit + push.

### A new command
Row in `commandSpecs()` (verb, positional hint, min/max, flags, summary, R- tag) → case in `dispatch`
→ an `Event` row if it reports something new → an L2 test through `dispatchText` → regenerate API.

### A new model field the UI needs
`AppModel.h` is a frozen contract with the UI: **add**, never rename/remove. Fill it, write it in the
codec, document it in `appModelFields()`, regenerate API.

---

## 4. Build, test, verify

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
ctest --test-dir build -R 'solaris|^unit$|^integration$'   # Solaris + the DSP library's suites
# the committed API document — regenerate after ANY command, event, model field or DSP registry change:
build/apps/solaris/cli/solaris-cc api --json > apps/solaris/docs/api.json
build/apps/solaris/cli/solaris-cc api --md   > apps/solaris/docs/API.md
# the DSP library alone (its own scripts):
bash core/DigitalSignalProcessing/unittest/buildSynthTests.sh  # must say 0 failed
python3 core/DigitalSignalProcessing/tests/run_integration.py
```

Suites: `solaris_model` (the .slp: fixed point, refusals, repairs) · `solaris_engine` (the mix on
rendered samples: laws to 1e-7, notes on their sample, byte-identical across chunk sizes) ·
`solaris_service` (L1 tables + L2: the real service through `dispatchText` with a fake decoder and
WAV writer) · `solaris_api_current` (the committed document = what the code prints). Set
`SOLARIS_TEST_DIR` to keep the service suite's songs out of /tmp.

### End to end from a shell (do this for any change a user would hear)
```bash
cd $SCRATCH && ffmpeg -loglevel error -f lavfi -i "sine=frequency=330:duration=1" -ac 1 tone.wav
CC=$REPO/build/apps/solaris/cli/solaris-cc
$CC --watch project new song.slp --bpm 120 \
  : strip add --kind instrument --instrument drums --name Drums \
  : clip add --strip ch_2 --at 0 --length 8 \
  : note add pt_1 --pitch 36 --at 0 : note add pt_1 --pitch 38 --at 1 : note add pt_1 --pitch 42 --at 0.5 \
  : strip add --kind instrument --instrument synth --name Bass : clip add --strip ch_3 --length 8 \
  : note add pt_2 --pitch 36 --at 0 --length 1.5 : set dv_2.filter.cutoff=600 \
  : clip add --src tone.wav --at 4 : strip add --kind bus --name Verb : device add ch_5 --type reverb \
  : send add ch_3 --to ch_5 --gain -10 : project save : audit : matrix print \
  : render --out mix.wav --stems ch_2,ch_3
```
Then **measure the files** (python `wave`: RMS just after each beat vs just before, the pitch by zero
crossings, the tail) — a render that "succeeded" with silence is a bug. `state print --json --stable`
is the diffable state; `audit` the mix report; `matrix print` every route.

## 5. Gotchas (keep this list growing)

- **`arstro::AudioConfig` is a process singleton** (R-NFR-7). Set the sample rate BEFORE building
  devices; every `SignalProcessor` re-derives its coefficients when it changes.
- **`SignalProcessor` smooths parameters per block**: a `setProperty` ramps over `bufferSize`
  samples. A test that sets a value and reads the very next sample sees the ramp's start.
- **`Oscillator` owns an ADSR.** Inside an instrument that has its own amplitude envelope, give the
  oscillators a gate envelope (attack 0, sustain 1) and release them only when the amp envelope has
  finished — or the release tail is cut.
- **DSP sources are `SignalProcessor`s registered in a static list.** Construct and destroy them on
  the service thread, never the audio thread.
- **Tests: `#ifdef NDEBUG / #undef NDEBUG / #endif` before `<cassert>`** or a Release build passes
  every assertion (cosmo D-43).
- **`core/DigitalSignalProcessing` is a submodule on `feature/1.0.0`.** Commit, pull, test and push
  it BEFORE the umbrella records its pointer.
- **A DSP object shared by the channels of a block depends on the block size.** Each block renders
  channel 0 and then channel 1; a generator shared by both hands each whichever stretch of its
  sequence the block leaves it. The engine's chunking test (128 vs 77) caught exactly this in the
  synth's noise (DSP `a16e972`) — keep per-channel state per channel, and keep that test.
- **Devices ramp their parameters over a block** after every write (`SignalProcessor` smoothing).
  The engine warms each device with a block of silence at build; a test that writes a parameter and
  reads the very next samples sees the ramp.
- **In a test that renders in blocks, an event is only seen at a block boundary** — schedule test
  notes at multiples of the block (a closed hat at 4800 in 128-blocks never fired; 4864 does).
- **Every edit is all-or-nothing** (`dispatch` copies the project and validates after). A new command
  needs no rollback code of its own — but it must not emit events before it has succeeded.

---

## 6. The UI (`apps/solaris/app/`, from U1)

Design law: `arstro.design.rule`; values: cosmo's. Like Interstellar, Solaris **aliases** cosmo's
token namespaces and forks ONE token: the accent, teal `#5AEDDE` (R-UI-2), installed at startup with
`arstro::cosmo_v2::palette::setAccent`. Cosmo's widgets are compiled from `apps/cosmo/widgets/`,
never copied; Interstellar's `app/` is the model for how (its `Theme.h`, `AppHooks.h`, `NOTES.md`).
Device panels are generated from the registry (R-UI-5). Verify by rendering shots at two sizes,
mid-transition as well as at rest, and **looking at them**.

---

## 7. Definition of done

- The requirement check (§2.1) is in the reply and the commit.
- Any new sound is in `core/DigitalSignalProcessing`, documented and tested there, pushed first.
- The R- tag's status is honest; the DR entry exists with live anchors.
- A test at the lowest sufficient level fails without the change (checked) and passes with it.
- `docs/api.json` + `docs/API.md` regenerated if the grammar, events, model or registry changed.
- `ctest` green; for anything audible, the rendered samples were measured.
- Ledger updated; defects filed; one commit; pulled, re-tested, pushed.
