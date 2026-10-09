---
name: arstro.solaris.debug
description: Use to investigate any suspected problem in Solaris, the Arstro DAW — a sound that is wrong, silent, too loud, late, clicking or different between playback and render; a strip, send, route or solo that does not do what the matrix says; a device parameter that does nothing or lands at the wrong value; a clip, pattern or lane that moved, doubled or lost notes; a .slp that does not round-trip or will not open; an audio device or port that is silent; a command refused or accepted wrongly; an event that lies; a UI that snaps, is cut off, shows a stale value or has a dead control. Reproduces it with no GUI in the loop — a script of command lines through solaris-cc, `get`, a diffable `state print --json --stable`, the `--watch` event stream, `audit`, `matrix print`, the .slp on disk, a rendered WAV MEASURED in numbers, the shot harness for anything visible — judges it against the written requirements, files it in the committed defect list with a pasteable reproduction, RECOMMENDS a fix without applying one, and commits. It never edits product code — landing a fix is arstro.solaris.implement. Invoke for "solaris is broken", "this sounds wrong", "the bass is silent", "why is this value X", "the render differs from playback", "is this a bug?", "/arstro.solaris.debug".
---

# arstro.solaris.debug

> **Invoke `arstro.rule` first**, `arstro.design.rule` when the report is about something visible,
> and `arstro.dsp.implement` §0 when it is about how something sounds. Then read
> `arstro.solaris.implement` §0 (the code map), §1 (the laws) and §5 (the gotchas) — a good share of
> reports are one of those laws working as written, or one of those gotchas come back.

**Reproduce → measure → judge → file → commit.** The output is never only an answer in chat: it is a
committed `D-n` entry in `apps/solaris/docs/DEFECTS.md` with a script anyone can re-run and a number
that shows the problem. **You do not change product code** — not even a one-liner, not in Solaris and
not in the DSP library. You recommend; `arstro.solaris.implement` lands it.

---

## 0. Orient

- `apps/solaris/docs/DEFECTS.md` — is it already filed, or closed and back (a regression)?
- `apps/solaris/docs/API.md` — the grammar, events, model fields, addresses and every device
  parameter as they exist today (generated). A report about a command that does not exist is a
  requirement question, not a defect.
- `REQUIREMENTS.md` + `docs/requirements.md` — what was asked, and what the code claims to do, with
  `file:line`. `docs/PROGRESS.md` — what is NOT built yet (several devices, versions, recording,
  automation): "my second interface is silent" is P2 not done, not a bug.
- `app/NOTES.md` — for a UI report: the command line each control sends, and the shot list.

---

## 1. Reproduce from a shell — always in a sandbox

```bash
export SCRATCH=$(mktemp -d)
export SOLARIS_SETTINGS=$SCRATCH/settings.txt SOLARIS_RECENTS=$SCRATCH/recents   # never the user's files
REPO=/home/namdln/1.workspace/m.yspace/arstro; CC=$REPO/build/apps/solaris/cli/solaris-cc
cd $SCRATCH
ffmpeg -loglevel error -f lavfi -i "sine=frequency=330:duration=1" -ac 1 tone.wav     # media the repro makes itself
ffmpeg -loglevel error -f lavfi -i "anoisesrc=d=0.5:a=0.5" -ac 2 hit.wav
```
Build first if the tree moved (`cmake --build build -j --target solaris-cc`), and say which commit
you reproduced on (`git rev-parse --short HEAD`, and the DSP submodule's).

### A script is the reproduction
```bash
cat > repro.txt <<'EOF'
project new song.slp --bpm 120
clip add --instrument synth --at 0 --length 4
note add pt_1 --pitch 45 --at 0 --length 2
set dv_1.filter.cutoff=800
strip add --kind bus --name Verb
device add ch_3 --type reverb
send add ch_2 --to ch_3 --gain -6
get dv_1.filter.cutoff
matrix print
audit
state print --json --stable
render --out mix.wav --stems ch_2,ch_3
EOF
$CC --watch --script repro.txt > out.txt 2> events.txt; echo "exit=$?"
```
Every line is the grammar the GUI dispatches, so a script reproduces a click (`app/NOTES.md` maps
each control to its line). A refused line prints `refused: <why>` on stderr and exits **3** — the
refusal text is often the diagnosis. `--watch` puts every `[evt]` line on stderr, in order.

### The instruments
| question | instrument |
|---|---|
| what exists, exactly? | `$CC api --json` — commands, flags, events, model fields, addresses, every device parameter with unit and range |
| what value is stored, and was it clamped? | `get <address>`; the `params.changed address=… value=…` event carries what was STORED, not what was typed |
| what is the whole state? | `state print --json --stable` — diff two of them; the only differences should be the ones the commands made |
| what happened, in order? | `--watch` — `project.changed what=… node=…`, `params.changed`, `transport.changed`, `command.rejected line=… why=…`, `render.finished … peak=` |
| where does each strip go? | `matrix print` (`--json`) — every main output and send, with dB and pre; `state print` → `strips[].targets` lists what a strip MAY feed |
| what is wrong with the mix? | `audit` — strips with no clips, clips on muted strips, unreachable strips, single-input buses, offline media, unknown devices, clipping |
| what does it sound like? | `render --out mix.wav [--stems ch_2,ch_3] [--ports] [--from b --to b]` then **measure** (below) — never "it rendered, so it works" |
| does live equal render? | `transport play --from 0` · `wait 2` · `state print` (position, meters) on this machine's PulseAudio; the L2 suite's fake output compares live and offline sample for sample |
| is the file right? | read `song.slp` itself; `project save` twice and `diff` — no change must be literally no diff (fixed point) |
| which devices does this machine have? | `devices list`, `settings print` — the project names ports, the settings map ports to devices |

### Measure the sound — numbers, not adjectives
```bash
cat > $SCRATCH/measure.py <<'EOF'
# usage: python3 -I measure.py file.wav [t0 t1 ...]   — RMS/peak (dBFS) and pitch per window from each t (s)
import sys, wave, struct, math
w = wave.open(sys.argv[1]); n, ch, sw, rate = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
raw = w.readframes(n)
if sw == 3: x = [int.from_bytes(raw[i:i+3], 'little', signed=True) / 8388608.0 for i in range(0, len(raw), 3)]
elif sw == 2: x = [v / 32768.0 for v in struct.unpack('<%dh' % (len(raw) // 2), raw)]
else: x = list(struct.unpack('<%df' % (len(raw) // 4), raw))
L = x[0::ch]; db = lambda v: 20 * math.log10(v) if v > 1e-9 else -math.inf
print(f"{sys.argv[1]}: {rate} Hz, {ch} ch, {n / rate:.3f} s, peak {db(max(map(abs, x))):.1f} dBFS, nan={any(v != v for v in x)}")
for t in map(float, sys.argv[2:] or ['0']):
    a, b = int(t * rate), int(t * rate) + rate // 10                       # a 100 ms window
    s = L[a:b]; r = math.sqrt(sum(v * v for v in s) / max(1, len(s)))
    zc = sum(1 for i in range(1, len(s)) if s[i - 1] < 0 <= s[i])            # rising zero crossings
    print(f"  t={t:7.3f}s  rms {db(r):6.1f} dBFS  peak {db(max(map(abs, s), default=0)):6.1f}  ~{zc * rate / max(1, len(s)):7.1f} Hz")
EOF
python3 -I $SCRATCH/measure.py mix.wav 0 0.45 0.5 1.0      # before / on / after a beat; a stem vs the mix
```
The usual readings: silence where a strip is muted or not reached (below −90 dBFS); a beat's onset
(RMS jumps between the window before and the window on the beat — at 120 bpm a beat is 0.5 s); a
pitch (A2 = 110 Hz, C2 = 65.4 Hz — zero crossings are good to a few Hz on a plain tone, use an FFT for
a rich one); a level change in dB against what `set` asked; `nan=True` is always S1. Compare a stem
with the mix, a render with a render at another block size (the engine suite does this), live with
offline.

### Lower levels — the fastest way to bisect
- **The service:** copy a case from `core/tests/serviceTests.cpp` (`Run r; r.ok("…"); r.no("…")`, the
  events in `r.events`) — the real service with a fake decoder, WAV writer, devices and output.
- **The mix alone:** `engine/tests/engineTests.cpp` builds a `MixGraph` by hand and measures samples —
  for a pan law, a send, a solo, a note's sample position, chunking.
- **The `.slp` alone:** `model/tests/modelTests.cpp` (no sound, milliseconds).
- **The sound alone:** the DSP library's suites (`unittest/buildSynthTests.sh`,
  `tests/run_integration.py`) — if a device sounds wrong in the DSP library's own render too, the
  cause is there.
- **The UI:** `solaris_app_shots --only <state> --outdir $SCRATCH/shots` and READ the PNG; for a
  snap, drive `tests/Rig.h` one `frame()` at a time and compare the LIVE eased value (`…Live(id)`,
  `…Amount(id)`) with the target — a transition is only provable mid-tween (`arstro.design.rule` §1);
  for a wiring question, `DISPLAY=:1 timeout 5 build/apps/solaris/solaris song.slp` (exit 124 = ran).

---

## 2. Judge — defect, intended, or requirement gap

Read the requirement **before** forming an opinion; quote the R-/DR- line each finding is judged
against, or write "no R- line: requirement gap" — the implement skill's requirement check starts from
your table.

**Intended — not defects** (each is written down; quote it):
- **A muted strip sends nothing, pre-fader sends included** (decision, PROGRESS 2026-10-08).
- **`render --out` is the master bus** after its rack and gain; a strip routed straight to a port is
  not in the mixdown — `--ports` writes what each port receives (decision).
- **Solo keeps the path alive** (R-MIX-7): a strip feeding or fed by a soloed strip stays audible.
- **A route or send to the same or an earlier mixer is refused**, and so is a `mixer move` that would
  point any route backward (R-MIX-4).
- **A new source strip feeds the first bus on a later mixer** ("Main"), else the master; a clip with
  no `--lane` gets a new lane (decisions).
- **Editing a note changes every copy of the clip** — duplicates are linked (R-CLIP-3); `clip unique`
  diverges.
- **An instrument strip keeps its instrument** — `device remove` on it is refused.
- **A device type or parameter this build does not know is kept in the file and left out of the
  sound**, and `audit` names it (R-FMT-3 spirit, DR-SVC).
- **A value is clamped to the registry's range**; the event says what was stored.
- **A strip's default colour comes from its id** (R-UI-7); **mixer fold groups start open** (R-MIX-12
  amended).
- **Only one audio device plays today** (P2 not built) — the others' ports are silent by design.

**Always a defect, whatever a document says:** a crash or hang; a `.slp` that loses or corrupts data
on save; output containing NaN/inf; a render that changes with the block size, the run, or the
thread timing (R-RENDER-1); live playback that differs from the offline render (DR-PLAY-1); an
allocation, lock or file access on the audio thread (R-PLAY-2); a refused command that changed the
song or announced a change (D-1's class); an unknown verb, flag, address, device type or parameter
that was ACCEPTED (R-SVC-3); sound produced in `apps/solaris/` instead of the DSP library (R-DSP-1);
a behaviour reachable only by clicking (R-G-4); a visible value that changes in one frame
(`arstro.design.rule` §1); a control cut off with no way to reach it (R6).

**A requirement gap** — the documents are silent or contradict each other: decide what it should be
from the neighbouring requirements and `docs/discussion.md`, write the R- line (conflict-checked), then
re-judge against it. If it is a product decision, write the option you recommend marked
`*Open — needs confirmation*`, ask the user, and file provisionally. **A number in a requirement needs
something that reads it back** (a test, a model field, an event) — a claim nothing measures is where
to look first for the defect (cosmo's R-CPU-4 lesson).

**Before blaming new code, check the known traps** (`arstro.solaris.implement` §5): the device warm-up
and per-block parameter smoothing; per-channel state shared across channels (depends on block size);
an event between block boundaries in a test; strips in PROCESSING order (`strips.back()` is not the
newest); a stale reference into `model()` after a dispatch; a relative `--src` resolved against the
song's folder; a path with a space unquoted; the DSP submodule pointer not bumped (Solaris builds the
old sound).

---

## 3. Severity, and what to do about it

- **S1** — crash, hang, data loss, a corrupted `.slp`, NaN on the output, a sound that could damage
  ears or speakers (a full-scale burst). File it and tell the user at once: what breaks, what to avoid
  until it is fixed, that `arstro.solaris.implement` should run next.
- **S2** — a wrong result a user would ship: a wrong mix, a silent strip, a render that differs from
  playback, a lost edit. File it and make it the ledger's **NEXT**.
- **S3** — wrong behaviour with a workaround. File; schedule in the ledger.
- **S4** — cosmetic or diagnostic. File; batch.

---

## 4. File it — `apps/solaris/docs/DEFECTS.md`

`D-<n>`: the next number across Open and Closed, never reused. Newest first under `## Open`:

```markdown
### D-n — <the symptom, in the user's words>
- **Area:** model | engine | dsp | service | live | host | cli | ui · **Status:** Confirmed | Unreproduced | Not-a-defect
  · **Severity:** S1–S4 · **Found:** <date>, <reported by / found while …>, on <commit> (DSP <commit>)
- **Reproduce:** `apps/solaris/tests/repro/D-n.txt` — run as `solaris-cc --watch --script D-n.txt` in a
  sandbox (§1); it makes its own media. Plus the measure line, if the evidence is a sound.
- **Expected:** the R-/DR- line, quoted.
- **Actual:** what happened — the refusal, the event, the dump diff.
- **Evidence:** a MEASUREMENT — `mix.wav t=0.500s rms −12.3 dBFS, stem ch_2 −91.0 dBFS`; two stable
  dumps diffed; an event line — not an argument.
- **Judgement:** defect (contradicts R-…) / requirement gap (closed by R-… written while filing).
- **Cause:** `file:line`, and why. **Regression?** `git log -S '<line>'` — when, which commit.
- **Recommended fix:** the change and the layer it belongs at (and why not the layer the symptom shows
  at); rejected alternatives, one line each; what else reads what it changes; **the test that should
  guard it** — named, at the lowest level, and what it must assert to FAIL on today's code.
```
A defect whose cause is in the DSP library is filed here with Area `dsp`, names the DSP `REQ-` id it
violates, and recommends the fix in `core/DigitalSignalProcessing` (two commits: submodule first). A
cause in a cosmo or Interstellar widget Solaris borrows gets an entry in that app's DEFECTS too,
cross-referenced both ways. When a defect is fixed, the implement skill moves it whole to `## Closed`
with the fix's commit and the guarding test — entries are never deleted.

**The reproduction is committed**, not left in the message: the script in
`apps/solaris/tests/repro/D-n.txt` (create the folder the first time), or a throwaway service test
case. If you could not reproduce, file `Unreproduced` with everything you tried and on which commit.

---

## 5. Report — the output IS the deliverable

In this order, each part short:
1. **What the problem is**, in the user's terms, before any machinery — *"the bass is silent in the
   render but not in playback"*.
2. **What proves it** — the numbers, pasted (dBFS, Hz, a diff, an event line).
3. **Why it happens** — the cause with `file:line`; a symptom the user did not mention that shares
   it; anything they suspected that is actually fine, and why.
4. **Whether it is a regression** — checked with `git log -S`, not assumed.
5. **What you recommend** — one change, at the layer the cause is at, its guarding test, its risk;
   the rejected alternatives in a line each. If the requirement was the problem, the recommendation
   is a requirement.
6. **That nothing was fixed**, and that `arstro.solaris.implement` lands it.

## 6. Commit — the diagnosis, never a fix

The commit holds `DEFECTS.md`, the repro script, any requirement written or amended, the `PROGRESS.md`
update (NEXT for an S1/S2) — **and no product code**:
`solaris: D-n filed — <symptom> (<R-tag>)`, with the evidence in the body. Pull with rebase, push
(`arstro.rule` §7). `git add` named files only — never the unrelated work others leave in the tree.

## 7. Definition of done

- [ ] Reproduced **with no GUI in the loop** (a script against the service, a test, or the shot
      harness for a visual report), in a sandbox, with media the script makes itself — or filed
      `Unreproduced` with everything tried.
- [ ] **Measured**: the evidence is a number or a diff, not an adjective; a sound was measured, a
      picture was looked at, a transition was sampled mid-tween.
- [ ] Judged against a **quoted** requirement; R-SVC-3 and R-G-4 considered, not only the area's.
- [ ] Any requirement gap closed in `REQUIREMENTS.md`, conflict-checked, with something that reads
      back any number in it.
- [ ] Filed in the §4 shape with a committed reproduction, a `file:line` cause, the regression check
      and a recommended fix with the test that must fail today. Cross-filed if the cause is borrowed.
- [ ] **No product code changed** — `git status` shows only `DEFECTS.md`, the repro, requirements and
      the ledger. Committed and pushed; the user told which skill lands it.
