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

## Batches

### batch-002 (2026-10-22) — engine 3: phrases, real rhythms, real motion

16 melodies, 2 each of the same 8 styles, made by the rebuilt engine (references/research.md §5): two-bar phrases from
the corpus's own phrase rhythms (14 of 16), motion learned from 880 professional melodies, expectedness in the score.
Measured before the user heard it: 2.98 bits an interval (real pop 2.94; 11 of 16 inside its middle half), 10 of 16
returning exactly, no rest mid-phrase. The lead voice rounder (a square under the saw, the filter lower, vibrato).
*Awaiting verdicts.* The question it asks: is "random" gone — and which styles now sound GOOD?

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
