---
name: arstro.interstellar.implement
description: Use to implement or resume ANY work in Interstellar, the video colour tool that can cut — the hosted Cosmo rack (grouping and colour), timelines-as-versions (override, pin, freeze, rebase), the .isp model, the cut operations, the lazy video volume and temporal effects, the render path and render queue, the InterstellarService grammar/events/model and its GENERATED API document, the interstellar-cc CLI, the audio subset, AND the UI (Home, Edit: Grade/Cut/Deliver; purple-pink cosmo). Runs the V-model with both requirement tiers in sync, verifies headlessly by driving the real service through text command lines and by rendering real frames, keeps the committed API document in step, records progress in the committed ledger, and commits. Invoke for "add a command", "add an address", "versions do X wrong", "add a temporal effect", "render to Y", "add a panel", "continue interstellar", "/arstro.interstellar.implement". NOT for diagnosing a reported bug without fixing it — that is arstro.interstellar.debug.
---

# arstro.interstellar.implement

> **Invoke `arstro.rule` first, then `arstro.design.rule` when the task touches a pixel.** They own
> the core/front-end split, requirements-first, the V-model doc sync, the ledger/defect/commit
> conventions and the design law. This file is Interstellar's map and checklist on top of them.

**Interstellar is a colour tool that can cut** (`REQUIREMENTS.md`, the second specification). Every
decision below follows from that sentence. The first specification made the timeline the app and
colour an embed; two builds shipped a cutting tool whose colour authority was a test fake, and were
withdrawn. **If a change makes colour less real, it is the wrong change.**

**You are the only skill that changes product code for a defect.** `arstro.interstellar.debug`
reproduces, files and recommends; read its `docs/DEFECTS.md` entry before re-deriving anything.

---

## 0. Orient — read these, in this order, before touching code

1. `apps/interstellar/docs/PROGRESS.md` — **NEXT** is the task unless the user named another.
2. `apps/interstellar/REQUIREMENTS.md` — intent (R-). `docs/requirements.md` — as built (DR-, with
   `file:line` anchors). A behaviour without a DR entry does not ship.
3. `apps/interstellar/docs/API.md` — **generated**; the command grammar, every event, every model
   field, the whole address space with units and owners. This is what the code accepts today.
4. `docs/project-format.md` (the `.isp`), `docs/architecture.md` (layers + module map),
   `docs/ui-brief.md` (screens), `../../docs/audio-format.md` (the suite audio schema).
5. `docs/DEFECTS.md` — D-1 (Cosmo alone cannot show a video source) and D-2 (Cosmo's save deletes an
   offline source; mitigated) shape what you may do with the rack.

### The code map

| directory | library | depends on | what lives there |
|---|---|---|---|
| `core/ImageProcessing/src/volume/` | `arstro_image` | — | `Volume`, `VolumeView`, `CachedVolume`, `TemporalDenoise`, `FrameBlend`, `freezeRemap`, `renderTemporal` |
| `apps/interstellar/model/` | `interstellar_model` | headers only | `Project` (.isp parse/serialize/validate), `Versions` (resolve, deltas, pin/freeze, rebase, diff), `arrange::` cut ops |
| `apps/interstellar/render/` | `interstellar_render` | `arstro_image` | `activeAt`, `compose`/`Layer`, `GradeEngine`, `FrameCache`, `hashParams` — **knows no project** |
| `apps/interstellar/core/` | `interstellar_core` | `cosmo_core`, model, render | `Rack` (hosted CosmoService), `Colour` (the one fold), `FrameSelector`, `ParamRegistry`, `service/` |
| `apps/interstellar/core/service/` | (in core) | | `Command` (the grammar TABLE), `Event`, `Json`, `AppModel` (frozen UI contract), `AppModelCodec`, `ApiDoc`, `InterstellarService` + `ServiceRender.cpp` |
| `apps/interstellar/host/` | `interstellar_host` | FFmpeg, GdkPixbuf | `VideoFrameDecoder` (Cosmo's decoder seam), `HostFrameSource` (stills via Cosmo's decoder), `FrameSourceFFmpeg`, `FrameWriterFFmpeg`, `PngWriter` |
| `apps/interstellar/cli/` | `interstellar-cc` | host | argv/stdout only — every verb is the service grammar |
| `apps/interstellar/app/` | `interstellar_app` | Artboard, cosmo widgets | the UI over `AppHooks` (see §6) |

---

## 1. The laws (each one cost a withdrawn build or a defect to learn)

1. **Colour is Cosmo's.** The rack is a real `cosmo::CosmoService` driven only by `cosmo::Command`s
   (`Rack`). Interstellar stores **no** `EditParams` as an authority. A colour write on a root
   timeline is Cosmo's `Select` + `Set`, saved into the `.cmp`. Never add a colour field anywhere
   else; a colour key on a `#clip` is a validation error by design (R-TL-2).
2. **A derived timeline stores DELTAS, never copies** (R-G-3). Arrangement edits go through
   `setField`/`setFields`/`dropNode`/`arrange::*`, which record `#tlset`/`#tldrop` on inherited
   nodes. Colour on a derived timeline is a `#tlgrade` **scalar delta on the colour source's own
   value** (nearest timeline wins per key). A curve/wheel/crop override on a version is refused,
   pointing at the base or `rack duplicate` — keep it refused unless the format grows a non-scalar
   delta (a spec change first).
3. **One fold.** `foldRender`/`foldEditTarget` (`core/Colour.cpp`) is `EditSession::effectiveParams`
   step for step. The live rack, a pin snapshot and a version's overrides all build a `ColourTree`
   and fold it here. `test_render_params_equal_cosmos_own_for_every_node` holds it equal to Cosmo.
4. **A pin is a byte copy of the `.cmp`** in `<stem>.pins/<commit>.cmp` + `<commit>.map`, read back
   through Cosmo's static `EditSession::readWorkspaceFile`. Read-only by construction.
5. **The grammar is a table.** A new command is a row in `commandSpecs()` (`core/service/Command.cpp`)
   plus a case in `InterstellarService::dispatch`. Never accept a flag you do not honour (R-SVC-3) —
   `eval --at` and `timeline diff --against` were removed for exactly that.
6. **The API document is generated and committed** (R-API-1, rung 4). Any change to a command, an
   event (`eventSpecs()`), a model field (`appModelFields()`) or an address (`paramDefs()`) must be
   followed by regenerating `docs/api.json` and `docs/API.md` (§4) in the same commit, or
   `interstellar_api_current` fails.
7. **The render path is pure** (R-RENDER-2): a frame is a function of (project, timeline, t, size).
   Caches key on everything that changes the pixels; the volume returns the same bytes for the same
   t whatever the order (R-VOL-7). A render NAMES its timeline (R-RENDER-1) and yields per frame.
8. **The core carries no codec and no OS path** (R-SCOPE-3). Decoders, writers and the recents path
   are injected through `InterstellarService::Host`.
9. **Don't let Cosmo save over an offline source** (D-2). Anything that makes Cosmo save — its `add`
   saves on finish — must go through `rackSaveBlocked` first.
10. **The UI dispatches TEXT** through `AppHooks::dispatch` → `dispatchText`. No privileged path.

---

## 2. The loop (V-model, one task per commit)

1. **Requirement first.** Find the R- tag; if there is none, add it to `REQUIREMENTS.md` (conflict
   check per `arstro.rule` §2) before code. A spec found wrong while building is **amended in place**
   with an `AMENDED (date)` note, as R-VER-2 and project-format §8 were.
2. **Lowest level that proves it** (R-TEST-2): model logic → `interstellar_model_tests`; grammar,
   codec, registry → `interstellar_service_tests`; anything that touches the rack, versions or frames
   → `interstellar_service_l2` (the REAL service, fake decoders only — never a fake rack).
3. **Write the test, watch it fail, then fix** (R-TEST-3). Before claiming a test guards something,
   break the code (a scratch mutant) and see it go red — e.g. "store the absolute value instead of
   the delta", "ignore pins". Restore and say in the commit that you did.
4. **Implement**, matching the surrounding code's density and idiom.
5. **Docs in the same commit:** the DR entry with live `file:line` anchors, the module map row in
   `architecture.md`, the R- status line, `PROGRESS.md` (NEXT, phases, a decision entry if you chose
   something), regenerated API docs, a DEFECTS entry for anything found.
6. **Verify (§4), commit, `git fetch` → rebase if behind → re-run ctest → push.** Never stage the
   unrelated work other people leave in the tree (`apps/launcher/android-shell/*`, `Testing/`,
   `core/ImageProcessing/lib/LibRaw`).

---

## 3. Recipes

### A new command
Row in `commandSpecs()` (verb, positional hint, min/max, flags as `name` or `name=<hint>`, summary,
R- tag) → case in `dispatch` → an `Event` row if it reports something new → L1 row-parses test is
automatic → an L2 test through `dispatchText` → regenerate API docs.

### A new address
A `ParamDef` in `core/ParamRegistry.cpp` (owner, pattern, filter for colour, kind, ENGINE unit and
range) → routing in `setAddress`/`getAddress` → the registry drift test checks every Cosmo key is
addressable → regenerate API docs. Colour keys are Cosmo's `EditParamsIO` keys, unchanged.

### A new model field the UI needs
`core/service/AppModel.h` is a frozen contract with the UI: **add**, never rename/remove. Fill it in
`refreshModel`, write it in `modelToJson`, document it in `appModelFields()` (the drift test fails
otherwise), regenerate API docs.

### A new temporal effect
A `TemporalOp` in `src/volume/TemporalOps.*` with a declared footprint and a test that fails against
a broken op (see `volumeTests.cpp`); an `#fx type=` in the model schema; wiring in
`InterstellarService::sourceFrame`; the fx key into the `FrameCache` source string (the cache must
key on it or a changed effect serves stale pixels).

---

## 4. Build, test, verify

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
INTERSTELLAR_TEST_DIR=$SCRATCH/ct ctest --test-dir build -R 'interstellar|volume'
# the committed API document — regenerate after ANY grammar/event/model/address change:
build/apps/interstellar/cli/interstellar-cc api --json > apps/interstellar/docs/api.json
build/apps/interstellar/cli/interstellar-cc api --md   > apps/interstellar/docs/API.md
```

Suites: `interstellar_model` · `interstellar_render` · `interstellar_core` (the rack, against a real
`.cmp`) · `interstellar_service` (tables) · `interstellar_service_l2` (the real service) ·
`interstellar_api_current` (drift) · `interstellar_still_equals_cosmo` (R-RENDER-5) · `volume`.
A test binary prints `[PASS]` lines; a failed assert aborts — stdout is unbuffered in each suite so
the line before the abort is visible.

### End to end from a shell (do this for any change a user would see)
```bash
cd $SCRATCH && export INTERSTELLAR_RECENTS=$SCRATCH/recents HOME=$SCRATCH
ffmpeg -loglevel error -f lavfi -i testsrc2=size=640x360:rate=24:duration=4 -pix_fmt yuv420p a.mp4
CC=$REPO/build/apps/interstellar/cli/interstellar-cc
$CC --watch project new mv.isp --res 640x360 : rack add a.mp4 : set a.basic.exposure=0.5 \
  : track add --kind video : clip add --track v0 --src a --in 0 --out 2 --at 0 --name shotA \
  : timeline new social30 --base main : timeline open social30 : set a.basic.exposure=0.8 \
  : eval a.basic.exposure --explain : project save \
  : render --timeline social30 --out out.mp4 : export-still --timeline main --out s.png --at 1
```
Then **look at the pixels** (Read the PNG) — a render that "succeeded" with a black frame is a bug.
`state print --json --stable` is the diffable state; `lint` lists offline media, dangling deltas,
refused nodes.

### R-RENDER-5 by hand
`apps/interstellar/tests/still_equals_cosmo.sh <interstellar-cc> <cosmo-cc> <ffmpeg> <dir>` — prints
both RGBA hashes; they must match.

---

## 5. Gotchas found the hard way (keep this list growing)

- **`cosmo::NodeModel::parent` is the parent's NODE ID**, not an index (cosmo's AppModel.h comment is
  wrong). Convert with `Rack::indexOf`. Walking it as an index hung the suite.
- **Cosmo's `import` REPLACES the workspace; `add` appends.** `Rack::addSources`/`beginAdd` use `add`.
- **Cosmo saves on `add`**, and its save **drops images without a slot** (D-66) — §1 law 9.
- **The rack load is asynchronous and decodes on a pool.** Pump against the WALL CLOCK with a 1 ms
  sleep (`Rack::pumpUntilLoaded`, `InterstellarService::pumpUntilIdle`); a spin over simulated time
  finishes before a worker opens the first file and reports an empty rack.
- **Binding `.cmp` entries to `#rackobj`:** entry i == live node i (Cosmo saves depth-first and loads
  in file order); `node=cn_<i>` is rewritten on every save by `syncRackObjNodes`, counting only groups
  and resident images (Cosmo skips the rest).
- **EditParams are floats.** A delta computed in double carries noise (`0.8 − 0.5 =
  0.30000000000000004`); round to `%.7g` before storing or every save is a spurious diff.
- **`canonicalNumber` is the model's** (always a decimal point: `0.0`, `24.0`). One spelling for the
  `.isp`, the log, the dump and the API document.
- **Timeline names are bind names** — `social30`, not `social-30s`.
- **Clip geometry is in FRAME units** in the `.isp`; the service converts to output pixels so a proxy
  and a full render place a clip identically.
- **A clip's source frame uses the SOURCE's rate**, not the project's (a 30p clip in a 24p project).
- **Tests: `#ifdef NDEBUG / #undef NDEBUG / #endif` before `<cassert>`** — a Release build otherwise
  disables every assertion and the suite "passes" (cosmo D-43).
- **`pkill -f <name>` matches your own shell** when the name is in the command line; use `pkill -x`.

---

## 6. The UI (`apps/interstellar/app/`)

Design law: `arstro.design.rule`; values: cosmo's (`arstro.cosmo.design.implement`). Interstellar
**aliases** cosmo's token namespaces and forks ONE token: the accent `#CF5AED`, installed at startup
with `arstro::cosmo_v2::palette::setAccent` (cosmo R-G-5) so every reused cosmo widget renders in
purple-pink. Cosmo's panels (`ParamPanel`, `MixerPanel`, `CurvePanel`, `GradePanel`, `XformPanel`,
`HistogramWidget`, `EditStackTabs`, …) are compiled from `apps/cosmo/widgets/`, never copied.

The app sees the service only through `AppHooks` (`model`, `dispatch(text)`, `renderFrame`). Its
harness and its own notes (shot list, the command line each control dispatches, contract requests)
are in `apps/interstellar/app/NOTES.md` — read it before changing a widget, and keep it current.
Verify by rendering shots at two sizes, mid-transition as well as at rest, and **looking at them**.

---

## 7. Definition of done

- The R- tag exists and its status line is honest; the DR entry exists with live anchors.
- A test at the lowest sufficient level fails without the change (checked) and passes with it.
- `docs/api.json` + `docs/API.md` regenerated if the grammar, events, model or addresses changed.
- `ctest` green; for anything visible, the PNG was looked at.
- Ledger updated; defects filed for anything found; one commit; fetched, re-tested, pushed.
