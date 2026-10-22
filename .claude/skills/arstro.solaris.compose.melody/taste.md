# The approver's taste — read this first

The user judges melodies by ear in the Melody Lab (`scripts/serve.py`) or in chat. Every verdict is in
`data/judgments.jsonl` with the whole melody; `scripts/taste.py` turns them into `data/weights.json`, which the
generator adds to its rubric. This file is the human-readable ledger: what was loved and rejected, the user's own
words, and the LESSONS — newest batch first.

**A lesson needs evidence** — two or three melodies behind it, or the user's explicit words. One verdict is a hint.
When a lesson becomes a rule, the code changes too (`melody.py`), and the entry says which change.

## Standing lessons

*None yet — batch-001 is the first sitting.*

## Batches

### batch-001 (2026-10-22) — the first sitting

16 melodies, 2 each of 8 styles (future bass, progressive house, NCS hook, pop ballad, melodic trap, cinematic,
kawaii, lo-fi), forms period / sentence / AABA / hook / call / climb, rendered through Solaris. *Awaiting verdicts.*

<!-- entry format:
### batch-NNN (date) — what it tried
counts: love n · good n · okay n · weak n · no n
loved: id (style, form, rhythm, motif) — why, in the user's words where they gave them
rejected: id (…) — why
lesson: the general pattern, and its evidence
changed: what moved in melody.py / the presets, if anything
-->
