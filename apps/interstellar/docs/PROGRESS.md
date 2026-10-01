# Interstellar — Progress Ledger

**The only authority on what is done and what is next.** Committed, so work resumes on any machine.
A session reads **NEXT**, does one task, updates this file, and commits — in the same commit.

- Rules: `.claude/skills/arstro.rule` · `.claude/skills/arstro.design.rule`
- Skill: `.claude/skills/arstro.interstellar.implement` *(to be rewritten for this specification —
  the one on disk describes the withdrawn first build)*
- Intent: [`../REQUIREMENTS.md`](../REQUIREMENTS.md) · As-built: [`requirements.md`](requirements.md)
  · Format: [`project-format.md`](project-format.md) · Audio: [`../../../docs/audio-format.md`](../../../docs/audio-format.md)
  · Architecture: [`architecture.md`](architecture.md) · UI: [`ui-brief.md`](ui-brief.md)
  · Defects: [`DEFECTS.md`](DEFECTS.md)
- Legend: `[ ]` not started · `[~]` in progress · `[x]` done + verified · `[!]` done but UNVERIFIED

*Last updated: 2026-10-01 — the volume engine is in `arstro_image`; render, model and UI streams in flight.*

---

## NEXT

**► Integration of four parallel streams, then the service.** The second build is being made by four
agents in disjoint directories (each builds standalone, none touches shared CMake, none commits) and
one integrator who moves, wires, tests and commits each stream:

| stream | directory | state |
|---|---|---|
| volume + temporal ops | `core/ImageProcessing/src/volume/` | `[x]` integrated — DR-VOL-1..3, DR-FX-2 |
| render path (active set, composite, grade, cache) | `apps/interstellar/render/` | `[x]` integrated — DR-TL-4, DR-FX-3, DR-RENDER-2 |
| `.isp` model + versions | `apps/interstellar/model/` | `[~]` agent running |
| UI (Home, Edit: Grade/Cut/Deliver) | `apps/interstellar/app/` | `[~]` agent running |
| service, grammar, codec, API doc, CLI | `apps/interstellar/core/service/`, `cli/` | `[~]` integrator — tables + drift test written, service next |

- [ ] `InterstellarService`: Rack + model + render + volume behind `dispatch(Command)` / `pump` /
      `model()` / `renderFrame`; `set` routed by owner; pins as content-addressed `.cmp` snapshots.
- [ ] `interstellar-cc` on the grammar; `docs/api.json` + `docs/API.md` committed and drift-tested.
- [ ] GTK host wiring the UI's `AppHooks` to the service.
- [ ] R-RENDER-5: an Interstellar still == the same frame from Cosmo, byte for byte.
- [ ] Rewrite `arstro.interstellar.implement` / `.debug` for this specification.

---

## Phases

| phase | status |
|---|---|
| **P0** specification | `[x]` requirements, format, audio format, architecture, UI brief. First build removed |
| **P1** the rack — colour reaching a real `.cmp` | `[x]` gate passed from a shell on a video source; read back by a second CosmoService. D-1 filed (Cosmo alone cannot show a video source) |
| **P2** `.isp` + versions | `[ ]` |
| **P3** service + API document | `[ ]` |
| **P4** arrange + composite + render | `[~]` render path built and integrated (DR-TL-4, DR-FX-3, DR-RENDER-2); arrangement with the model stream |
| **P5** Volume + temporal effects | `[~]` engine built and integrated into `arstro_image` (DR-VOL-1..3, DR-FX-2); `#fx` wiring with the service |
| **P6** audio | `[ ]` |
| **P7** UI | `[ ]` |

---

## Decisions log (newest first)

**2026-10-01 — the render path is its own library and knows no project.** `interstellar_render`
takes plain structs (`ClipSpan`, `Layer`, `Raster`, `EditParams`) and nothing else, so a render is a
pure function of what it was handed (R-RENDER-2) by construction: it cannot quietly read a project
field it was never given. The model and the service translate into it. A dissolve needs a flag the
first contract lacked (`dissolveWithPrevious`) — weights summing to 1 are not enough if the layers
are stacked rather than mixed against one base.

**2026-10-01 — the volume landed as RGBA8, not linear float.** The architecture sketch had float
pixels; the build uses straight RGBA8 frames because the temporal ops run on decoded source frames
(R-VOL-5: ungraded) and the grade — which needs float — runs after them, per frame, in EditEngine.
A float window would cost 4× the residency to hold values that are about to be quantised anyway.
The view is a fixed 33-slot array of frame pointers, so it is a stack value that never allocates.

**2026-10-01 — the second specification, and what it changes.**

1. **The centre moved.** The first specification made the timeline the app and colour an embed, and
   two builds shipped a cutting tool whose colour authority was a test fake. Interstellar is a
   **colour tool that can cut**. `R-RACK` is now the first requirement and P1 the first phase, and
   the phase gate is a `.cmp` read back through `cosmo-cc`.
2. **A timeline IS a version** (R-VER), replacing project-level Nebula branching. Branching the
   whole project put the version boundary around the *colour authority*, which made "rebase to get
   the base's latest colour" impossible to express. One project, one rack, many cuts of it.
3. **One inheritance model, found by writing the format.** R-VER-2 first had colour inherit live and
   arrangement inherit by a replayed delta. Writing §3.2's resolution rules showed those to be two
   names for one mechanism; `cut=frozen` is now the same lever as `colour=pin`. Amended in place.
4. **The audio format is a suite document, not an app document** — two apps read it, so it is not
   one app's property. Interstellar implements a subset and **parses, preserves and refuses** the
   rest: a Solaris project opens here without loss, and a part it cannot render makes the render
   refuse rather than quietly go missing.
5. **The accent is derived, not picked.** `#CF5AED` is cosmo's blue rotated to hue 288° at the same
   lightness, so `ring`, `primaryAlpha` and the selection wash need no re-tuning. It is the **only**
   forked token.
6. **The FFmpeg source and writer were kept** across the removal. They are concept-independent
   plumbing, tested, and the new design needs them identically; deleting and retyping them would
   have been waste dressed as a clean slate.

---

## Verification notes

Nothing is built. The first build's evidence does not transfer: it was taken against a `FakeRack`
and a per-frame engine, and both are gone.

**The three claims that will need evidence rather than assertion, recorded now so they are not
quietly skipped:**

1. **P1's gate** — a value set in Interstellar, read back out of the `.cmp` by `cosmo-cc`. No
   architecture argument substitutes for that output.
2. **P5's measurement** — ms per frame for a radius-2 temporal denoise at 1080p and at 4K, which is
   what decides whether the volume's window size is affordable. The first build never measured the
   graded path at all, because identity params short-circuited the engine.
3. **R-API-1's drift test** — the document regenerated and diffed in CI. Generating it is the half
   that is easy; committing and diffing it is the half that makes it trustworthy.
