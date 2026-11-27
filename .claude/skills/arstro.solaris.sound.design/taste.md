# The approver's taste in SOUND — read this first

The user judges in the Sound Lab (`scripts/lab.py`, http://127.0.0.1:8788/) or in chat. Every verdict is in
`data/judgments.jsonl`. This file is the ledger a person reads: the user's words, what they loved and rejected, and
the lessons. A lesson needs evidence — two or three verdicts, or the user's explicit words. A lesson that is a rule
changes a patch (`scripts/futurebass.py` and `references/analog-patches.md`, together).

## What the user has asked for (standing)

- **Future bass**, mainstream — "like martin garrix, illenium, san holo" — "with clean sound and energy drop"
  (2026-11-27). Whole songs.
- The verse has **a reese bass, an airy bell and a main synth**; the drop has **a supersaw, a bass, the kick and the
  bassline in sync, and a lot of percussion filling between** (2026-10-22).
- A new instrument must have "good UI UX, physical base (math base) and follow music industrial standard".

## Standing lessons

1. **The instrument comes first.** *"the synth you use is scrap and the sound design quality is nowhere near
   acceptable"* (2026-11-27), after three batches on the Basic Synth never rose above "okay" (fb-002: 1 good, 6 okay,
   1 weak) and a full song on it was rejected. Every synth sound is now the analog synth's.
2. **Soft, not rough, in the verse.** *"synth is too rough, need to have more soft synth"*. The only verse rated good,
   twice, was the softest: a vocal chop singing the line.
3. **The vocal chop is liked** (good in fb-002 and fb-003). It sings the verse and answers the drop.
4. **The reese is deep and ambient.** *"reese bass is not deep enough, it need more ambient"*.
5. **The bell must be pretty.** *"bell need to high cut a little bit and more depth"*, then *"bell is not good"* twice
   after the cut and the longer reverb. The fault was its design (inharmonic, out of tune with itself, an octave too
   high, drowned), not its EQ.
6. **The supersaw is sharp**, *"super saw need to be more sharp"* (twice) — and the sound is **clean**. Both: bright by
   the synth itself (free-running unison, an octave stack, an open ladder), not by boosts.
7. **The kick punches; the bass is hard.** *"kick is not punchy enough, need some hard bass"*.
8. **The drop's melody rests, and something answers it.** *"need some rest note and counter melody"*.
9. *My own reading, awaiting a verdict:* the earlier batches sat an octave too high; the registers are lower now.

## Batches

### songs-002 (2026-11-27) — the first songs on the analog synth

Hold the Sky (E minor, 150 — emotional and wide, Illenium-like) and Wildfire Hearts (D major, 140 — bright, staccato,
Garrix's pop side). Every synth part is the analog synth: a 14-voice free-running supersaw with a lead on its top
note, a sine sub from phase zero, a driven ladder mid bass, a two-saw reese that breathes, a pad, a pluck and lead or
a vocal chop, an arp in the second drop. Levels set by measurement. *Awaiting verdicts.*

### songs-001 (2026-11-27) — five songs on the Basic Synth — rejected

One song (Hold the Sky) was previewed: *"the synth you use is scrap and the sound design quality is nowhere near
acceptable"*. The batch was stopped and the skill removed (lesson 1). Its songs' WRITING lives on in `futurebass.py`.

### fb-003, fb-002, fb-001 (2026-10-22) — 30-second section clips on the Basic Synth

fb-002: good 1 (verse 2 Glass Garden) · okay 6 · weak 1. fb-003: 2 of 8 judged, good 1 · okay 1, both "bell is not
good". fb-001: unjudged. Their words are lessons 2–8.

<!-- entry format:
### songs-NNN (date) — what it tried
counts: love n · good n · okay n · weak n · no n
loved / rejected: which song, which element, in the user's words
lesson: the general pattern, and its evidence
changed: which patch moved
-->
