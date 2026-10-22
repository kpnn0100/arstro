# The approver's taste in SOUND — read this first

The user judges sounds in the Sound Lab (`scripts/lab.py`, http://127.0.0.1:8788/) or in chat. Every verdict is in
`data/judgments.jsonl` with the section it judged and its elements' recipes. This file is the human-readable ledger:
what was loved and rejected, which element, the user's own words, and the LESSONS — newest first.

**A lesson needs evidence**: two or three sections behind it, or the user's explicit words. When a lesson becomes a
rule, the recipe changes too (`futurebass.py`, `references/future-bass.md`), and the entry says which.

## The user's brief (2026-10-22) — standing until they change it

- Future bass only, for now.
- Judged by section: a verse, a build-up, a drop, separately.
- **The verse has a reese bass, an airy bell and a main synth.**
- **The drop has a supersaw, a bass, the kick and the bassline in sync, and a lot of percussion filling between.**
- Learned from professionals first ("Trap Nation"): references/research.md.

## Standing lessons

1. **Soft, not rough, in the verse.** The one good verse (fb-002 Glass Garden) was the softest design: a vocal-chop
   main synth, a gentle reese (±7 ct, 600 Hz), a glassy bell. The words on another: *"synth is too rough, need to have
   more soft synth"* (Embers). *(the main synth softened: the lead's low-pass 1.7 kHz, a triangle under the saws, a 35 ms
   attack; the pluck's 2.6 kHz; the reese ±8 ct; Embers' reese drive 14 → 8 dB)*
2. **The verse's reese is deep and ambient.** *"reese bass is not deep enough, it need more ambient"* (Afterlight).
   *(an octave lower, A♯1–A2; the sine under it louder, 0.85; a chorus and a 3.8 s room above 300 Hz, 150 % wide on
   top, the floor dry and centred)*
3. **The bell is high-cut and deep.** *"bell need to high cut a little bit and more depth"*. *(a high cut at 6.5 kHz,
   a 6.5 s room at 0.55, a longer echo)*
4. **The supersaw is SHARP.** *"super saw need to be more sharp"* (Embers), *"same problem"* (Neon Tide). *(open to
   14 kHz; +5 dB of bite at 3.5 kHz; +4 dB of air at 8 kHz; −4 dB of body at 300 Hz; more of the octave-up stack; a
   drier room. Measured: its spectral centre 1.9–2.1 → 2.7–2.9 kHz, its top +2 dB)*
5. **The kick punches; the bass is hard.** *"kick is not punchy enough, need some hard bass"* (Glass Garden). *(a click
   layer an octave up, 70 ms; a +70 % transient; tube density; +3 dB. Measured: each hit 3–6 → 7–10 dB over the mix.
   The mid bass driven 18 dB and open to 2.6 kHz)*
6. **The drop's melody needs rests, and a counter-melody.** *"need some rest note and counter melody"* (Afterlight).
   *(the supersaw leaves the last two beats of every second bar to a vocal chop answering it, on chord tones)*
7. *Hint, not yet a lesson:* the vocal chop is liked. It was the main synth of the one good verse; it is now the drops'
   counter-melody too.

## Batches

### fb-003 (2026-10-22) — the same four tracks, the lessons applied

The same four tracks, so the change is heard against fb-002, with lessons 1–6:
- the verses softer, their reese deep and ambient, their bell high-cut and deep;
- the drops with a sharp supersaw, a punchy layered kick, a hard mid bass, rests in the melody and a vocal-chop
  counter-melody.

Measured:
- supersaw centre 2.7–2.9 kHz (was 1.9–2.1);
- kick hits 7.3–10.3 dB over the mix (was 3.3–5.8);
- the kick ducks the sub 9.7–11.7 dB;
- the counter never collides with the supersaw;
- the drops' melodies at 2.96–3.27 bits.

*Awaiting verdicts.*

### fb-002 (2026-10-22) — four tracks, four designs: a verse and a build-up → drop each

The user: *"make me 4 verse, 4 build up + drop all in future bass, i will judge, don't continue on current melody"*.
Four original tracks with new melodies, each its own sound design, so the verdicts say which way to go:
1 Afterlight (bright, Illenium), 2 Glass Garden (dreamy, San Holo), 3 Embers (dark, Said The Sky), 4 Neon Tide (quirky,
Flume). references/future-bass.md has the design table. The build-up runs into its drop in one clip, so the landing is
judged with the drop.

Measured: the melodies fit their chords (79–88 % of strong notes) and stay under the melody skill's surprise gate
(3.10–3.27 bits); every element's loudness is set to the same order; the kick ducks the sub 9.0–11.1 dB.
counts: love 0 · good 1 · okay 6 · weak 1 · no 0 (mean 2.88)
good: verse 2 Glass Garden (the vocal-chop main synth, the gentle reese, the glassy bell)
weak: verse 1 Afterlight — "reese bass is not deep enough, it need more ambient, bell need to high cut a little bit and
more depth"
okay: verse 3 Embers — "synth is too rough, need to have more soft synth"; verse 4 Neon Tide; drop 1 Afterlight — "need
some rest note and counter melody"; drop 2 Glass Garden — "kick is not punchy enough, need some hard bass"; drop 3
Embers — "super saw need to be more sharp"; drop 4 Neon Tide — "same problem, supersaw need more sharp"
lesson: standing lessons 1–6, hint 7. changed: every recipe they name → fb-003.

### fb-001 (2026-10-22) — the first verse, build-up and drop

- **Material**: Paper Lanterns (rated good as a song) at 150 bpm half time, D major.
- **verse**: the reese (two saws ±12 ct, a sine under, a phaser), the airy bell (FM 3.5 ratio, echo, room), the main
  synth (a pluck plus a saw lead with a late vibrato), a soft kick on 1 and a snap on 3.
- **build-up**: quickening supersaw stabs as the filter opens, the kick and snare rolling (the snare climbing an
  octave), noise and pitch risers, the breath, one clap, the landing.
- **drop**: the supersaw carrying the melody (a pitch rise, a 1/16 tremolo, pumped); a sub and a mid bass struck with
  every kick and ducked by it (9.4 dB, back within 200 ms); the snare and clap on 3; hats, rolls, snaps, toms, a
  shaker, a ride, crashes.

Every element is measured (references/future-bass.md, the last table). Unjudged: the user asked for four new
tracks instead (fb-002), not on Paper Lanterns.

<!-- entry format:
### fb-NNN (date) — what it tried
counts: love n · good n · okay n · weak n · no n
loved: section — which element, why, in the user's words
rejected: section — which element, why
lesson: the general pattern, and its evidence
changed: which recipe moved
-->
