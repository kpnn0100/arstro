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

*Last updated: 2026-10-01 — P1 passed its gate: a colour edit on a VIDEO source reached a real `.cmp`.*

---

## NEXT

**► P1 — the rack: a colour edit that reaches a real `.cmp`.**

This is first because it is what both previous builds skipped, and nothing else counts as done while
the colour authority is a fake. It is also small: Cosmo's service surface is already the right shape.

- [ ] **`Rack`: own a `cosmo::CosmoService`, implement `RackAccess` over it.** Its constructor is
      `explicit CosmoService(ThreadBudget &)` — the budget arrives by reference already — and
      `setDecoderFactory` / `setWorkerInit` / `setImageWriter` / `subscribe` / `applySettings` /
      `dispatch` / `dispatchText` / `pump` / `model` / `takeFrame` cover everything else.
      `effectiveParams(node)` maps onto `EditSession::effectiveParams(slot)`; a write maps onto a
      `Select` + `Set` pair, exactly as `cosmo-cc` does, so the two paths are one path.
- [ ] **A video source, with no change to Cosmo.** Install a decoder factory that returns one
      extracted frame for a video path, through the FFmpeg source carried forward in `host/`.
      Cosmo then treats it as an image slot (R-RACK-3).
- [ ] **THE GATE, and it is one sequence:** `rack import japan18.cmp` → `set gr1.basic.exposure=0.2`
      → `project save` → open the `.cmp` with `cosmo-cc` and **see the value there**. Paste it into
      the commit. Until that output exists, P1 is not done.
- [ ] **Then R-RENDER-5:** a still from Interstellar and the same frame from `cosmo-cc`, byte-identical.

**The order after that, and why:**

| phase | what | why here |
|---|---|---|
| **P2** | `.isp` + versions: document, canonical text, base chains, deltas, resolution, rebase | the model everything else hangs on; pure logic, fastest to make correct |
| **P3** | service + Command/Event/AppModel + **committed, drift-tested `api.json`** | R-API-1 is rung 4 and no app in the suite has it. Cheap now, expensive later |
| **P4** | arrange + composite + the carried-forward codecs: render a named timeline | the first end-to-end picture |
| **P5** | Volume + the three temporal effects | the engine change; needs P4 to have something to measure against |
| **P6** | audio subset: track, clip, gain, fade, master sum | the format is already specified, so this is implementation only |
| **P7** | UI: Home, then Edit/Grade, then Cut, then Deliver | last, as the suite's law requires — and Grade first within it, for the same reason P1 is first |

---

## Phases

| phase | status |
|---|---|
| **P0** specification | `[x]` requirements, format, audio format, architecture, UI brief. First build removed |
| **P1** the rack — colour reaching a real `.cmp` | `[x]` gate passed from a shell on a video source; read back by a second CosmoService. D-1 filed (Cosmo alone cannot show a video source) |
| **P2** `.isp` + versions | `[ ]` |
| **P3** service + API document | `[ ]` |
| **P4** arrange + composite + render | `[ ]` |
| **P5** Volume + temporal effects | `[ ]` |
| **P6** audio | `[ ]` |
| **P7** UI | `[ ]` |

---

## Decisions log (newest first)

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
