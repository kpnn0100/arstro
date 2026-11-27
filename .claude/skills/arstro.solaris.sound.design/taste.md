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
10. **Start from the bare drop.** songs-002 (the first songs on the analog synth): *"still not good, your bass design
   is worst"*, *"currently the kick is not punchy enough"* (2026-11-27). The user then set the drop's shape themselves:
   a trap kick and snare — a READY-MADE kick sample first — and one saw: *"don't do gliding, just straight powerful saw
   play the chord that end when the snare come and play along the kick"*. So: no pitch envelope, no glide, no decay on
   the saw; it strikes with every kick, holds flat, and stops dead at the snare; the bass does the same. Build back up
   from there only on their word. *(scripts/drops.py)*
11. **The kick is a sample, and it is loud.** The user's pack (26 kicks, kept outside the repository); `kick_trap` is
   the punchiest by measure (159 ms body, 14 dB crest). At −13 LUFS its hit stood 4.8 dB over the mix; at −10, 8.4 dB.
12. **The bass is an 808.** drops-001: Drop 2 **love** — *"love the bass of this"* (the 808: a sine with a 5-semitone
   thump in 18 ms, driven 0.55, no glide; under the wider, brighter supersaw). A pure sine and a driven saw bass were
   okay; a hollow square weak; a distorted, clipped sub **no**. *(the 808 design is the default for every drop)*
13. **Fewer kicks; chords that carry a mood.** *"make it less kick, just drop snare at 3 and 7, kick at 1, 4, 6, now play
   some more advanced chord that bring mood"*. Over two bars: kick on 1, 4, 6; snare on 3, 7. Triads are not enough:
   ninths, sus chords that resolve, a borrowed minor iv, a bass that walks under a held chord. *(drops.py --moods)*
9. *My own reading, awaiting a verdict:* the earlier batches sat an octave too high; the registers are lower now.

## Batches

### drops-002 (2026-11-27) — one sound, five moods

The loved design (Drop 2: the wide supersaw, the 808) on the user's sparser pattern, five chord sets in E minor / G major:
1. **Bittersweet** — Cmaj9 · Dadd9 · Em9 · G/B.
2. **Longing** — Em9 · Cmaj7♯11 · Am9 · B7sus4 → B7.
3. **Dreamy** — Gmaj9 · Bm11 · Cmaj9 · Cm6 (the borrowed minor iv).
4. **Lifted** — Em7 · Cadd9 · Gsus2 → G · Dsus4 → D, one top note held.
5. **Falling** — Em(add9) over a bass stepping down E · D♯ · D · C♯ · C · B, to B7.

The third hit of each two bars moves the top note or resolves the chord. Levels as drops-001. *Awaiting verdicts.*

### drops-001 (2026-11-27) — the bare drop: five saws, five basses

One piece of music for all five (E minor, 150 bpm half time, Em · C · G · D, 16 bars; the user's `kick_trap` sample;
a tight snare and clap on 3), so only the saw and the bass differ:
1. **Classic** — the JP-8000 supersaw (7 + 7 an octave up); a pure sine sub.
2. **808** — a wider, brighter supersaw; an 808 (a 5-semitone thump, driven, no glide).
3. **Stack** — three octaves of saw (body, chord, air); a saw over a sine through the ladder, driven 12 dB.
4. **Tight** — five saws close together with a square under them; a hollow square bass over a sine.
5. **Driven** — the supersaw pushed 10 dB into the ladder; a sine driven 20 dB and clipped.

Measured, the same in every option: kick −10, saw −14.5, bass −16, snare −16.5 LUFS alone; the kick's hit 8.3–8.5 dB
over the mix; the saw 31–35 dB down within the gap after the snare (only its short room), the bass silent; true peak
−1.5 dBTP.
counts: love 1 · good 0 · okay 2 · weak 1 · no 1
loved: 2 (808) — "love the bass of this" · okay: 1 (Classic), 3 (Stack) · weak: 4 (Tight) · no: 5 (Driven)
in chat: "all 5 is good but make it less kick …" — the page's marks are the record. lesson: 12, 13.

### songs-002 (2026-11-27) — the first songs on the analog synth

Hold the Sky (E minor, 150 — emotional and wide, Illenium-like) and Wildfire Hearts (D major, 140 — bright, staccato,
Garrix's pop side). Every synth part is the analog synth: a 14-voice free-running supersaw with a lead on its top
note, a sine sub from phase zero, a driven ladder mid bass, a two-saw reese that breathes, a pad, a pluck and lead or
a vocal chop, an arp in the second drop. Levels set by measurement.
verdict (chat): *"still not good, your bass design is worst … currently the kick is not punchy enough"* → lessons
10 and 11, drops-001.

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
