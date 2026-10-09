# Solaris for agents — making a song with `solaris-cc`

> For an AI agent (or anyone scripting). `solaris-cc` is the whole DAW with no window: every line is the
> grammar in [`API.md`](API.md) (generated: every command, address, device parameter with its range, the
> notation). This page is how to use it well. Requirement: R-SVC-8 (as built: DR-SVC-8).

## 1. Run lines

| how | use it for |
|---|---|
| `solaris-cc --script song.txt` | a whole song in a file, one command per line, `#` comments (also after a command: ` # …`) |
| `solaris-cc shell --song song.slp` | a session on stdin: ONE song, its ids and undo last; each line prints its output at once |
| `solaris-cc <cmd> : <cmd> : …` | a quick one-shot (` : ` chains) |

Exit codes: `0` ok · `2` usage · `3` a line was refused — stderr says `refused: line N: <why, with the
nearest names>`; a script stops there unless `--keep-going` · `4` the song has **unsaved edits** — end
with `project save` (or pass `--discard` if you mean it). Set `SOLARIS_SETTINGS` and `SOLARIS_RECENTS` to
scratch files when testing, so the user's own settings are not touched.

## 2. Ids

Every node has an id: `ch_` strip, `dv_` device, `pt_` pattern, `ln_` lane, `ac_` clip, `sd_` send,
`au_` automation, `mx_` mixer, `prt_` port. A command that makes something prints its id **first**, then
`made:` with everything else it made:

```
$ clip add --instrument drums --at 0 --length 32
ac_1
made: strip=ch_2 device=dv_1 pattern=pt_1 lane=ln_1
```

Ids are allocated in order (the highest of a prefix + 1, never reused) and a new song always holds
`mx_1` Sources, `mx_2` Buses, `ch_1` Main (a bus → master) and `prt_1`. So a script can name what its
earlier lines made — and the `made:` lines of its run confirm it. In `shell`, read the id, then use it.

## 3. The notation

| | written | e.g. |
|---|---|---|
| pitch | a number 0–127, a name (C4 = 60, `#`/`b`, c-1 = 0), or a kit pad | `60` `C4` `F#3` `Bb2` `kick` `closed-hat` |
| note | `<pitch>@<beat>[:<length>[:<vel>]]` — an empty field = the default | `C4@0:0.5:90` `kick@1` `E4@2::70` |
| step row | `x` note · `X` accent (127) · `.` rest; spaces and `\|` ignored | `x...x...x...x...` |
| chord | root + quality (`m`, `7`, `maj7`, `m7`, `dim`, `sus4`, `add9`, `9` … — API.md lists 21) | `Cm7` `F#dim` `Bbmaj7` |

Drum Machine pads: `kick` 36 · `rim` 37 · `snare` 38 · `clap` 39 · `low-tom` 41 · `closed-hat` 42 ·
`mid-tom` 45 · `open-hat` 46 · `high-tom` 48 · `cowbell` 56.

```
notes add pt_2 "C2@0:0.5 Eb2@1:0.5 G2@2:1:90" --length 0.25 --vel 100   # many notes, ONE edit, ONE undo
pattern steps pt_1 --pitch kick "x...x...x...x..." [--step 0.25] [--at 0] [--vel 100]   # replaces that row
note add pt_3 --chord Cm7 --at 0 --length 4 [--octave 4] [--inversion 1]
pattern duplicate pt_1 · pattern clear pt_1 [--pitch kick] · pattern transpose pt_2 --semi -12 · pattern delete pt_9
```

A bad token refuses the whole line by its position (`note 3 \`X9@2\`: …`) and nothing lands.

## 4. Arrangement

- A **pattern loops** inside a longer clip at the pattern's length (new patterns are 4 beats). A bar of
  drums in a 32-beat clip repeats 8 times. For a longer part, set the length first:
  `set pt_2.length=32` — a note at or after the end is warned at once and named by `audit`.
- `clip add` with no `--lane` lands on its strip's lane (a new lane only for a new strip; `--lane new`
  asks for one). `clip duplicate ac_1 --count 3` = three linked copies, end to end.
- A new instrument or audio strip feeds `ch_1` Main; `strip add --kind bus` lands on Buses → master.
- Values are in the units `API.md` lists (Hz, dB, ms, st); one out of range is **refused** with the range.

## 5. A worked song — 8 bars, 32 commands

Beethoven's *Ode to Joy* (public domain). The comments show what each making line printed.

```
project new ode.slp --bpm 120 --name "Ode"

clip add --instrument drums --at 0 --length 32        # ac_1 · made: strip=ch_2 device=dv_1 pattern=pt_1 lane=ln_1
pattern steps pt_1 --pitch kick       "x...x...x...x..."
pattern steps pt_1 --pitch snare      "....x.......x..."
pattern steps pt_1 --pitch closed-hat "..x...x...x...x." --vel 70

clip add --instrument synth --at 0 --length 32        # ac_2 · made: strip=ch_3 device=dv_2 pattern=pt_2 lane=ln_2
set pt_2.length=32 dv_2.osc2.octave=0 dv_2.filter.cutoff=500 dv_2.amp.decay=180 dv_2.amp.sustain=0.3
notes add pt_2 "C2@0 C2@1 C2@2 C2@3 G1@4 G1@5 G1@6 G1@7 C2@8 C2@9 C2@10 C2@11 G1@12 G1@13 G1@14 G1@15 C2@16 C2@17 C2@18 C2@19 G1@20 G1@21 G1@22 G1@23 C2@24 C2@25 C2@26 C2@27 G1@28 G1@29 C2@30 C2@31" --length 0.5 --vel 110

clip add --instrument synth --at 0 --length 32        # ac_3 · made: strip=ch_4 device=dv_3 pattern=pt_3 lane=ln_3
set pt_3.length=32 dv_3.amp.attack=150 dv_3.amp.release=500 dv_3.filter.cutoff=1500 dv_3.osc2.level=0.3
note add pt_3 --chord C --at 0  --length 4 --vel 70
note add pt_3 --chord G --at 4  --length 4 --vel 70 --octave 3 --inversion 1
note add pt_3 --chord C --at 8  --length 4 --vel 70
note add pt_3 --chord G --at 12 --length 4 --vel 70 --octave 3 --inversion 1
note add pt_3 --chord C --at 16 --length 4 --vel 70
note add pt_3 --chord G --at 20 --length 4 --vel 70 --octave 3 --inversion 1
note add pt_3 --chord C --at 24 --length 4 --vel 70
note add pt_3 --chord G --at 28 --length 2 --vel 70 --octave 3 --inversion 1
note add pt_3 --chord C --at 30 --length 2 --vel 70

clip add --instrument synth --at 0 --length 32        # ac_4 · made: strip=ch_5 device=dv_4 pattern=pt_4 lane=ln_4
set pt_4.length=32 dv_4.osc1.wave=square dv_4.osc2.level=0 dv_4.filter.cutoff=3000 dv_4.amp.release=120
notes add pt_4 "E4@0 E4@1 F4@2 G4@3 G4@4 F4@5 E4@6 D4@7 C4@8 C4@9 D4@10 E4@11 E4@12:1.5 D4@13.5:0.5 D4@14:2 E4@16 E4@17 F4@18 G4@19 G4@20 F4@21 E4@22 D4@23 C4@24 C4@25 D4@26 E4@27 D4@28:1.5 C4@29.5:0.5 C4@30:2" --length 0.9

strip add --kind bus --name Verb                       # ch_6
device add ch_6 --type reverb                          # dv_5
send add ch_4 --to ch_6 --gain -6
send add ch_5 --to ch_6 --gain -10
set ch_2.gain=-4 ch_3.gain=-6 ch_4.gain=-14 ch_5.gain=-9
device add master --type limiter                       # dv_6

ls
audit
project save
render --out ode.wav --stems ch_2,ch_5
```

Measured when this page was written: 16 s + a 1.4 s tail, RMS −21.8 dBFS, peak −6.5 dBFS; the kick
18.6 dB or more above the level just before every one of the beats; onsets 500.0 ms apart (120 bpm); the
lead at E4, G4, C4, D4 within 0.2 Hz.

## 6. Read it back

| command | gives |
|---|---|
| `ls` | the tree: mixers → strips (kind, devices, output, sends), master, lanes → clips (`@at len`, pattern or file), patterns (length, notes, clips), automations |
| `show <id>` | one node's non-default fields; a device's changed parameters with unit and default |
| `pattern print <pt>` | its notes in the notation above (pads by name), the MIDI number after `#` |
| `state print --json --compact` | the song as one JSON line: no registry or machine, devices only `name=value` that differ, notes in the notation |
| `audit` | what is wrong: unused or unreachable strips, silent clips, empty/unused patterns, notes past a pattern's end, clipping in the last render |
| `get <address>` · `eval <address> --at <b> --explain` | one value; what a formula plays and why |
| `matrix print` | every route and send |

None of these is an edit: no undo step, the song stays saved.

## 7. Check the render

A render that "succeeded" can be silent. Render stems for the parts you care about and measure them —
Python's `wave` + `numpy` is enough:

```python
import wave, numpy as np
def load(p):
    w = wave.open(p); b = np.frombuffer(w.readframes(w.getnframes()), np.uint8).reshape(-1, 3)   # 24-bit PCM
    x = b[:, 0].astype(np.int32) | b[:, 1].astype(np.int32) << 8 | b[:, 2].astype(np.int32) << 16
    return np.where(x >= 1 << 23, x - (1 << 24), x).reshape(-1, w.getnchannels()) / 2**23, w.getframerate()
mix, rate = load("ode.wav"); spb = rate * 60 / 120                                            # samples per beat
print("peak dBFS", 20 * np.log10(abs(mix).max()), "RMS dBFS", 20 * np.log10(np.sqrt((mix ** 2).mean())))
kick = load("ode.ch_2.wav")[0].mean(1); w = int(0.04 * rate)
for b in range(1, 32):                                                   # the kick on every beat
    s = int(b * spb); assert np.sqrt((kick[s:s+w]**2).mean()) > 2 * np.sqrt((kick[s-w:s]**2).mean())
```

Check, at least: not silent (RMS above about −40 dBFS), peak under 0 dBFS (`audit` also names a strip
that clipped), the hits on their beats, and a pitch or two (an FFT peak of a stem) where the tune says.

A whole song made this way, rendered and measured, is [`demo/canon/`](../demo/canon/README.md) —
Pachelbel's Canon as 128 bpm EDM in 86 commands (R-SVC-9); `ctest -R solaris_demo_canon` re-makes it.

## 8. Drive the window the user is looking at

The same lines reach a running window. Start it with a control socket, then attach:

| how | use it for |
|---|---|
| `solaris --control /tmp/sol.sock [song.slp]` | the user's window, drivable |
| `solaris-cc attach /tmp/sol.sock --script song.txt` | a script into that window — it prints exactly what `solaris-cc` prints, refusals and exit codes included |
| `solaris-cc attach /tmp/sol.sock clip add --instrument drums --at 0 --length 8 : audit` | a quick line or two; `--follow` keeps printing events after the last line |
| `solaris-cc ntwb install` | the web face for Arstro Remote (the same grammar from a browser; R-SVC-7) |

One script through `solaris-cc` and through a live window gives the same events, output, state, `.slp`
and render — `tests/acceptance/run.sh` proves it (R-SVC-6, DR-SVC-5).
