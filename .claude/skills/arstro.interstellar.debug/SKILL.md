---
name: arstro.interstellar.debug
description: Use to investigate any suspected problem in the Interstellar video editor — a parameter that resolves to the wrong number, an automation clip that does nothing or jumps, a binding that will not take, a cut operation that loses frames, an .isp that loses data on save, a colour edit that does not reach the Cosmo rack, a frame that composites wrong, a UI that snaps or is cut off or has a dead control. Reproduces it with no GUI in the loop — a script of Commands through interstellar-cc, `eval <address> --at <t>`, a diffable stable model dump, the event stream, a headless PNG shot or UI-tree dump — judges against the written requirements whether it is a defect or intended behaviour, files it in the committed defect list with an exact reproduction, RECOMMENDS a fix without applying one, and commits. It never edits product code — landing a fix is arstro.interstellar.implement, which the user must invoke. Invoke for "interstellar is broken", "the exposure ramp is wrong", "why is this parameter that value", "the clip jumped", "is this a bug?", "the lane is empty", "/arstro.interstellar.debug".
---

# arstro.interstellar.debug

> **Invoke `arstro.rule` first**, and `arstro.design.rule` when the report is about something
> visible. Then `arstro.interstellar.implement` §0 for the code map and §5 for the shell surface.

The **reproduce → judge → file → commit** workflow for Interstellar. Its output is never just an
answer in chat: it is a **committed defect entry with a command anyone on any machine can re-run**,
plus — when the requirements turned out to be silent — a requirement that now says what the right
behaviour is.

**This skill does not fix anything.** It reproduces, judges, files and *recommends* — and then
stops. Landing a fix is `arstro.interstellar.implement`'s job and the user invokes it; a
recommendation is not permission to act on itself.

Why the split is absolute rather than "fix it if it is small and obvious": **the size of a fix is
not knowable until the diagnosis is finished, and by then you are invested in it.** In this app the
temptation is sharp, because a wrong number usually looks like one line — and the two defects found
while Interstellar was first built were a flag parser that mis-read any value containing `=` (three
lines) and a deck whose two views did not share a time origin, which had hidden every automation
link in the first 190 px (a shared constant, four call sites, and a requirement about deriving one
fact once). Only one of those was a one-liner.

You may WRITE and RUN throwaway things to prove a point — a probe in the scratchpad, a fixture, a
temporary assertion you delete. What you may not do is change product code. The boundary is exactly
that: **nothing under `apps/` or `core/` that ships**, except `DEFECTS.md`, `PROGRESS.md`,
`REQUIREMENTS.md` and a committed fixture the reproduction needs.

---

## 0. Orient

Read, in order: `apps/interstellar/docs/DEFECTS.md` (it may already be filed — add evidence to the
existing entry rather than opening a duplicate) · `docs/PROGRESS.md` (what changed recently is where
bugs live) · `REQUIREMENTS.md` and `docs/requirements.md` for the area · and **`docs/binding.md` §6
whatever the area is**, because in this app most "wrong number" reports are a question about the
frame pipeline's order.

---

## 1. The loop

1. **Capture the report** in the user's own words, before you theorise.
2. **Reproduce with no GUI** (§2). No reproduction → file it as `Unreproduced` with everything you
   tried, which is itself useful to the next session.
3. **Minimize** — fewest commands, smallest project, one address.
4. **Judge** against the written requirement (§3): defect, requirement gap, doc drift, or
   works-as-specified.
5. **File it** (§4), with a pasteable reproduction and a *measurement* rather than an argument.
6. **Recommend a fix**, and say what NOT to change.
7. **Commit** the defect entry and any requirement it forced (§5).

---

## 2. Reproduce with no GUI — the instruments, in the order to reach for them

### 2.1 `eval <address> --at <t>` — reach for this FIRST

Most reports in this app are "this parameter is the wrong number", and this answers exactly that.
`--explain` prints each input and the intermediate, so a value a user cannot account for becomes a
value with a derivation:

```bash
cat > /tmp/repro.txt <<'EOF'
project open /tmp/cut.isp
eval gr1.basic.exposure --at 3.0 --explain
eval gr1.opacity --at 3.0 --explain
EOF
./build/apps/interstellar/cli/interstellar-cc run --script /tmp/repro.txt --watch
```

**The four questions it settles, which are the four ways a number goes wrong here:**

| the answer says | the producer is | so look at |
|---|---|---|
| `static: X` and `no automation link covers t` | the static value | the `.isp`, or a `set` that was refused |
| `static: X` / `automation active -> Y` | a link | its `at`/`dur`/`from`/`to`/`mode`/`scope`/fades |
| `binding: <expr>` with its deps listed | an expression | a dep that resolved to something unexpected |
| nothing — it refuses | the address | the typo, named, with the nearest candidates |

### 2.2 The stable model dump, diffed

```bash
interstellar-cc run --script before.txt --stable > /tmp/a.txt
interstellar-cc run --script after.txt  --stable > /tmp/b.txt
diff /tmp/a.txt /tmp/b.txt
```
One field moving is a diagnosis. Use `--resolved` to include every resolved address. **The stable
form excludes what is a property of WHEN the dump was taken** — `revision`, `frameSeq`, `frameMs`;
if your evidence needs one of those, use the unstable dump and say so, and do not "fix" a test by
adding a field to the exclusion list (that is falsifying the test).

### 2.3 The event stream

`--watch` prints `formatEvent()`, and that **is** the log. `command.rejected` with its reason is
the first thing to look for in any "it did nothing" report — and a rejection also lands in
`lastError`, so it is inspectable after the fact.

### 2.4 The `.isp` itself

It is plain text, canonically formatted. `project open` then `project save` to a new path and
`diff` against the original: **a difference is a round-trip defect** (R-FMT-4), which is a data-loss
class of bug and therefore S1.

### 2.5 For anything visible

```bash
./build/apps/interstellar/interstellar_shots --outdir /tmp/s --size 1024x640 --script /tmp/state.txt --tree
```
`--tree` prints the UI tree with each node's world rect and opacity — reach for it **before** the
PNG, because a shot tells you *that* something is wrong and the tree tells you *which node*. An
empty panel is three different bugs (rows missing, rows off-screen, rows transparent) and the tree
distinguishes them.

**For "it snaps": a still frame cannot show you.** Pump one frame at a time and read the live eased
value (`App::workspaceFade`, `TimelineView::pixelsPerSecond`, `LaneStack::expansion`,
`Transport::playFade`) — they are public for exactly this. **Never settle then look**: 400 ms is
long enough for a 260 ms fade to finish, so the assertion reads 1.0 and a snap passes.

### 2.6 What is NOT available, so you do not spend an investigation finding out

- **No control socket, so no live window to drive** (R-SVC-8 is unbuilt). A GUI-only report is
  reproduced through `interstellar_shots --script` or `interstellar_ui_tests`, not by attaching.
- **No FFmpeg writer**: `render` produces PPM only. A codec report is not reproducible yet.
- **No real rack unless one is wired**: with no `RackAccess`, every colour address resolves to
  nothing and every clip reads `offline`. **That is expected, not a bug** — check whether the report
  is really "no rack attached" before filing anything.
- **No frame source**, so the compositor draws a flat placeholder. A report about *what the picture
  looks like* is about geometry and blending, not about decoded pixels.

---

## 3. Judge it — against the requirement, not against taste

Quote the requirement you judged against, verbatim enough to be checkable. The judgements that
recur in this app, and what each turns into:

- **"the parameter would not take"** → check for a binding on it. A bound parameter is deliberately
  not writable (R-EVAL-1): the write would be overwritten on the next frame, so it is refused.
  **Works-as-specified**, and the fix is a better message if the message was not clear.
- **"the automation jumped"** → run `lint`. A link whose first mapped value differs from the static
  value with `fadeIn = 0` **steps by design** (R-AUTO-5) and the lint says by how much. That is
  works-as-specified *with* a reporting requirement — if the lint did not fire, THAT is the defect.
- **"only one of my two parameters moved"** → two links share a shape only if they name the same
  `#autoclip`. Check the ids in the `.isp`, not the names in the UI.
- **"my second link was refused"** → overlapping links on one address are refused (R-AUTO-4).
  Works-as-specified.
- **"the colour edit did not reach Cosmo"** → is the rack pinned? A pin is read-only through every
  path (R-COSMO-5). Otherwise it is a real S2.
- **"the value is right and the picture is wrong"** → the numbers are steps 1–4 and the picture is
  steps 5–8. `eval` proves which half, and that halves the search.
- **"a wrong number that is *consistently* wrong"** → suspect the pipeline ORDER before suspecting
  the arithmetic (`binding.md` §6). An expression reading a stale automated value is the signature.

**A `Judgement` of requirement gap MUST produce a new requirement** — that is the rule that stops a
debugging session from ending in an opinion.

---

## 4. File it — `apps/interstellar/docs/DEFECTS.md`

One entry per defect, newest first in `## Open`. Ids are `D-<n>`, sequential across the unit,
**never reused** — check the highest existing id in both sections first. Keep this exact shape:

```markdown
### D-3 — The exposure ramp is 0.4 EV when the shape says it should be 0.8
- **Area:** core / evaluator        <!-- core | ui | format | rack | cli -->
- **Status:** Open                  <!-- Open | Confirmed | Fixed | Not-a-defect | Unreproduced | Deferred -->
- **Severity:** S2                  <!-- S1 data loss/crash · S2 wrong output · S3 wrong UX · S4 cosmetic -->
- **Found:** 2026-09-11, reported by the user / by inspection during D-2
- **Reproduce:**
  ```bash
  cat > /tmp/repro.txt <<'EOF'
  project new /tmp/d3.isp --fps 24 --res 640x480
  track add --kind video --name v0
  clip add --track v0 --src rack:cn_1 --in 0 --out 4 --at 0 --name clp_a
  auto new ac_push --dur 2.0 --points 0=0,1=1 --ease linear
  auto link ac_push -> clp_a.opacity --at 1.0 --dur 2.0 --from 0 --to 0.8
  eval clp_a.opacity --at 2.0 --explain
  EOF
  ./build/apps/interstellar/cli/interstellar-cc run --script /tmp/repro.txt --watch
  ```
- **Expected:** 0.4 at the shape's midpoint (R-AUTO-2: the mapped value is
  `from + (to - from) * shapeValue`).
- **Actual:** 0.2 — and `--explain` shows the shape sampled at 0.25 rather than 0.5.
- **Evidence:** the `--explain` trace, pasted. `AutoClip::value(1.0)` returns 0.25 because the
  shape's second breakpoint is at t=2.0, not t=1.0 — `auto new --points` scales point times by
  `dur`.
- **Judgement:** requirement gap — `--points` scaling is undocumented. (or: defect, contradicts …)
- **Cause:** `InterstellarService.cpp:<line>` multiplies each point's `t` by `a.dur`.
- **Requirement:** R-AUTO-1 (existing) / R-AUTO-1a (written while filing this)
- **Recommended fix:** document the normalisation in `project-format.md` §8 and reject
  `--points` values outside 0..1. **Do NOT** change the scaling: the `.isp` stores local seconds
  and a shape's breakpoints must round-trip.
- **Guard:** a resolved-value table entry at t=2.0 — checked to fail on the pre-fix code.
```

Rules: **the Reproduce block must be something someone else can paste.** Prefer a **measurement** in
Evidence over an argument — an `--explain` trace, a stable-dump diff, a `--tree` line, an assertion
that failed. Say **which front end** you reproduced with, since "only in the GUI" and "in both" are
different bugs. If you could not reproduce, still file it as `Unreproduced` with everything you
tried. When a defect is resolved it moves **whole** to `## Closed` with its commit hash and its
guarding test; **never delete an entry.**

---

## 5. Severity, and what to do about it

- **S1** — crash, hang, or **data loss**, which in this app means a failed `.isp` round trip, a lost
  binding, or a rename that left a stale reference. File it, then tell the user plainly and
  immediately: what breaks, what to avoid, and that `arstro.interstellar.implement` should be run
  next. Do not fix it.
- **S2** — wrong output the user would ship: a wrong resolved value, a wrong composite, a colour
  edit that did not reach the rack. File it and make it the ledger's **NEXT**.
- **S3** — wrong UX: a dead control, an unreachable row, a snap, a message that did not say which
  address was wrong.
- **S4** — cosmetic.

**Then commit**, on `main`, with the defect entry and any requirement it forced:

```
interstellar: file D-<n> — <symptom in the user's words> (R-AREA-n)

<What was reported, how it was reproduced, what was measured, the judgement, and the
recommended fix.> Paste the reproduction's output.

Co-Authored-By: <the model writing it> <noreply@anthropic.com>
```

Then pull, and push (`arstro.rule` §7).

---

## 6. Definition of done

- [ ] Reproduced **with no GUI**, by a command someone else can paste — or filed `Unreproduced`
      with everything tried.
- [ ] Judged against a **quoted** requirement, not against taste.
- [ ] Evidence is a **measurement**, not a sentence about what the code does.
- [ ] A requirement gap produced an actual requirement.
- [ ] Filed in `DEFECTS.md` with a new, never-reused id, and a **recommended fix that says what not
      to change**.
- [ ] A guarding test named, and checked to fail on the current code.
- [ ] **No product code changed.**
- [ ] Ledger updated if this became **NEXT**; committed, pulled, pushed.
