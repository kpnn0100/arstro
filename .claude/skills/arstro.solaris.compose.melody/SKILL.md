---
name: arstro.solaris.compose.melody
description: The base of every song made in Solaris — how to write a GOOD melody (a hook, a drop lead, a topline, a vocal-chop line, a piano theme, a riff, a counter-melody) with music theory, and how to learn the user's taste in melody from their verdicts. Use it whenever notes of a melody are about to be written: before the melody of any demo track or song (`apps/solaris/demo/*/make_script.py`), when the user asks for a melody, a hook, "a better / more emotional / catchier melody", melodic ideas, or melodies to listen to and judge, and on `/arstro.solaris.compose.melody` — even when the melody is a small part of a bigger task. It holds the method (motif → form → development → harmony fit → contour → cadence), a generator and analyzer (scripts/melody.py), a renderer through Solaris (scripts/render.py), the Melody Lab audition page on localhost where the user — the approver — rates melodies (scripts/serve.py), and the taste ledger their verdicts build (taste.md, data/).
---

# arstro.solaris.compose.melody

> **The melody is the base of everything.** A song is a melody with a reason to exist; harmony, sound design and mix
> serve it. **The user is the approver** — their verdicts outrank every rule in this file. **Read `taste.md` first,
> every time:** it is what they have loved and rejected, and what that taught.

This skill is a craft written down, and a loop that keeps it honest: theory proposes, the user's ear disposes, and
the ledger remembers. When the rubric and the user disagree, the rubric is wrong — change it (§4).

Related: `arstro.solaris.implement` owns making the song around the melody (the demo script, the measure, the
commit). This skill owns the notes of the line itself and the user's taste in them.

---

## 0. Before a single note

1. **Read `taste.md`** — the approver's loves and rejections and the lessons drawn. If it says they find a pattern
   boring, do not write it, however well it scores.
2. **Know the role.** A drop hook is short, rhythmic and repeated; a verse line is speech-like and lower; a breakdown
   theme is slow and bare; a counter-melody moves when the main line holds. §2's table.
3. **Know the harmony first** — key, mode, the chord of every bar, tempo, half time or not. A melody is written
   AGAINST chords; most of what makes one sound "right" is how it meets them.

## 1. How a good melody is made — the method

Each step says why; `references/theory.md` has the full theory, the tables and worked examples. Read it the first
time you use this skill in a session, and whenever a step below is not enough.

1. **Write a MOTIF — one gesture.** One bar (two for a hook): a rhythm cell and a shape in scale steps — a rise, a
   fall, an arch, a turn, or a leap filled by steps. Choose the rhythm first: rhythm is what the ear remembers. Say
   what the motif is ("a rise, 3-4-5-8, dotted"); a line with no nameable motif wanders.
2. **Choose a FORM for the role** (8 bars): a *period* (a question, then its answer), a *sentence* (idea, idea moved,
   fragments, cadence), *AABA*, a *hook loop* (two bars four times, the last lifted), *call and response*, a
   *climbing sequence*. The form decides where repetition and change fall.
3. **DEVELOP the motif: repeat it, then change it.** The ear needs to recognise and it needs news. Sequence it (move
   it by a scale step and BEND it to the new chord — a tonal sequence, never a copy pasted over a chord it clashes
   with), vary its end, fragment it, invert it, augment it. About two thirds of the bars should come from the motif;
   the rest contrast or answer.
4. **FIT it to the harmony.** Chord tones on the strong beats (beat 1, beat 3, any note a beat or longer). Prefer the
   3rd, 5th, 7th or 9th on a downbeat — the root doubles the bass and sounds plain. Non-chord tones go on weak beats
   (passing and neighbour tones), or on a strong beat ONLY as a leaning note that resolves by step (an appoggiatura or
   a suspension) — that lean is where much of a melody's emotion lives.
5. **SHAPE the contour.** About 60 % steps; leaps for feeling, each leap of a 4th or more recovered by a step back the
   other way; ONE climax — the highest note, once — about 70 % of the way through, reached by a rise; a range of about
   an octave (9–16 semitones) for a hook.
6. **CADENCE.** Phrases end on a long note. The first phrase asks (ends on 2, 5 or 7: a half cadence); the second
   answers (ends home — 1, or 3 — when the chord holds it; over a V chord end on its 3rd or 5th, never force the
   tonic). A loop hook may end open so it can repeat.
7. **RHYTHM for the style.** Syncopation (anticipations, 3-3-2) for EDM, trap and pop; straight and long for ballads
   and cinematic lines; rests to breathe — a line that never rests is tiring.
8. **Honour the tendency tones**: 7 → 1, 4 → 3, ♭6 → 5 (the sigh), 2 → 1 or 3. Where you break one, mean it.
9. **Check, then let the user hear it** — `melody.py analyze` gives the rubric (§3); the ear decides (§4).

## 2. By role and style

| role / style | rhythm | form | register (MIDI) | devices that work |
|---|---|---|---|---|
| drop hook (EDM) | syncopated, 3-3-2 or eighths + dotted | hook loop, call and response | 64–83 | a 2-bar hook ×4, the last lifted; octave jumps; open ending to loop |
| future bass lead (Illenium, San Holo) | dotted, tresillo | period, climbing sequence | 63–82 | appoggiaturas, the 9th on the downbeat, a late climax, borrowed iv / ♭VI under the sigh |
| progressive house anthem | lyrical, anticipations | period, AABA | 62–81 | long notes over IV–I–V–vi, the climax in the second half, a sung quality |
| NCS / future house hook | eighths, one rhythm every bar | hook loop | 64–83 | repetition with a small change, bright, chord tones |
| melodic trap | off-beat starts, rests | call and response, sentence | 61–80 | sparse calls, a minor key, answers that fall |
| pop ballad / verse | speech-like, straight | period, sentence, AABA | 60–77 | steps, a question and its answer, appoggiaturas |
| cinematic / breakdown theme | long notes | period, climbing sequence | 60–79 | suspensions, borrowed chords, augmentation |
| kawaii / pluck | sixteenths | hook loop, call | 66–86 | arpeggio-like chord tones, add9 / maj7 colour |
| lo-fi | lazy off-beats, swing | call, AABA | 60–79 | 7ths and 9ths on downbeats, space |

These are starting points. `taste.md` overrides them for this user.

## 3. The tools — `scripts/`

Run from the skill folder (`.claude/skills/arstro.solaris.compose.melody/`). Python 3 and the repo's built
`solaris-cc` (`build/apps/solaris/cli/solaris-cc`) are all they need.

| script | what it does |
|---|---|
| `melody.py generate --out <dir>/batch.json [--styles a,b] [--per-style 2] [--tries 1000] [--seed N]` | makes candidates per style preset — a motif developed through a form, adapted to each bar's chord, fitted, scored — and keeps the best, diverse; reads `data/weights.json` (the taste) automatically |
| `melody.py analyze <melody.json>` / `melody.py styles` | the rubric for one melody; the presets |
| `render.py <dir>` | each melody through Solaris → `<id>.mp3` and its `<id>.txt` (a re-runnable `solaris-cc` script): one bar of chords, the melody over a pad and a bass, light drums for the style; the melody leads the mix (measured: ~7 LU over the chords for each voice) |
| `serve.py [--port 8787]` | **the Melody Lab**: http://127.0.0.1:8787/ — every melody of every batch with a piano roll over its chords, the theory it uses, a playhead, and five verdicts (Love it · Good · Okay · Weak · No) and a note; verdicts go to `data/judgments.jsonl` with the whole melody. Binds 127.0.0.1 only |
| `taste.py [--dry]` | what the verdicts teach: ratings by style, form, rhythm, motif kind, voice; the metrics where liked and disliked melodies differ; writes `data/weights.json` |

The rubric (`melody.py score`) — each part 0…1, each a theory point from §1: chord fit and unresolved strong
non-chord tones; steps (~62 %); repeated notes; leaps recovered, none huge, no tritone; range (~12); pitch variety;
colour on the downbeats; one climax at ~72 %; a rise to it; tendency tones resolved; the cadence's length; rhythmic
variety (~3 bar rhythms); syncopation for the style; direction changes. **It ranks candidates; it does not judge
music — the user does.**

## 4. The audition loop — the user approves

1. **Generate a batch** into the lab: `~/Music/Solaris/melody-lab/batch-NNN/` (the next number), from what the user
   asked for (`--styles`) and steered by their taste (automatic). 12–16 melodies is a good sitting.
2. **Render** it (`render.py`), ~2 s a melody.
3. **Serve** the lab (`serve.py`, in the background) — if it is already running, the new batch is simply in its batch
   menu. Give the user the URL and one line on how to judge.
4. **Wait for their verdicts** — on the page, or in chat ("#3 and #9 are great, #12 is boring"). A chat verdict is a
   verdict: record it through the page's API (`POST /api/verdict` with batch, id, rating 1–5, note) so it is in
   the ledger like the rest.
5. **Learn**: run `taste.py`. Then write the batch's entry in `taste.md` — the counts, what was loved and rejected
   (ids, style, form, motif), the user's own words, and the LESSON in general terms ("they like a late climax reached
   by a sequence; they find hovering lines boring"). A lesson that is a rule becomes code: change the rubric or a
   preset in `melody.py` (and say so in `taste.md`). Do not overfit one verdict — a lesson needs two or three
   melodies behind it, or the user's explicit words.
6. **Next batch** leans into what they loved and tries one or two new ideas — taste is learned at the edges.
7. **Commit** `data/judgments.jsonl`, `data/weights.json`, `taste.md` and any code change: the taste is part of the
   skill and must survive the session (one commit, `git add` named files only, `Co-Authored-By` trailer).

## 5. A melody into a song

- Take the notes from `batch.json` (`t` beats, `d` length, `p` MIDI, `name`) into the song's score — a list of
  (beat, length, note name) as `demo/storytrack/make_script.py` keeps its `THEME`.
- Put the melody on **its own strip** (a melody inside chords cannot be pitch-read) and make it **lead the mix** —
  measure each strip's loudness per section; a plucked or struck voice reads far quieter than a held saw.
- In the song's `measure.py`, check the melody **as written** (the devices: motif, sequence, climax, cadence) and
  **as heard** (YIN pitch on its stem, within 50 cents).

## 6. Failure modes already met

- A melody with no motif — a well-behaved random walk — sounds aimless however well it scores.
- The motif pasted at one pitch over changing chords: bend it (tonal sequence).
- The tonic forced as the last note over a V chord.
- Hovering: five or fewer pitches in eight bars, or a note repeated four times — boring unless it is the rhythm hook.
- The climax struck twice — it stops being a climax.
- A pass/fail rubric: every candidate scored 1.000 and a monotonous line passed. Preferences must be graded.
- The lead buried under the pad (the piano voice, by 4.5 LU, the first time) — measure, don't assume.

## References

- `references/theory.md` — the theory in full: harmony and the non-chord tones, scale degrees and their pull, motif
  development, phrase forms, contour, rhythm, cadences, genre notes, the rubric's reasons, a worked example.
- `taste.md` — the approver's verdicts and the lessons; read first.
- `data/judgments.jsonl` (every verdict with its melody), `data/weights.json` (what `taste.py` learned).
