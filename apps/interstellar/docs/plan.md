# Interstellar — Implementation & Test Plan

**Guiding principle: the basic workflow first, features later — and every phase ends in a gate that
can be run from a shell with no display.** A phase is not done because its code exists; it is done
because its gate command was run and its output pasted into the commit (`arstro.rule` §4).

Status: nothing started. Conformance rung **0**. The ledger — the only authority on status — is
[`PROGRESS.md`](PROGRESS.md).

---

## Strategy

Four ideas shape the order below.

1. **Offline before interactive.** A deterministic single-frame render (P2) comes before playback
   (P7), because a frame is comparable byte-for-byte and a playback is not. Every later phase is
   verified by rendering frames, so this is the load-bearing one.
2. **The colour authority early.** The Cosmo embed is P3 — before the timeline. Colour is the thing
   the product is *for*, and it is also the largest architectural risk, so it is proven while there
   is little to unpick.
3. **The address space before the things that use it.** The registry (P5) precedes automation and
   bindings, because both are consumers of it, and it is generated, so it cannot be retrofitted
   without regenerating everything that read it.
4. **The UI last, and only last.** CLI-first is the suite's law (`arstro.rule` §1, vision §7). The
   UI is P10 and it starts from the brief, from `arstro.design.rule` and from Cosmo's widget
   catalogue — not from a blank `Theme.h`.

---

## P0 — Freeze the contracts *(no product code)*

Nothing here is Interstellar code; it is the decisions and the two dependencies that would otherwise
be discovered halfway through.

### P0.1 Decide, and write down
- [ ] **Nebula: real git with custom drivers, or a self-contained store?** This changes a lot
      downstream and Solaris flags it as the first decision too. Decide once, for both apps.
- [ ] **Where `Gene` lives** (`core/Gene`), and the migration commit that makes `genesis_core` alias
      it rather than own it.
- [ ] **`ThreadBudget`: promote `cosmo::ThreadBudget` to a shared header, or write a second one?**
      Promoting is the answer unless there is a reason; write the reason either way.
- [ ] **The frame-time representation** — the exact rule that makes `(seconds, fps)` round-trip to
      one integer frame (R-CUT-5), including how a 23.976 project is written.
- [ ] **FFmpeg: system pkg-config, or vendored?** ImageProcessing vendors LibRaw; GTK comes from the
      system. Pick, and say which precedent applies.
- [ ] The `.isp` schema and the command grammar are **already** frozen, in
      [`project-format.md`](project-format.md). Changes to them from here on are amendments with a
      reason.

### P0.2 Cross-app prerequisites — work in OTHER units
These are not Interstellar commits and must not be attempted inside `apps/interstellar`. Each is
listed with the skill that owns it.

- [ ] **R-COSMO-7: a video source in Cosmo.** An `IImageDecoder` (or a sibling seam) that decodes
      frame *t* of a video file; a `frame=` selector on an image node; a `rack frame` equivalent
      command; the `.cmp` writer carrying the selector. **Owner: `arstro.cosmo.core.implement`**, in
      Cosmo's own commit, against Cosmo's own requirements. *P3 is blocked on this.*
- [ ] **R-COSMO-8: plumb the pixel-memory caps through `CosmoService`.** Already verified against
      the source: the constructor is `explicit CosmoService(ThreadBudget &budget)` — the budget
      arrives by reference — and `setDecoderFactory` / `setWorkerInit` / `setImageWriter` /
      `subscribe` / `applySettings` cover the rest. **Only the caps are missing**: they live on
      `RenderService::setMemoryCaps` and are reachable only through the transitional `session()`
      accessor Cosmo's ledger is counting down. One method on `CosmoService`. **Owner:
      `arstro.cosmo.core.implement`.**
- [ ] **`Gene` promotion to `core/Gene`**, with dotted paths and a time scope, `genesis_core`
      aliasing it, and Genesis's own tests still green. **Owner: a Genesis/shared-library commit,
      not this app.** *P6 is blocked on this.*
- [ ] **Minimal Nebula** — the serializer and the commit/branch store at least. Shared with Solaris;
      see [`../../solaris/docs/prerequisites.md`](../../solaris/docs/prerequisites.md) §B1. *P1 is blocked
      on the serializer; P9 on the rest.*

**Gate:** every box above is either ticked or has a written decision recorded in
[`design.md`](design.md) and a row in [`PROGRESS.md`](PROGRESS.md). A phase that starts on an
unfrozen contract is how a schema gets three versions.

---

## P1 — Project model, `.isp`, and the CLI skeleton  *(foundation)*

**Build:** `Project`, `Timeline` (data only, no operations yet), `Command` + its generated parser
and formatter, `Event` + `formatEvent`, `AppModel` + `AppModelCodec`, `InterstellarService`
(dispatch/pump/model, most kinds still rejected), `interstellar-cc` with `new` / `open` / `save` /
`state print` / `--watch` / `run --script` / `api`.

**Prove:**
- `project new` → `state print --stable` → `project save` → reopen → **the two stable dumps are
  identical**.
- **The round-trip fixed-point test**: parse → serialize → parse → serialize, byte-identical, over a
  fixture set that includes unknown keys, comments, quoted strings and every node type. This is the
  cheapest test in the project and it catches most format regressions for free.
- `api --json` prints, and a test regenerates `docs/api.json` and **diffs** it (R-SVC-10). Doing
  this in P1, when the tables are small, is the only time it is cheap.
- An unknown command and an unknown field are both **rejected naming themselves**, and the rejection
  is in `lastError` *and* in the event stream (R-SVC-6).

**Gate:** `ctest` green; the round-trip and API-drift tests registered; a `run --script` file
committed under `tests/scripts/` that creates a project and dumps it.

---

## P2 — The frame seam and one deterministic frame

**Build:** `IFrameSource` + the FFmpeg implementation; `IFrameWriter` + **the PNG-sequence writer
first**; a still-image `IFrameSource` (one frame) so stills need no second path; the minimal render
path — one source, no rack, no automation — plus `render` and `export-still`.

**Prove:**
- A committed 2-second test clip renders to a PNG sequence, and **the same frame renders identically
  twice** (R-NFR-1).
- The first **golden-frame test**: a tiny project, a committed PNG, a stated and *justified*
  tolerance (R-TEST-5).
- **A measurement, not a demo** (design.md §8.2): decode + colour-render ms per frame at 3840×2160
  and at a 1280 proxy edge, on the dev machine, pasted into the commit. This number decides whether
  P7's default proxy level is 1280 or lower, and guessing it would mean discovering it during
  playback work.

**Gate:** `render --format png-seq` produces byte-identical output on two runs; the golden test
fails when a pipeline stage is perturbed (check it, do not assume it).

---

## P3 — The Cosmo embed: colour authority  *(the architectural risk)*

**Blocked on P0.2's first two rows.**

**Build:** `RackEmbed` — construct a `cosmo::CosmoService` with Interstellar's budget, decoder and
caps; `rack import` / `rack new` / `rack add` / `rack group` / `rack duplicate` / `rack frame` /
`rack select`; `set` routed to Cosmo for a colour address; Cosmo's events re-published as `rack.*`;
`AppModel::rack` carrying `cosmo::AppModel` by value; the pin, and the refusal it implies.

**Prove:**
- `rack import japan18.cmp` → `set gr1.basic.exposure=0.2` → `rack save` → **open the `.cmp` in
  `cosmo-cc` and see the value there** (R-COSMO-3). This one command sequence *is* the
  project-in-project requirement, and it is the single most important test in the plan.
- **R-RENDER-5's identity**: `export-still --at t` from Interstellar and the same source exported
  from `cosmo-cc` are **byte-identical**. If the numbers differ, the two apps are not sharing the
  authority they claim to.
- A colour command against a **pinned** rack is refused, naming the commit (R-COSMO-5).
- **One budget**: with `cpuPercent=25` on an N-core box, the measured concurrent thread peak across
  rack decode + colour render is ≤ the budget. This is D-11's regression test and it must exist
  before there are three consumers instead of two.

**Gate:** the round-trip through `cosmo-cc` above, run and pasted; the byte-identical still; the
budget peak measured, not argued.

---

## P4 — The timeline: cut and composite

**Build:** the operations in R-CUT-3 as commands; `Timeline::activeClipsAt` with an index rebuilt on
mutation; `Composite` — geometry, `fit`, blend modes, opacity, dissolve and dip.

**Prove:**
- A committed script cuts a five-clip sequence (add, trim, split, move, roll, slip, ripple delete)
  and the stable dump matches a committed expectation — one test for all of R-CUT-3.
- Golden frames for: a two-layer composite, each blend mode, each `fit` mode, a dissolve **sampled
  mid-transition**, and a clip whose source is offline.
- A colour field on a clip is **rejected** (R-CUT-2) — asserted, because "it is rejected" is a claim
  about a code path nobody exercises otherwise.

**Gate:** the cut script; the blend-mode golden set; the rejection test.

---

## P5 — The parameter registry and automation

**Build:** `ParamRegistry`, **generated** — every Cosmo address from `EditParamsIO`'s keys, every
Interstellar address from the timeline and composite structs; the router in `set`; `AutoClip`,
`AutoLink`, `Automation`, the lane projection, the boundary lint; `eval <address> --at <t>`;
`auto *` commands.

**Prove:**
- **The resolved-value table test**: a committed `.isp` with one shape and several links; assert a
  dozen addresses at a dozen times against a committed table. It catches a mapping error, a mode
  error, a fade error and an order regression in one file.
- **The requirement itself, asserted**: one shape linked to `gr1.basic.exposure` (0 → 0.8 EV) and to
  `clp_a.geom.scale` (1.0 → 1.08); move one breakpoint; **both** resolved values change (R-AUTO-2).
- An overlapping link on one address is **refused** (R-AUTO-4).
- `api --json` now contains the whole address space, and the drift test still passes.
- A boundary step with `fadeIn=0` produces a **lint finding** — and the finding's text is asserted,
  because a warning nobody has seen printed is a warning that does not work.

**Gate:** the resolved-value table; the shared-shape test; the lint assertion; the API diff.

---

## P6 — Bindings

**Blocked on P0.2's `Gene` promotion.**

**Build:** `BindingGraph` — compile, fold, collect dependencies, refuse cycles, topological order;
the `Scope` with the time built-ins; `bind` / `bind list` / `bind delete`; `eval --explain`; the
broken-binding fallback.

**Prove:**
- **The user's own example, end to end**:
  `bind gr1.opacity = clamp(gr1.basic.exposure / 2 + 0.5, 0, 1)`; set the exposure; assert the
  resolved opacity at three values, including both clamps.
- A cycle is **refused at set time**, with both ends named (R-BIND-4).
- A binding reading an automation clip (`1 + ac_push.value * 0.08`) resolves correctly at three
  times — the test that proves automation runs before bindings (R-EVAL-2).
- A binding on an address that also has a link is **refused** (R-BIND-5).
- A deleted object leaves the binding `broken`, the parameter at its **static** value, **one** event
  emitted per render — asserted by counting the events over a 100-frame render (R-BIND-8).
- A rename rewrites every referencing expression atomically and re-validates (R-PARAM-2).

**Gate:** all six above; the golden-frame set re-rendered unchanged (bindings must not perturb a
project that has none).

---

## P7 — Scrub, then playback

**Build:** `FrameCache` with the parameter-hash key; range invalidation; `Playback` — the clock,
byte-bounded read-ahead, level selection, frame dropping; `playhead` / `play` / `pause` / `stop`.

**Prove:**
- **Scrub correctness first**: seek to 50 random times; every frame matches the offline render of
  that time (within the golden tolerance). This is the test that makes the cache trustworthy.
- **Staleness cannot happen**: seek, change a parameter that feeds the frame, seek back — the frame
  is re-rendered, not served from the cache. Asserted via the cache's own hit/miss counters, which
  is why they are in the model.
- Playback under an artificial load **drops frames and lowers the level**, and both are visible in
  the model (R-NFR-4).
- Read-ahead stays under its byte cap, measured.

**Gate:** the 50-seek scrub test; the staleness test; the drop-and-report behaviour observed under
load.

---

## P8 — The master render

**Build:** `RenderJob` — one frame per `step()`, progress, cancel, resume at a frame boundary,
`--lint`; the FFmpeg writer (ProRes, H.264/H.265).

**Prove:**
- A full render of the test project to ProRes and to a PNG sequence; the PNG sequence matches the
  per-frame golden renders exactly.
- Cancel mid-render leaves a valid partial file or none — never a half-written frame.
- Resume from frame N produces the same bytes as an uninterrupted render from N.
- Progress events arrive **during** the render, not in a burst at the end (the `step()`-per-frame
  requirement, asserted by timestamping the events).

**Gate:** the PNG/ProRes agreement; the cancel and resume behaviour.

---

## P9 — Nebula: branches, merge, embed propagation, audio

**Build:** the rest of Nebula (semantic merge, auto-rebase, embed resolution, the resource pool);
`branch` / `rebase --status` / `merge --policy`; rack-embed propagation and conflict reporting;
`as=audio` and `AudioOut`.

**Prove:**
- The rack's branch advances outside Interstellar → a clean rebase updates the cut's colour; a
  conflicting one **flags the exact nodes and fields and loses no cuts** (R-COSMO-6).
- Two cut branches merge; a field-scoped conflict is reported as one field, not one file (R-VCS-3).
- Two projects merge with `concatenate` and with `overlay`, and the result is a normal project that
  can itself be branched.
- A pinned embed is read-only through **every** front end and every command path (R-VCS-6).
- The measured A/V offset is published (R-PLAY-6) — published rather than claimed.

**Gate:** the clean and the conflicting propagation, both run; the merge policies; the pin honoured.

---

## P10 — The Artboard UI

**Build:** in this order — the shell + workspace switcher + `Monitor` + `Transport` first (so
everything after it has somewhere to appear), then Grade (mostly Cosmo's widgets), then Cut, then
Mix, then Deliver. `interstellar_shots` and `interstellar_ui_tests` from the **first** UI commit,
with `--size`, `--script` and a headless tree dump (ui-brief §8).

**Prove:** `arstro.design.rule` §10's definition of done, per surface. The four that are not
negotiable:
- **Nothing changes in one frame** — established by comparing two frames half a tween apart, for the
  workspace cross-fade, the timeline zoom, the lane expansion and the column fold. Never by reading
  the code.
- **Two window sizes**, one small.
- **Every state shot, empty and loading included.**
- **The equivalence test** (R-SVC-9): one committed script through the CLI and through a live window
  over the socket; assert the event streams; diff the stable dumps.

**Gate:** the equivalence test passing; shots for every state at two sizes committed; `ui dump`
answering correctly on a live tree.

---

## Dependency graph

```
P0 contracts ─┬─► P1 model + CLI ──► P2 frame + golden ──┬─► P4 cut + composite ──┐
              │                                          │                        │
              ├─► (cosmo: video source) ──► P3 rack ─────┤                        ├─► P8 master
              │                                          │                        │   render
              ├─► (Gene promotion) ─────────────┐        └─► P5 registry + auto ──┤
              │                                 └─────────────► P6 bindings ──────┤
              └─► (minimal Nebula) ──► P1                                         │
                                                          P7 scrub + playback ◄───┘
                                                                   │
                                       P9 VCS + embeds + audio ◄───┤
                                                                   │
                                                     P10 UI ◄──────┘
```

**Genuinely parallel:** P4 (cut) and P3 (rack) after P2 — the cut needs a frame, not a grade. P5's
registry generation is independent of P4's operations. P9 touches almost nothing P1–P8 built, which
is why it is late rather than woven through.

**Genuinely serial, and why:** P2 before everything (it is how every later phase is verified); P5
before P6 (bindings resolve addresses); P3 before the golden set grows (otherwise every golden frame
is re-baked when colour arrives).

---

## Cross-cutting test strategy

| level | what it looks like here |
|---|---|
| **L0 unit** | plain asserts in the local style — no framework introduced |
| **L1 numeric** | `AutoClip::value` at known times; `Composite` blend maths on synthetic pixels; the frame-time round-trip |
| **L2 the real service, headless** | **the usual level**: construct the service with a fake `IFrameSource`, dispatch a command script, pump to a fixed tick, assert the stable dump or the collected event lines |
| **L3 the CLI** | the actual `interstellar-cc` invocation, run, output pasted into the commit |
| **L4 randomized / concurrent** | the thread budget under randomized load with a stall deadline; the cache under concurrent invalidation; a sanitizer build |
| **L5 live** | the GUI on real footage — the only thing that catches a host↔service wiring mistake |

**A fake `IFrameSource` is the most valuable test asset in the project.** It answers `frameAt(n)`
with a synthetic frame that *encodes n* (a counter pattern), so a test can assert that the right
source frame reached the right output frame — through speed changes, transitions, caching and
read-ahead — with no codec, no media file and no tolerance. Build it in P2 and everything after it
gets cheaper.

**And the golden tolerance is chosen, once, with a reason.** A golden test whose tolerance nobody
picked is a test that either fails on every platform or passes through real regressions.

---

## Open decisions to settle before the phase that needs them

| decision | needed by | note |
|---|---|---|
| Nebula on git vs a self-contained store | P1 (partly), P9 (fully) | shared with Solaris; decide once |
| FFmpeg vendored vs system | P2 | LibRaw is vendored, GTK is system — pick the precedent |
| Colour-space handling (rec709 only, or a transform chain) | P2 | v1 may be "rec709 in, rec709 out" if that is *written down* |
| One shared `RenderService` or two | P3 | design.md §8.6; decide with the P2 measurement in hand |
| Default proxy edge | P7 | the P2 measurement decides it |
| Audio: which mixer/device layer | P9 | Solaris will have opinions; do not invent a second one |
| Whether `.eff` (effective-value reads) ever lands | post-v1 | reserved in R-BIND-6, deliberately absent |
