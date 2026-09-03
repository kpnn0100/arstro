---
name: arstro.cosmo.design.debug
description: Use to investigate any suspected visual, layout, motion or interaction problem in the cosmo photo editor — a dead button, a panel that is empty or cut off, a list that cannot be scrolled, text overflowing, something that snaps instead of animating, a layout that breaks on resize, a modal that swallows clicks, wrong colours or spacing. Turns a vague human report ("that panel looks wrong") into a reproduction using the debug log, a UI tree dump and headless PNG renders, judges it against the requirements and the design rules, files it in the committed defect list, RECOMMENDS a fix without applying one, and commits. It never edits product code — landing a fix is `arstro.cosmo.design.implement`, which the user must invoke. Invoke for "the UI is broken", "this button does nothing", "the list is cut off", "it flickers", "why does this jump", "/arstro.cosmo.design.debug". For engine, session, load or export problems use arstro.cosmo.core.debug.
---

# arstro.cosmo.design.debug

> **Invoke `arstro.rule` first.** It carries the rules this skill used to restate: the
> core/front-end split, requirements-first and the conflict rule, the V-model doc-sync loop, the
> agent-drivable surface and its API document, and the ledger/defect/commit conventions. **Follow
> both; where they overlap, this file's checklist is the one to satisfy** — except on the
> architecture, requirement and agent-drivability laws, where `arstro.rule` wins.

The **reproduce → judge → file → commit** workflow for everything the user can see. It exists for one
specific situation: *the user is looking at the app, something is wrong, and they can only describe it.*
Your job is to turn that description into a rendered frame, a log excerpt and a UI tree dump — evidence
you can read — and then into a committed defect entry with a repeatable command.

**This skill does not fix anything.** It reproduces, judges, files and *recommends* — then stops.
`arstro.cosmo.design.implement` lands the fix, and the user invokes it; a recommendation is not
permission to act on itself. Filing is mandatory and so is saying what you would do, but never leave
the finding in chat alone.

The reason the split is absolute here in particular: a visual symptom is the **furthest** thing in this
codebase from its cause. D-23 was reported as "export says (missing image)" and lived in the `.cmp`
reader, two layers down and in the other skill's territory. Recommending lets that be re-routed;
fixing where the symptom is visible would have left the filmstrip and the breadcrumb still wrong.

You may render, dump, script and probe freely — that is the job. What you may not do is edit anything
under `apps/` that ships, except `DEFECTS.md`, `PROGRESS.md`, a requirement file, and a `--script`
fixture the reproduction needs.

Shares the defect list, ID sequence and entry format with `arstro.cosmo.core.debug` §4 — read that
section for the format; it is not repeated here.

---

## 0. Orient

Read `apps/cosmo/docs/DEFECTS.md` (already filed?), `apps/cosmo/docs/PROGRESS.md` (what changed
recently), `apps/cosmo/REQUIREMENTS.md` — especially the global design rules `R-G-1…R-G-3` — and the
`DR-` entry for the surface involved. Then `arstro.cosmo.design.implement` §1 for the token values and
§5 for the widget conventions the code is supposed to follow.

---

## 1. Get a usable report out of a vague one

Ask for only what you cannot get yourself, in one round, then go and look:
**which screen** · **what you did** (click, drag, wheel, resize, open a project) · **what you expected**
vs **what happened** · **window size**, roughly · **does it happen every time** · and **the log**:

```bash
tail -n 300 "${XDG_CONFIG_HOME:-$HOME/.config}/cosmo_v2/cosmo_v2.log"
```

If the log holds nothing useful, that is a finding in itself — cosmo's UI currently logs **nothing**, so
the first fix for a whole class of reports is adding the `ui`/`input` debug logging specified in
`arstro.cosmo.design.implement` §7. File that as a defect and note which report it would have answered.

---

## 2. Reproduce — three instruments, in this order

**a) The log.** With `--debug` / `COSMO_LOG_CATEGORIES=ui,input`, the answer is usually one line: which
widget consumed the gesture, whether the scroll clamped, whether a transition started and settled,
whether a value ever reached the session. **"click at 320,540 consumed by nobody" resolves most
dead-control reports on its own.**

**b) The UI tree dump.** `ui dump [--json] [--root editor|home|splash] [--visible] [--depth N]` —
a **Command over the control socket**, not a flag. Start the app with `cosmo --control /tmp/c.sock`,
then `cosmo-cc attach /tmp/c.sock` and send it; the reply is framed by `[evt] ui.begin` /
`[evt] ui.end`. It walks the Segment tree and writes each node's class, world rect,
visible/enabled/opacity, scroll offset and hover state — which is what distinguishes the three ways
a panel can "look empty": the rows were never created, they are positioned off-screen, or they are
drawn at opacity 0. (Note `opacity <= 1e-3` also disables hit-testing.)

It needs a **live window**: `cosmo-cc run` answers `ui dump` with *"no view attached"*, because
there is no view. There is no headless tree dump today — that gap is real and is the first thing to
file when it costs you an investigation.

**c) A headless render.** `cosmo_shots <outdir> --only <substring>` renders real frames to PNG with
no display; `--list` names the states, `--images A,B` supplies photos for the project shots,
`--check` fails on a uniform-colour PNG. Render **the broken state and a known-good neighbour**, and
sample **mid-transition as well as at rest** — a snap and a settle look identical in a single still.

**Two limits, and they decide how you reproduce.** `cosmo_shots` renders a *fixed, named* list of
states: there is **no `--size`** and **no `--script`**, so you cannot ask it for an arbitrary window
geometry or for the state your own click sequence produced. Where a shot exists that covers the
symptom, use it. Where one does not, either add the named shot to `renderShots.cpp` (the shots are
product code, not test scaffolding) or reproduce live with `cosmo <image>` and the control socket,
and say in the defect which of the two you used.

For the assembled app there is also `cosmo_ui_tests`, which renders through a `RecordingTarget` and
asserts geometry and animated values with no display — the right instrument when the question is
"do these two widgets overlap" rather than "what does it look like".

---

## 3. Symptom → first thing to check

Each of these has already been a real bug in this codebase or in its sibling app. Check the cheap
explanation before you go hunting.

| Symptom | Check first |
|---|---|
| A button or whole panel is dead | Something above it is capturing: a `hitTestSelf` that returns `true` unconditionally, or a modal left `mOpen`. Child order is reverse hit-test order; `MenuStrip` calls `raise()`. Also check `inputTransparent` and `enabled`. |
| Clicks land on the wrong control | Draw order vs hit order, or a stale `layout()` — geometry is recomputed every frame from animated values, so a widget mid-tween is not where the last still showed it. |
| A panel looks empty | `ui dump`: rows missing (never created / `visible=false`), off-screen (scroll offset or a parent that never laid out), or `opacity≈0` (a fade that never ran). Note `opacity <= 1e-3` also disables hit-testing. |
| Content is cut off and unreachable | R6: clipped without scrolling, or the viewport measured is not the box the rows are laid out in — the classic is a viewport a padding taller than the rows can occupy, which makes the last row unreachable at *every* offset. |
| The last row is missing at every scroll offset | Same as above. Assert *reachability*, not that the offset moved. |
| Something snaps / teleports | R1: a value written directly instead of through `animateTo`, or a setter with no `nowMs` that never recorded a pending target for `advance()` to start. Tab highlights and content swaps must travel or cross-fade. |
| An animation never finishes, or the UI is static | `advance()` did not chain to `Segment::advance(nowMs)`, or the host stopped ticking. Compare two mid-transition shots. |
| Text overflows or is clipped mid-glyph | R5: `estimateTextWidth` is only `len * px * 0.6` — under-measures wide strings. Prefer `t.measureText()`. |
| Text is off-centre | Baseline must be `centerY + sizePx * 0.35`; centring measured with the estimate rather than the real metric. |
| Hover does nothing / sticks | Two mechanisms only: `hoverAmount()` + `hoverBox()` for one Segment, `HoverFade` for multi-region. A sticky wash is a missing `if (!isHovered()) mHover.clear();` in `advance()`. |
| Layout breaks on resize | R4: a hardcoded size instead of `mW`/`mH`; or reading `LeftRail::kOpenWidth` instead of the **animated** `width.value()`. |
| Widget draws under its own children | `onPaint` runs before children — rings, scrims and dropdowns belong in `onOverlay`. |
| Rows pile up at (0,0) | Children created `visible=true` before their first data push. Default them hidden and positioned for the empty state. |
| A colour or radius looks off-family | A raw hex or an off-ladder px literal in a widget. Every value comes from `palette::`/`radius::`/`font::` and the 3.25px ladder. |
| Phone UI differs from desktop | Often deliberate: `touch/PhoneApp.cpp` keeps its own palette copy and coarser metrics. Check the touch brief before calling it a bug. |

---

## 4. Judge — defect, intended, or requirement gap

Read the requirement before forming an opinion, and quote it in the entry. The rule is the same as
`arstro.cosmo.core.debug` §3, with one addition specific to the UI:

- **Contradicts a requirement, or breaks `R-G-1…R-G-3` or `arstro.design.rule`'s R1–R6 → defect.**
  Those rules are requirements, not preferences: a snap violates R1, an unreachable row violates R6, an
  overflowing string violates R5, a hardcoded size that breaks at 1024px violates R4. You do not need a
  feature-specific requirement to call one of these a defect.
- **Matches a requirement → not a defect.** Say so, quote it. If the user wants it changed anyway, that
  is a requirement amendment (`**AMENDED (…)**` in place, with the reason) handed to
  `arstro.cosmo.design.implement` — not a bug fix.
- **No requirement covers it → requirement gap, and closing it is part of this task.** Decide what the
  surface *should* do from the neighbouring requirements, the Figma-derived values in `Theme.h`, and how
  cosmo already does the equivalent thing; **write the `R-` requirement**, conflict-check it, then
  re-judge against it. A new screen's requirement must state its states (idle / hover / pressed / active
  / disabled / empty / loading), how it reflows, and what happens when its content overflows — those are
  exactly the cases that go unspecified and then get reported as bugs. If the answer is a product
  decision, write your recommendation, mark it `*Open — needs confirmation.*`, and ask.

**Always a defect regardless of the documents:** a control the user cannot reach or operate at any window
size; content that cannot be scrolled to; a crash or freeze from an interaction; a UI thread blocked long
enough to drop frames; unreadable contrast on a normal surface.

---

## 5. File it

Use the entry format in `arstro.cosmo.core.debug` §4 verbatim, with `**Area:** design`, plus two
UI-specific additions:

- **Attach the evidence**: the shot filename(s), the `ui dump` excerpt (the relevant nodes, not the whole
  tree), and the log lines. Commit the fixture `--script` file; commit a baseline PNG only if it is small
  and genuinely the point of the entry.
- **Name the rule**: which of `R-G-1…R-G-3` / R1–R6 it breaks, or the requirement it contradicts, or the
  requirement you had to write.

Severity for UI defects: **S1** freeze, crash, or a control unreachable at a normal window size · **S2**
a user-visible surface that is wrong or unusable (content unreachable, values that never commit) ·
**S3** wrong behaviour with a workaround (a snap, a sticky hover) · **S4** cosmetic.

---

## 6. Report and recommend — and lead with the problem

The output is the deliverable. **Say what the problem was first**, in the user's own words, before any
machinery — they are looking at the app and something is wrong; that sentence is what they came for.
Then follow `arstro.cosmo.core.debug` §6's order, which this skill shares: proof, cause with
`file:line`, whether it is a regression (`git log -S`), the recommendation, and the fact that you have
not applied it.

Two things specific to a visual defect:

**Show the frame.** A rendered PNG is the evidence, so put it in the report, and say what to look at in
it — "the New card wraps to a second row at 1280 and the grid leaves a column of dead space" beats
"layout is wrong". If a shot at a second size or mid-transition is what makes the point, include both;
that is what `cosmo_shots` is for.

**Recommend at the cause's layer, and name the skill that owns it.** A visual symptom is the furthest
thing from its cause in this codebase: the export dialog printed "(missing image)" because the `.cmp`
reader never set a name (D-23), and the CPU-limit chips were drawn correctly for a budget that was
being applied twice (D-11). So say which layer the change belongs in, and if that is `core/`, say
plainly that `arstro.cosmo.core.implement` is the skill to invoke — not this pair. Getting the routing
right is most of the value this skill adds over guessing.

Also name **the assertion that should guard it** — a `cosmo_ui_tests` property (reachability,
non-overlap, fit, reflow), never a pixel — and what it must assert to fail on today's code. And if the
log or `ui dump` could not have shown you the answer, recommend that too: a defect the log could not
reveal is also an observability defect, worth its own entry.

---

## 7. Commit — the diagnosis, never a fix

The defect list is the deliverable. Include `DEFECTS.md`, any requirement written or amended, the
`--script` fixture, and the `PROGRESS.md` update — **and no product code**. A reviewer should be able to
read the commit and disagree with the recommendation before anything is built on it.

```
cosmo: file D-14 — the preset list's last row is unreachable at 1024x640 (R6)

Reproduced with cosmo_shots --only presets-crowded; ui dump over the socket
shows row 11 at y=612 with a viewport of 604, and the scroll offset already
clamped. The measured viewport is a pad taller than the rows occupy. Not fixed
yet; PROGRESS NEXT now points at it.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
```

Do not push unless asked.

---

## 8. Definition of done

- [ ] The symptom was reproduced with a **written-down command** producing a shot, a dump, or a log line
      — or filed as `Unreproduced` with everything tried.
- [ ] Evidence attached: the shot(s), the `ui dump` excerpt, log lines. Where layout is involved
      and no second-size shot exists, say so — `cosmo_shots` has no `--size`.
- [ ] Judged against a **quoted** requirement or a named rule (R-G-*, R1–R6).
- [ ] Any requirement gap **closed in `REQUIREMENTS.md`**, conflict-checked, including the surface's
      states and overflow behaviour.
- [ ] The defect entry is complete and uses the shared format and ID sequence.
- [ ] **No product code changed** — `git status` shows only the ledger, a requirement, and a fixture.
- [ ] The report **leads with the problem** in the user's terms, and includes the frame to look at.
- [ ] The recommendation names the **layer** the cause is in and the **skill** that owns it — which may
      be `arstro.cosmo.core.implement`, and often is.
- [ ] Scheduled in `PROGRESS.md`, and the user was told which skill lands it.
- [ ] Committed to `main`.
