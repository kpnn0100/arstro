# Interstellar — Progress Ledger

**This file is the single source of truth for what is done and what to do next.** It is committed
and travels between computers. A session reads the **NEXT** line below, does **one** task, updates
this file, and commits — in the same commit as the work.

- Rules: `.claude/skills/arstro.rule` (process) · `.claude/skills/arstro.design.rule` (pixels).
  **No `arstro.interstellar.*` skill exists yet** — see the decision of 2026-09-08 below.
- Intent: [`../REQUIREMENTS.md`](../REQUIREMENTS.md) · As-built: [`requirements.md`](requirements.md)
  · Defects: [`DEFECTS.md`](DEFECTS.md) · Plan + gates: [`plan.md`](plan.md) ·
  How to work: [`DEVELOPING.md`](DEVELOPING.md)
- Checkbox legend: `[ ]` not started · `[~]` in progress · `[x]` done + verified · `[!]` done but
  UNVERIFIED (say exactly what is unverified and why).

*Last updated: 2026-09-08 — the specification landed; no code exists.*

---

## NEXT

**► P0 — freeze the contracts. Nothing in `apps/interstellar/` is code yet, and the first code must
not start on an unfrozen schema.**

Take **one** of these. The first three are decisions (cheap, and everything downstream depends on
them); the last four are work in **other units** and must not be attempted inside
`apps/interstellar/`.

- [ ] **Decide: Nebula on real git with custom merge drivers, or a self-contained store.** Shared
      with Solaris, which flags it as its own first decision. Record the answer and the reasoning in
      [`design.md`](design.md), and add a row here.
- [ ] **Decide: `ThreadBudget` — promote `cosmo::ThreadBudget` to a shared header, or write a
      second one.** Promoting is the answer unless there is a reason; write the reason either way.
      (D-11 is why this is a decision and not a detail.)
- [ ] **Decide: FFmpeg vendored or system.** ImageProcessing vendors LibRaw; GTK comes from the
      system. Pick, and say which precedent applies and why.
- [ ] **Cosmo: a video source graded on a reference frame (R-COSMO-7).**
      **Owner: `arstro.cosmo.core.implement`, in Cosmo's own commit, against Cosmo's own
      requirements.** *P3 is blocked on this.* Interstellar's requirement states the contract; it
      does not authorise editing Cosmo's docs or code from here.
- [ ] **Cosmo: plumb `setMemoryCaps` through `CosmoService` (R-COSMO-8).** Already verified
      against the source, so this item is narrow: the constructor takes `ThreadBudget &` by
      reference and the decoder, worker-init, writer, event sink and settings are all already
      injectable. **The pixel caps are the only gap**, and reaching them today means using the
      transitional `session()` accessor whose remaining call sites Cosmo's ledger counts — which
      Interstellar must not add to.
- [ ] **`core/Gene`: promote `genesis::gene` out of `genesis_core`, extend it with dotted paths of
      arbitrary depth and a time scope, and make Genesis *alias* it.** Genesis's own tests must stay
      green. *P6 is blocked on this.*
- [ ] **`core/Nebula`: the minimal subset — the canonical serializer and the commit/branch store.**
      Shared with Solaris ([`../../solaris/docs/prerequisites.md`](../../solaris/docs/prerequisites.md)
      §B1); build it as its own library, not inside either app. *P1 is blocked on the serializer.*

**Gate for P0:** every box above ticked or carrying a written decision, and a row in this ledger for
each. Then P1 may start.

---

## Phases

Detail, dependencies and gates in [`plan.md`](plan.md). Status only, here.

| phase | what | status |
|---|---|---|
| **P0** | freeze the contracts; the three cross-unit prerequisites | `[ ]` |
| **P1** | project model, `.isp`, `Command`/`Event`/`AppModel`, `interstellar-cc`, the API document | `[ ]` |
| **P2** | `IFrameSource`/`IFrameWriter`, one deterministic frame, the first golden test, **the throughput measurement** | `[ ]` |
| **P3** | the Cosmo embed as colour authority — the architectural risk | `[ ]` |
| **P4** | timeline cut operations + the compositor | `[ ]` |
| **P5** | the generated parameter registry + automation clips and links | `[ ]` |
| **P6** | expression bindings | `[ ]` |
| **P7** | scrub, then playback: the frame cache, read-ahead, level selection | `[ ]` |
| **P8** | the master render | `[ ]` |
| **P9** | Nebula VCS, embed propagation, the audio bed | `[ ]` |
| **P10** | the Artboard UI, and the equivalence test | `[ ]` |

---

## Documents

| document | state |
|---|---|
| [`../README.md`](../README.md) | **written** 2026-09-08 — amended to the project-in-project / mixer direction; the superseded design kept in §9 |
| [`../REQUIREMENTS.md`](../REQUIREMENTS.md) | **written** 2026-09-08 — 19 areas, all `📋 SPECIFIED` |
| [`requirements.md`](requirements.md) | **written** (empty by construction — rung 0) |
| [`architecture.md`](architecture.md) | **written** — layers, seven seams, workspaces, threading, the full module map |
| [`architecture.puml`](architecture.puml) | **written** — every planned type has a box |
| [`detailed_design.md`](detailed_design.md) | **written** — proposed signatures for 14 core classes and 5 widgets |
| [`design.md`](design.md) | **written** — 5 major decisions with their rejected alternatives, 7 known risks |
| [`project-format.md`](project-format.md) | **written** — 9 node types, the address space, the command grammar, a worked example |
| [`binding.md`](binding.md) | **written** — the three producers, the frame pipeline, the failure modes |
| [`ui-brief.md`](ui-brief.md) | **written** — 4 workspaces, the new widgets, the state matrix |
| [`plan.md`](plan.md) | **written** — P0–P10 with gates, the dependency graph, the open decisions |
| [`DEVELOPING.md`](DEVELOPING.md) | **written** — the doc-authority table, 10 invariants, others' scars |
| [`DEFECTS.md`](DEFECTS.md) | **written** (empty; carries the inherited-scars table) |
| `api.json` / `API.md` | **not written — generated, and there is nothing to generate from yet.** P1 |

---

## Decisions log (newest first)

**2026-09-08 — the specification, and the six decisions inside it.**

1. **Interstellar HOSTS a `cosmo::CosmoService` as its colour authority.** Not a shared
   `EditParams`, not a read-only `as=look` embed, not a second process. This is what makes "project
   in project" structural and what makes R-RENDER-5's byte-identical still true by construction
   rather than by re-establishment. Rejected alternatives and their costs:
   [`design.md`](design.md) §1.
2. **Colour comes off the timeline.** A clip references a rack node and carries no colour field of
   any name — and such a field is a validation **error**, not an ignored key, because ignoring it
   would silently discard a user's edit. A shot that needs a different look duplicates the rack
   node (R-RACK-3). [`design.md`](design.md) §2.
3. **Animation is `autoclip` (a named, reusable shape) + `autolink` (its application).** Not
   keyframes. The split is what makes "multi param can use the same automation" *true* rather than
   *maintained*. `keyframe` and `grade-dissolve` are withdrawn as two mechanisms for one job, and
   what they could express is mapped onto the replacement in [`design.md`](design.md) §3.
4. **One generated address space, `<object>[.<filter>].<param>`, with the registry as the router.**
   Cosmo's own leaf names, so a preset, a `.cmp`, a `cosmo-cc set` line and an expression spell
   `exposure` identically. Generated, not hand-written, because Cosmo's `--help` already ran the
   experiment: generated names always right, hand-maintained hints missing for 8 of 30.
5. **Gene is promoted to `core/Gene` and aliased from `genesis_core`, not copied**, and extended
   with dotted paths and a time scope. The forked-palette scar is why; `arstrobench`'s aliasing of
   `cosmo/Theme.cpp` is the pattern.
6. **Two crops, deliberately named differently**: `s1.xform.crop` is the source's *framing* (Cosmo's,
   static, part of the look) and `clp_a.geom.crop` is the shot's *reframe* (Interstellar's,
   animatable, per clip). They also sit on opposite sides of the colour stage, which is the deeper
   reason. [`design.md`](design.md) §4.

**2026-09-08 — no `arstro.interstellar.*` skill was created, deliberately.** A skill that names a
file, a command or a flag which does not exist costs the next agent a whole investigation to discover
it is fiction — `arstro.rule` §8 records four such instructions in Cosmo's own skills. There is
nothing to build against yet, so work runs under `arstro.rule` + `arstro.design.rule` with
[`DEVELOPING.md`](DEVELOPING.md) §3 and §6 as the local law. **The right moment to write
`arstro.interstellar.core.implement` and `.design.implement` is at the end of P1**, when the service
seam, the CLI and the test harness exist and can actually be named.

**2026-09-08 — the as-built tier and the defect list were created empty, on purpose.** Both cost
nothing now and are expensive to retrofit; the suite already has one unit that skipped the second
tier and carries it as an open task.

---

## Verification notes

Nothing is verified, because nothing is built. The first three claims that will need evidence rather
than assertion, recorded now so they are not quietly skipped:

1. **P2's throughput measurement** — decode + colour-render ms per frame at 3840×2160 and at a
   1280 proxy edge, on the dev machine, pasted into the commit. The default proxy level depends on
   it, and [`design.md`](design.md) §8.2 lists it as an open risk precisely because nobody has
   measured it.
2. **P3's `.cmp` round-trip** — `rack import` → `set gr1.basic.exposure=0.2` → `rack save` → the
   value read back through `cosmo-cc`. This *is* the project-in-project requirement, and no amount
   of architecture argues for it.
3. **P3's budget peak** — measured concurrent threads across rack decode and colour render at
   `cpuPercent=25`. D-11's regression test, and it must exist before there are four consumers
   instead of two.
