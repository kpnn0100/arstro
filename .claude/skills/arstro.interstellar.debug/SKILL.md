---
name: arstro.interstellar.debug
description: Use to investigate any suspected problem in Interstellar, the video colour tool that can cut — a grade that does not reach the Cosmo .cmp, a version that does or does not inherit its base's colour, a pin that yields, a rebase that loses an override, a clip that moved or lost frames, a render that is black or differs from Cosmo, an offline source, a command refused or accepted wrongly, a UI that snaps, is cut off or has a dead control. Reproduces it with no GUI in the loop — a script of command lines through interstellar-cc, `eval <address> --explain`, a diffable `state print --json --stable`, the `--watch` event stream, `lint`, the .isp/.cmp on disk, a rendered PNG — judges it against the written requirements, files it in the committed defect list with a pasteable reproduction, RECOMMENDS a fix without applying one, and commits. It never edits product code — landing a fix is arstro.interstellar.implement. Invoke for "interstellar is broken", "the version shows the wrong colour", "why is this value X", "the render is black", "is this a bug?", "/arstro.interstellar.debug".
---

# arstro.interstellar.debug

> **Invoke `arstro.rule` first**, and `arstro.design.rule` when the report is about something
> visible. Then read `arstro.interstellar.implement` §0 (the code map) and §5 (the gotchas) — half
> of all reports are one of those gotchas.

**Reproduce → judge → file → commit.** The output is never only an answer in chat: it is a committed
`D-n` entry in `apps/interstellar/docs/DEFECTS.md` with a command anyone can re-run. **You do not
change product code** — not even a one-liner. You recommend; `arstro.interstellar.implement` lands.

---

## 0. Orient

- `docs/DEFECTS.md` — is it already filed? D-1 (Cosmo alone cannot show a video) and D-2 (Cosmo's
  save deletes an offline source; Interstellar refuses saves while one is offline) explain many
  "the video is missing in Cosmo" and "save was refused" reports.
- `docs/API.md` — the grammar, events, model fields and addresses as they exist today (generated).
- `REQUIREMENTS.md` + `docs/requirements.md` — what was asked, and what the code claims to do.

---

## 1. Reproduce from a shell — always in a sandbox

```bash
export SCRATCH=$(mktemp -d) HOME=$SCRATCH INTERSTELLAR_RECENTS=$SCRATCH/recents
CC=$REPO/build/apps/interstellar/cli/interstellar-cc
cd $SCRATCH
ffmpeg -loglevel error -f lavfi -i testsrc2=size=640x360:rate=24:duration=4 -pix_fmt yuv420p a.mp4
ffmpeg -loglevel error -f lavfi -i smptebars=size=640x360:rate=24:duration=3 -pix_fmt yuv420p b.mp4
```
`HOME` matters: Cosmo's service reads and writes a recents index under it.

### A script is the reproduction
```bash
cat > repro.txt <<'EOF'
project new mv.isp --res 640x360
rack add a.mp4 b.mp4
set a.basic.exposure=0.5
track add --kind video
clip add --track v0 --src a --in 0 --out 2 --at 0 --name shotA
timeline new social30 --base main
timeline open social30
set a.basic.exposure=0.8
eval a.basic.exposure --explain
state print --json --stable
EOF
$CC --watch --script repro.txt
```
Every line is the same grammar the GUI dispatches, so a script reproduces a click. A refused line
prints `refused: <why>` and exits 3 — the refusal text is often the diagnosis.

### The instruments
| question | instrument |
|---|---|
| what value does this address resolve to, and from where? | `eval <address> --timeline <tl> --explain` — names the colour source (`rack` or `pin@<commit>`), the own value, the override and which timeline it came from, each ancestor group's contribution, bypass, the effective value |
| what is the whole state? | `state print --json --stable` — diffable; two front ends showing the same state print the same text |
| what happened, in order? | `--watch` — every `[evt]` line; `params.changed … target=rack|version|clip` tells you where a write went |
| what is broken in the project? | `lint` — offline media, dangling deltas, refused nodes |
| what does this version change? | `timeline diff <tl>`, `timeline list` |
| did the colour reach Cosmo? | `grep -A30 '^#image' mv.cmp` — read the FILE, not Interstellar's model; or `cosmo-cc project mv.cmp --print --node-params` |
| are the pixels right? | `export-still --timeline <tl> --out s.png --at <t>` and **Read the PNG**; for Cosmo agreement, `apps/interstellar/tests/still_equals_cosmo.sh` |
| is the `.isp` stable? | save twice; `diff` — "no change" must be literally no diff |

### Lower levels
- The real service in a test: copy a case from `core/tests/serviceL2Tests.cpp` (fake decoders, real
  rack) — the fastest way to bisect a version/colour bug.
- The model alone: `model/tests/modelTests.cpp` (no Cosmo, milliseconds).
- The render path alone: `render/tests/renderTests.cpp` (fixed inputs).
- The UI: the app's shot harness and UI tests named in `apps/interstellar/app/NOTES.md` — render the
  state, dump the tree, and look; for motion, pump one frame at a time and compare the LIVE eased
  value with the target (`arstro.design.rule` §1).

---

## 2. Judge — defect, intended, or requirement gap

Read the R- tag. Common verdicts:
- **A version shows the base's new colour** — intended: colour inherits live (R-VER-2). Only a pin
  stops it (R-VER-3).
- **A curve edit on a version is refused** — intended (DR-VER-2): a version override is a scalar
  delta. A second look is `rack duplicate` (R-RACK-5).
- **`project save` says "saved the .isp but NOT the rack"** — intended mitigation of D-2.
- **A render without `--timeline` is refused** — intended (R-RENDER-1).
- **A value differs between `get` and `eval`** — `get` is the node's own value in this version;
  `eval` folds its ancestor groups. Not a defect unless `--explain`'s layers do not add up.
- **The video source is `failed` in Cosmo** — D-1.
- Anything that **loses a user's edit, silently** is S1, whatever else it is.

If the requirement is silent or contradicts itself, that is a **requirement gap**: file it as such
and recommend the R- amendment; do not decide product behaviour in a defect entry. **Every report is
checked against the requirements, item by item**: quote the R- line each finding is judged against
(or write "no R- line: requirement gap"), so the implement skill's mandatory requirement check (its
§2.1) starts from your table rather than from scratch.

---

## 3. File it — `apps/interstellar/docs/DEFECTS.md`

`D-<n>`, next number, never reused. Under `## Open`:

```
### D-n — <the symptom, in the user's words>
- **Area:** rack | versions | model | arrange | volume | render | service | cli | ui · **Status:**
  Confirmed (measured) · **Severity:** S1 data loss / S2 wrong result / S3 degraded / S4 cosmetic ·
  **Found:** <date>, <how>
- **Reproduce:** a pasteable script (sandboxed, generating its own media)
- **Expected:** quote the R-/DR- line
- **Actual:** the output, pasted — a measurement, not an adjective
- **Cause:** `file:line`, and why
- **Requirement:** R-…
- **Recommended fix:** what to change, where, and the test that should guard it (and that it must
  fail before the fix)
```
A defect whose root cause is in **Cosmo** gets a Cosmo entry too (`apps/cosmo/docs/DEFECTS.md`, its
own numbering) and a cross-reference both ways — as D-2 ↔ Cosmo D-66.

---

## 4. Report, commit, push

Report: the verdict, the reproduction, the evidence, the recommendation — and that nothing was
fixed. Commit only the DEFECTS entry (and a PROGRESS note if it changes NEXT):
`interstellar: D-n filed — <symptom> (<R-tag>)`. Fetch, rebase if behind, push.

## 5. Definition of done

- Reproduced from a shell in a sandbox, with media the script generates itself.
- Judged against a quoted requirement.
- Filed with a pasteable reproduction, a measurement, a `file:line` cause and a recommended fix with
  its guarding test. Cross-filed in Cosmo if the cause is there.
- No product code changed. Committed and pushed.
