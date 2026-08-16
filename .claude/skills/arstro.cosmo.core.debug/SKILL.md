---
name: arstro.cosmo.core.debug
description: Use to investigate any suspected non-UI problem in the cosmo photo editor — wrong pixels, an adjustment that does nothing, a crash or hang, a project/preset/settings file that loses data, a slow or stalled image load, a bad export, GPU-vs-CPU divergence. Reproduces it from a shell (cosmo-cc, scripted runs, seeded config, the debug log), judges against the written requirements whether it is a defect or intended behaviour, files it in the committed defect list with the exact reproduction command, and commits — whether or not it is fixed in the same session. Invoke for "cosmo is broken", "the export looks wrong", "it crashed", "is this a bug?", "why is loading slow", "/arstro.cosmo.core.debug". For visual/layout/interaction problems use arstro.cosmo.design.debug.
---

# arstro.cosmo.core.debug

The **reproduce → judge → file → commit** workflow for cosmo's engine, session, persistence, load and
export. Its output is never just an answer in chat: it is a **committed defect entry with a command
anyone on any machine can re-run**, plus — when the requirements turned out to be silent — a requirement
that now says what the right behaviour is.

Fixing is the sibling skill's job (`arstro.cosmo.core.implement`). This skill may fix a defect it has
reproduced when the fix is small and obvious, but **filing is mandatory and fixing is optional.** Never
end a debugging session with the finding only in the conversation.

---

## 0. Orient

Read, in order: `apps/cosmo/docs/DEFECTS.md` (it may already be filed — if so, add evidence to the
existing entry rather than opening a duplicate) · `apps/cosmo/docs/PROGRESS.md` (what changed recently is
where bugs live; check the last commits) · `apps/cosmo/REQUIREMENTS.md` and
`apps/cosmo/docs/requirements.md` for the area involved. Then read
`arstro.cosmo.core.implement` §0 for the code map and §5 for the shell surface.

---

## 1. The loop

1. **Capture the report.** Write down what the user saw, in their words, before you theorise.
2. **Reproduce from a shell** (§2). No reproduction, no defect entry — file it as *Unreproduced* with
   everything you tried, which is itself useful.
3. **Minimize** — smallest image, fewest params, fewest steps, one command.
4. **Judge** (§3): defect, intended behaviour, or **requirement gap**.
5. **File it** (§4) in `apps/cosmo/docs/DEFECTS.md`, with the reproduction command and the log excerpt.
6. **Close the requirement gap if there is one** (§3c) — write the requirement before anything else.
7. **Fix it, or hand it to the implement skill** (§6) — and if you fix it, the test comes first.
8. **Commit** (§7). The defect list is committed **even when nothing was fixed.**

---

## 2. Reproduce from a shell

Always in a sandbox, so a run cannot corrupt the user's real config and so the log is only this run:

```bash
S=/tmp/cosmo-dbg && rm -rf $S && mkdir -p $S
export XDG_CONFIG_HOME=$S
printf 'cosmosettings=1\npreviewEdge=1600\nthreads=1\nuseGpu=0\n' > $S/cosmo_v2/settings.txt   # deterministic
./build/apps/cosmo/cosmo.exe --debug <args>          # once the flags of §5 exist
tail -n 200 $S/cosmo_v2/cosmo_v2.log
```

Fix the variables that make runs differ: **`threads=1` and `useGpu=0` first** — if the symptom vanishes,
you have learned something enormous (a race, or a GPU-vs-CPU divergence) rather than lost the
reproduction. Then turn each back on separately.

**Prefer the headless path.** `cosmo-cc` reproduces engine, project, preset and export bugs with no
window at all, and its output is diffable text or a PNG:

```bash
cosmo-cc info photo.RW2                                  # did it even decode, and how
cosmo-cc render photo.RW2 --set exposure=1.2 -o a.png    # is the pixel wrong, without any UI
cosmo-cc render photo.RW2 --cpu -o cpu.png ; cosmo-cc render photo.RW2 --gpu -o gpu.png
cosmo-cc project bad.cmp --print                         # what the file actually says
cosmo-cc params --diff before.cosmo after.cosmo          # which field changed
cosmo-cc export bad.cmp --outdir out/                    # export without the modal
cosmo-cc backends                                        # threads, backend, LibRaw, build defines
```

For anything that needs the UI in the loop, use a `--script` file (grammar in
`arstro.cosmo.core.implement` §5.3) so the reproduction is a committed file, not a sequence of clicks.

**If the harness you need does not exist yet**, that is itself the finding: file the blind spot as a
defect (see the seeded `D-` entries in `DEFECTS.md`), reproduce with what does exist — bare
`cosmo.exe <image>`, seeded `settings.txt`/`recent.tsv`/`.cmp` files, the log, `ctest`, and a throwaway
test in `sessionTests.cpp` — and note in the entry which harness item (A1–A12) would have made it a
one-liner.

**Narrow it with the existing suites.** A failing `cosmo_core_tests` or `image_tests` case is a better
reproduction than any app run: `cd build && ctest -R cosmo --output-on-failure`, or add a temporary test
that fails, and keep it as the regression guard if it turns out to be a real defect.

---

## 3. Judge — defect, intended, or requirement gap

Read the requirement **before** you form an opinion. Search both tiers for the area
(`REQUIREMENTS.md` `R-<AREA>`, `docs/requirements.md` `DR-<AREA>`), and quote the sentence you judged
against in the defect entry.

**a) The behaviour contradicts a written requirement → it is a defect.** Confirmed. File it, cite the
requirement it violates.

**b) The behaviour matches a written requirement → it is not a defect.** Say so plainly, quote the
requirement, and explain the reasoning behind it if the doc gives one. If the user still wants it
changed, that is a **requirement change**, not a bug: amend the `R-` entry in place with
`**AMENDED (…)**` and one line of why, then hand the work to `arstro.cosmo.core.implement`. Do not
"fix" behaviour that a requirement asks for — you would break whatever that requirement was protecting.

**c) No requirement covers it → this is a requirement gap, and closing it is part of this task.**
This is the case the skill exists for. Do not silently pick a side.
1. Decide what the behaviour *should* be, from the surrounding requirements, the design rationale in
   `docs/design.md`, and how the neighbouring features behave. State your reasoning.
2. **Write the requirement** — a new `R-<AREA>-<n>` in `REQUIREMENTS.md` — and conflict-check it against
   every existing requirement in that area.
3. **Now re-judge against it.** Usually the observed behaviour is then a defect; occasionally the
   requirement you just wrote blesses it, and you file nothing but the requirement.
4. If the right answer genuinely depends on what the user wants (a product decision, not a technical
   one), write the requirement as the option you recommend, mark it `*Open — needs confirmation.*`, ask
   the user, and file the defect against it provisionally.

**Always a defect regardless of what any document says:** a crash, a hang, data loss or silent
corruption of a saved file, a memory or handle leak, a data race, wrong pixels the pipeline order
cannot explain, or a GPU path that disagrees with the CPU reference.

**Before blaming new code, check these cosmo-specific traps** — each has bitten this codebase already:
slot id must equal the index in every `mSlot*` vector (`resetWorkspace()` must reset the service);
`RenderService::render()` coalesces, so a missing frame may be a *superseded* request, not a lost one;
`composeParams` follows `base` for crop and `curveLog` but adds `rotation` — a "wrong crop in a group" is
often correct-by-design; `effectiveParams` and `effectiveEditParams` differ on bypass deliberately; a
stalled load is usually the `OrderedParallelLoad` window/byte-cap predicate, whose only safety is the
`i == mConsumed` exemption; a param that round-trips lossily is a missing case in `EditParamsIO` or
`EditParamsApf`, not an engine bug; and the engine works in **linear light**, so a processor that looks
"too strong" may be assuming sRGB.

---

## 4. File it — `apps/cosmo/docs/DEFECTS.md`

One entry per defect, newest first in the Open section. IDs are `D-<n>`, sequential across both debug
skills, **never reused**. Keep the exact shape below; the debug skills and the ledger cross-reference it.

```markdown
### D-12 — Group crop is ignored when the child is bypassed
- **Area:** core / engine            <!-- core | design -->
- **Status:** Open                   <!-- Open | Confirmed | Fixed | Not-a-defect | Unreproduced | Deferred -->
- **Severity:** S2                   <!-- S1 data loss/crash · S2 wrong output · S3 wrong UX · S4 cosmetic -->
- **Found:** 2026-08-16, by inspection during D-11 / reported by the user
- **Reproduce:**
  ```bash
  XDG_CONFIG_HOME=/tmp/cosmo-dbg ./build/apps/cosmo/cosmo-cc.exe render sample.jpg \
      --params tests/fixtures/group-crop.cosmo -o out.png
  ```
- **Expected:** the group's crop applies to the member (R-GROUP-3: "a group's params compose onto
  every member").
- **Actual:** the member renders uncropped; `cosmo-cc project --print` shows the crop present on the
  group node.
- **Evidence:** log line `[DEBUG] [render] preview slot=0 crop=0,0,1,1 (group crop 0.1,0.1,0.8,0.8 dropped)`
- **Judgement:** defect — contradicts R-GROUP-3. *(or: requirement gap, closed by new R-GROUP-5.)*
- **Cause:** `composeParams` follows `base` for crop, and the leaf's own crop is full-frame.
- **Requirement:** R-GROUP-3 (existing) / R-GROUP-5 (written while filing this)
- **Fix:** commit `abc1234`; guarded by `group_crop_composes_onto_member` in `sessionTests.cpp`.
```

Rules: **the Reproduce block must be a command someone else can paste**, not a description. Quote the
requirement you judged against, verbatim enough to be checkable. If you could not reproduce, still file
it with Status `Unreproduced` and list everything you tried — the next session on another machine starts
from there instead of from zero. When a defect is resolved, move the whole entry to the `## Closed`
section with its commit hash and the test that now guards it; never delete an entry.

---

## 5. Severity, and what to do about it

- **S1** — crash, hang, data loss, corrupted save. Stop and fix now, or if you cannot, file it and tell
  the user plainly what to avoid in the meantime.
- **S2** — wrong output the user would ship (bad pixels, bad export, a lost adjustment). Fix in this
  session if it is contained; otherwise file and make it the ledger's **NEXT**.
- **S3** — wrong behaviour that has a workaround. File; schedule in the ledger.
- **S4** — cosmetic or diagnostic. File; batch.

---

## 6. Fix, or hand off

If you fix it here, you are running the implement skill's rules — all of them:
1. Requirement first (already done in §3).
2. **Write the failing test before the fix** and watch it fail; keep it as the regression guard. Put it
   in `sessionTests.cpp` (plain `assert()`) or `engineTests.cpp` (MiniTest) in the local style.
3. Fix, respecting the invariants in `arstro.cosmo.core.implement` §0.
4. Add the debug log line that would have made this obvious the first time — a defect the log could not
   have shown you is also an observability defect. Consider filing that separately.
5. Sync the docs (`arstro.cosmo.core.implement` §8) and update the `DR-` entry if as-built behaviour
   changed.
6. Move the entry to Closed with the commit hash and the test name.

If you do not fix it: leave Status `Open`/`Confirmed`, add it to `PROGRESS.md` under the right milestone
so it is scheduled rather than forgotten, and say clearly in your report what is broken, how bad it is,
and what the workaround is.

---

## 7. Commit — always

Even a session that fixed nothing ends in a commit, because the defect list is the deliverable:

```
cosmo: file D-12 — group crop dropped when the member is bypassed (R-GROUP-3)

Reproduced headlessly with cosmo-cc render …; the group's crop is dropped
because composeParams follows base for framing. Requirement R-GROUP-5 written
to state what a bypassed member should inherit. Not fixed yet; PROGRESS NEXT
now points at it.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
```

Include in the same commit: `DEFECTS.md`, any requirement you wrote or amended, the fixture or `--script`
file your reproduction needs, and the `PROGRESS.md` update. Do not push unless asked.

---

## 8. Definition of done

- [ ] The symptom was reproduced from a **shell command that is written down**, or is filed as
      `Unreproduced` with everything tried.
- [ ] Judged against a **quoted** requirement — defect, not-a-defect, or requirement gap.
- [ ] Any requirement gap is **closed in `REQUIREMENTS.md`** (conflict-checked), not left implicit.
- [ ] The defect entry is complete: repro, expected, actual, evidence, judgement, severity.
- [ ] If fixed: a test that fails without the fix, docs synced, entry moved to Closed with the hash.
- [ ] If not fixed: scheduled in `PROGRESS.md`, and the user was told plainly.
- [ ] Committed to `main`.
