---
name: arstro.cosmo.design.debug
description: Use to investigate any suspected visual, layout, motion or interaction problem in the cosmo photo editor — a dead button, a panel that is empty or cut off, a list that cannot be scrolled, text overflowing, something that snaps instead of animating, a layout that breaks on resize, a modal that swallows clicks, wrong colours or spacing. Turns a vague human report ("that panel looks wrong") into a reproduction using the debug log, a UI tree dump and headless PNG renders, judges it against the requirements and the design rules, files it in the committed defect list, and commits. Invoke for "the UI is broken", "this button does nothing", "the list is cut off", "it flickers", "why does this jump", "/arstro.cosmo.design.debug". For engine, session, load or export problems use arstro.cosmo.core.debug.
---

# arstro.cosmo.design.debug

The **reproduce → judge → file → commit** workflow for everything the user can see. It exists for one
specific situation: *the user is looking at the app, something is wrong, and they can only describe it.*
Your job is to turn that description into a rendered frame, a log excerpt and a UI tree dump — evidence
you can read — and then into a committed defect entry with a repeatable command.

Fixing is `arstro.cosmo.design.implement`'s job. This skill may fix what it reproduced when the fix is
small and obvious, but **filing is mandatory and fixing is optional.** Never leave the finding in chat.

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

**b) The UI tree dump.** `--dump-ui <file>` (or a `dump-ui` step in a `--script` file) walks the Segment
tree and writes each node's class, world rect, visible/enabled/opacity, scroll offset and hover state.
This is what distinguishes the three ways a panel can "look empty": the rows were never created, they
are positioned off-screen, or they are drawn at opacity 0.

**c) A headless render.** `cosmo_shots <outdir> --only <state> --size 1440x900` renders a real frame to
PNG with no display. Render **the broken state and a known-good neighbour**, at **two window sizes**, and
sample **mid-transition as well as at rest** — a snap and a settle look identical in a single still.

Wrap it in a scripted run so the reproduction is a committed file rather than a sequence of clicks:

```
size 1440 900
open /path/photo.jpg
wait 900
click 1180 420          # the control the user described
dump-ui after-click.txt
shot after-click.png
expect log "ui: tab changed masks -> curve"
```

The same script must run headless and live (`arstro.cosmo.core.implement` §5.3). If the harness item you
need does not exist yet (`COSMO_APP_NOMAIN`, `cosmo_shots`, `--dump-ui`, `--script`, UI logging), file
the blind spot as a defect, reproduce with what exists — running `cosmo.exe <image>` and looking, plus
`cosmo_widget_tests` — and record which harness item would have made this a one-liner.

---

## 3. Symptom → first thing to check

Each of these has already been a real bug in this codebase or in its sibling app. Check the cheap
explanation before you go hunting.

| Symptom | Check first |
|---|---|
| A button or whole panel is dead | Something above it is capturing: a `hitTestSelf` that returns `true` unconditionally, or a modal left `mOpen`. Child order is reverse hit-test order; `MenuStrip` calls `raise()`. Also check `inputTransparent` and `enabled`. |
| Clicks land on the wrong control | Draw order vs hit order, or a stale `layout()` — geometry is recomputed every frame from animated values, so a widget mid-tween is not where the last still showed it. |
| A panel looks empty | `dump-ui`: rows missing (never created / `visible=false`), off-screen (scroll offset or a parent that never laid out), or `opacity≈0` (a fade that never ran). Note `opacity <= 1e-3` also disables hit-testing. |
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

- **Contradicts a requirement, or breaks `R-G-1…R-G-3` or `arstro.design.desktop`'s R1–R6 → defect.**
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

- **Attach the evidence**: the shot filename(s), the `dump-ui` excerpt (the relevant nodes, not the whole
  tree), and the log lines. Commit the fixture `--script` file; commit a baseline PNG only if it is small
  and genuinely the point of the entry.
- **Name the rule**: which of `R-G-1…R-G-3` / R1–R6 it breaks, or the requirement it contradicts, or the
  requirement you had to write.

Severity for UI defects: **S1** freeze, crash, or a control unreachable at a normal window size · **S2**
a user-visible surface that is wrong or unusable (content unreachable, values that never commit) ·
**S3** wrong behaviour with a workaround (a snap, a sticky hover) · **S4** cosmetic.

---

## 6. Fix, or hand off

If you fix it here, you run `arstro.cosmo.design.implement`'s rules in full: requirement first, then a
`cosmo_ui_tests` assertion that fails without the fix (reachability, non-overlap, fit, reflow — assert
the property, not a pixel), then the fix, then **render the shots again at two sizes and look at them**,
then sync `detailed_design.md` and the puml, then move the entry to Closed with the commit hash and the
test name.

Add whatever log line or `dump-ui` field would have made this obvious the first time. A defect the log
could not have shown you is also an observability defect — file that one too.

If you do not fix it: leave it Open, schedule it in `PROGRESS.md`, and tell the user plainly what is
broken and what the workaround is.

---

## 7. Commit — always

Even with no fix, the defect list is the deliverable. Include `DEFECTS.md`, any requirement written or
amended, the `--script` fixture, and the `PROGRESS.md` update.

```
cosmo: file D-14 — the preset list's last row is unreachable at 1024x640 (R6)

Reproduced with cosmo_shots --only presets-crowded --size 1024x640; dump-ui
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
- [ ] Evidence attached: shot(s) at 2+ sizes where layout is involved, `dump-ui` excerpt, log lines.
- [ ] Judged against a **quoted** requirement or a named rule (R-G-*, R1–R6).
- [ ] Any requirement gap **closed in `REQUIREMENTS.md`**, conflict-checked, including the surface's
      states and overflow behaviour.
- [ ] The defect entry is complete and uses the shared format and ID sequence.
- [ ] If fixed: a `cosmo_ui_tests` assertion that fails without the fix, shots re-rendered and looked at,
      docs synced, entry moved to Closed with the hash.
- [ ] If not fixed: scheduled in `PROGRESS.md`, and the user was told plainly.
- [ ] Committed to `main`.
