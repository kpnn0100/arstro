# The approver's taste — read this first

The user judges melodies by ear in the Melody Lab (`scripts/serve.py`) or in chat. Every verdict is in
`data/judgments.jsonl` with the whole melody; `scripts/taste.py` turns them into `data/weights.json`, which the
generator adds to its rubric. This file is the human-readable ledger: what was loved and rejected, the user's own
words, and the LESSONS — newest batch first.

**A lesson needs evidence** — two or three melodies behind it, or the user's explicit words. One verdict is a hint.
When a lesson becomes a rule, the code changes too (`melody.py`), and the entry says which change.

## Standing lessons

1. **A melody must sound MEANT, not random** — built in two-bar PHRASES (an active bar, then a landing on a held note),
   the phrase returning exactly where the chords repeat, every note heading for the next strong note, moving as real
   melodies move (dwelling on a note now and then, turning back after a leap). Evidence: batch-001 — 4 notes in the
   user's words ("too random", "this feel random", "still feel random"), 9 of 16 weak or no, nothing above okay; its
   melodies measured 3.38 bits of surprise an interval against real pop's 2.94. *(engine 3)*
2. **Rests go where a singer breathes** — after a held note or before a pickup, never mid-phrase. Evidence: "the rest is
   not in natural place" (batch-001-02, rated no — its fragmentation left gaps mid-phrase). *(engine 2: rests are
   measured; engine 3: phrase rhythms from real songs)*
3. **Emotion needs held notes and a reach** — long notes at phrase ends and the climax, a leaning note into one or two
   of them, the climax entered by a leap. Evidence: "there is no emotional" (batch-001-01); the four "okay" melodies
   that moved slowly (cinematic, lo-fi). *(engine 2)*
4. **Call and response, not the hook loop** — a two-bar idea answered by its mirror pleases; one two-bar unit four
   times does not. Evidence: call 3.2 over 6 melodies (3.7 over the 3 of engine 3, two of them good); the hook loop
   2.2 over 5 (3 of 3 weak in batch-002). The sentence form 2.0 over 4. *(the presets: no hook loop, no sentence —
   both still made when asked for)*
5. **Dwell a little more, surprise a little less** — the good ones repeat a note about a fifth of the time (0.20
   against 0.08 for the weak and no) and sit under real pop's surprise (2.82 bits against 3.04). *(repeats target 0.20,
   surprise target 2.85 and weighted double)*
7. **"Luxury": legato, and colour under the held notes** — "the melody is not luxury" (batch-003-02, future bass): its
   notes were sixteenths on eighth-note places, a gap after each — choppy, cheap. Real songs' MIDI writes sung
   syllables short; the engine had copied those lengths. A gap under ¾ beat is articulation, not a rest: the note now
   carries to the next. And the lush styles (future bass, progressive house) play 7th, maj7 and add9 chords, so a held
   note can rest on colour. *(engine 4)*
8. **A good ending: a cadence, the home note ON the downbeat, held** — "the ending is not good" (batch-003-15, the one
   loved): its last note leaned G → F onto beat 2 and lasted 1.5 beats over vi, and the audition then played ii. Now
   every answer ends V7 → I, the tonic on bar 8's downbeat held the bar (stepped into from 7 or 2), no leaning note on
   the final note, and the audition's last chord is that I. *(engine 4)*
9. **A melody must be a SONG, not an exercise.** The approver's words, after batch-004: *"these melody quality are
   still too far to be a song … need a break through"*. Four batches of 8-bar lines over a loop rose from 2.38 to 3.31,
   then stopped. A line with no words, no sections and no title has nothing to say, nothing building and nothing to come
   home to. Songs are now written as songs (`references/songwriting.md`, `scripts/songbook.py`): the lyric's stresses on
   the beats, the verse low and spoken, the pre-chorus climbing to a breath, the chorus higher with the title as its
   hook, lines in mirrored pairs, one money note, V → I home. They are arranged by section and heard sung
   (`scripts/song.py`). *(songs-001)*
10. *Hint, not yet a lesson:* **the catchiest hook is short, repeated at once, and has one unusual leap.** Run With Me
   was rated love twice before settling on good: it is the fastest (126 bpm), its title is a one-bar hook sung twice in
   a row, and its climax is a minor-sixth leap. The earworm study found the same three things (Jakubowski et al. 2017:
   faster, a common contour, an unusual interval). One song, so a hint.
6. *Hint, not yet a lesson:* the good ones change direction less (0.32 against 0.45) and use fewer bar rhythms (2.5
   against 2.8) — simpler lines, as the research says. *(the motion and rhythm targets nudged)*

## Batches

### songs-002 (2026-10-22) — the whole song, and a voice that sings

The same three songs, so the change is heard against songs-001, now:
- **in the whole form** (references/songwriting.md §7): an intro teasing the hook, verse, pre-chorus, chorus, a drop
  on the hook (EDM), verse 2 with new words, chorus 2 with a harmony, a bridge that turns the story, the last chorus
  ending on vi, and a tag that lands home;
- **with a voice made to sing** (§8): a late vibrato (songs-001 had none — the mod wheel was never moved), a scoop
  into each phrase, consonants, doubles in every chorus, a harmony from chorus 2.

Measured: every check holds across the whole song — stresses on the beat 93–98 %, the climax once a chorus, no rest
out of place, home on the downbeat held two bars; the voice 3–7 LU over the loudest other part in every sung section; the vibrato
pitch-tracked (a held note: ±41 cents at 5.3 Hz after a steady start). *Judging begun: 01 Paper Lanterns good.*

### songs-001 (2026-10-22) — the breakthrough: three songs, written as songs

After batch-004 the approver asked for a breakthrough rather than more lines. Three original songs were written by
hand, each 20 bars: an intro bar, an 8-bar verse, a 4-bar pre-chorus and an 8-bar chorus, with words. Each is rendered
twice — sung (a voice through the vowel filter, its vowel per syllable) and on a piano — over an arrangement that
builds by section.
- 01 **Paper Lanterns**: future bass, D major, 75 bpm.
- 02 **Slow Sunday**: lo-fi pop, F major, 82 bpm.
- 03 **Run With Me**: progressive house, A major, 126 bpm.

Measured before the user heard them:
- surprise 2.95 / 2.58 / 3.06 bits a note;
- 90–97 % of stressed syllables on the beat, nothing misstressed, no rest out of place;
- each chorus above its verse, its title returning at line 3;
- the climax once, in the chorus, entered by a leap;
- every song ending V(7) → I on the downbeat, held a bar;
- the sung lead 3–6 LU over the loudest other part in every section, its held notes within 50 cents.

counts: love 0 · good 3 (03 was rated love twice, then good) · okay 0 · weak 0 · no 0 (mean 4.0 — the 8-bar lines'
best was 3.31). The user's words: *"it's better now, but need to improve more"*.
lesson: yes — lesson 9 holds: written as songs, every one was good. Hint 10 from Run With Me. What still lacked, by my
own reading: the form was one pass (70 s, a demo), and the voice had no vibrato at all → songs-002.

### batch-004 (2026-10-22) — engine 4: legato, lush chords, real endings — superseded

Judged only in part: 01 (future bass, period) **good**. Then, in chat: *"these melody quality are still too far to be
a song"* (→ lesson 9, songs-001). The rest of the batch stays unjudged; its melodies remain in the lab.

16 melodies, 2 each of the 8 styles (11 period, 4 call, 1 climb), lessons 7–8 applied; and surprise made a GATE (a line
over 3.25 bits is chosen only when nothing in range is left — the first draft of batch-004 chose four at 3.45–3.77,
winning on other parts). Measured: 2.99 bits (14 of 16 inside real pop's middle half), all 16 ending V7 → I on the
downbeat, held; 10 exact returns; no rest mid-phrase; the lush styles on maj7 / m7 / add9 chords.

### batch-003 (2026-10-22) — lessons 4–6 applied

16 melodies, 2 each of the 8 styles, call and response (6) and periods (9) and one climb; the taste learner tempered
(its ranges widened by the liked melodies' own spread, their pull ±0.02 scaled by how many melodies are liked — at
first it would have let four verdicts outvote the theory, and batch-003's first draft rose to 3.26 bits); surprise
learned as a taste metric too and weighted double in the rubric. Measured: 3.0 bits (14 of 16 inside real pop's middle
half), repeats 0.17, 9 exact returns, no rest mid-phrase. It asked: do call and response and a line that dwells carry
into the EDM styles?
counts: love 1 · good 5 · okay 8 · weak 2 · no 0 (mean 3.31 — from 2.88, from 2.38)
loved: 15 lo-fi (period) — "the ending is not good" (→ lesson 8)
good: 03 prog house (call), 04 prog house (period), 05 NCS (period), 11 cinematic (period), 14 kawaii (period)
okay: 01, 02 future bass — 02 "the melody is not luxury" (→ lesson 7); 07, 08 ballad; 09, 10 trap; 12 cinematic (climb);
16 lo-fi (call). weak: 06 NCS (call), 13 kawaii (period)
lesson: yes for prog house and NCS; future bass and trap still only okay. Period caught up with call (3.6 here).

### batch-002 (2026-10-22) — engine 3: phrases, real rhythms, real motion

16 melodies, 2 each of the same 8 styles, made by the rebuilt engine (references/research.md §5): two-bar phrases from
the corpus's own phrase rhythms (14 of 16), motion learned from 880 professional melodies, expectedness in the score.
Measured before the user heard it: 2.98 bits an interval (real pop 2.94; 11 of 16 inside its middle half), 10 of 16
returning exactly, no rest mid-phrase. The lead voice rounder (a square under the saw, the filter lower, vibrato).
counts: love 0 · good 4 · okay 6 · weak 6 · no 0 (mean 2.88 — from 2.38); no notes this time
good: 11 cinematic (period), 14 kawaii (call), 15 lo-fi (period), 16 lo-fi (call)
weak: 05, 06 NCS (hook loop), 13 kawaii (hook loop), 07 ballad (period), 10 trap (period), 12 cinematic (climb)
lesson: "random" is no longer the complaint (nothing rated no); lessons 4–5, hint 6. The lead voice: 2.2 over 16
against the piano's and the pluck's 3.0 — still confounded with style (the lead plays the EDM presets); open.

### batch-001 (2026-10-22) — the first sitting: engine 1, bar by bar

counts: love 0 · good 0 · okay 7 · weak 8 · no 1 (mean 2.38)
okay: 07 pop ballad (period, lyrical, a fall), 11–12 cinematic (period / climb, lyrical), 13–14 kawaii (call / hook,
hook8), 15–16 lo-fi (period / call, offbeat / sparse) — the slower lines and the ones that repeat a unit
weak: 01, 03–06, 08–10 (future bass, prog house, NCS hook, a ballad, melodic trap); no: 02 (future bass, sentence)
the user's words: 01 "the melody is too random, there is no emotional" · 02 "the rest is not in natural place" ·
03 "this feel random" · 04 "still feel random"
lesson: standing lessons 1–3. Also: the bright saw lead averaged 1.9 against the piano's 2.8 and the pluck's 3.0 —
confounded with style (the lead played the busier EDM styles), so not a lesson yet; the lead was made rounder anyway.
changed: engine 2 (phrases, exact returns, held phrase ends, leaning notes capped at two, rests measured), then on the
user's request to learn from professionals, engine 3 (references/research.md): real phrase rhythms, motion and
expectedness from POP909, targets recalibrated. data/weights.json from these verdicts judges ENGINE 1 — read its
style and rhythm weights lightly; they describe melodies the engine no longer makes.

<!-- entry format:
### batch-NNN (date) — what it tried
counts: love n · good n · okay n · weak n · no n
loved: id (style, form, rhythm, motif) — why, in the user's words where they gave them
rejected: id (…) — why
lesson: the general pattern, and its evidence
changed: what moved in melody.py / the presets, if anything
-->
