---
name: arstro.solaris.compose.melody
description: The base of every song made in Solaris — how to write a GOOD melody (a hook, a drop lead, a topline, a vocal-chop line, a piano theme, a riff, a counter-melody) with music theory, and how to learn the user's taste in melody from their verdicts. Use it whenever notes of a melody are about to be written: before the melody of any demo track or song (`apps/solaris/demo/*/make_script.py`), when the user asks for a melody, a hook, "a better / more emotional / catchier melody", melodic ideas, or melodies to listen to and judge, and on `/arstro.solaris.compose.melody` — even when the melody is a small part of a bigger task. It holds the method (a two-bar phrase → form → development → harmony fit → motion as real melodies move → contour → cadence), research on what professionals and 880 professional pop melodies teach (references/research.md, scripts/corpus.py), a generator and analyzer (scripts/melody.py), a renderer through Solaris (scripts/render.py), the Melody Lab audition page on localhost where the user — the approver — rates melodies (scripts/serve.py), and the taste ledger their verdicts build (taste.md, data/). For a SONG melody — a verse, a pre-chorus and a chorus with words (a topline) — it holds the songwriter's method (references/songwriting.md: prosody, the sections' jobs, melodic math, the title as the hook, the money note), hand-written songs (scripts/songbook.py) and their arrangement through Solaris, sung by a voice through the vowel filter (scripts/song.py).
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
4. **Is it a line or a SONG?** An 8-bar line over a loop (a drop lead, a hook, a theme) is §1's method and
   `melody.py`. A song (a verse, a pre-chorus and a chorus, a topline with words) is **`references/songwriting.md`**:
   the words first (prosody), each section doing its job, lines in mirrored pairs, the title as the hook, one money
   note, a cadence home. The approver's words that forced the split: *"these melody quality are still too far to be a
   song"* (after batch-004). An 8-bar exercise cannot become a song by being scored better.

## 1. How a good melody is made — the method

Each step says why; `references/theory.md` has the full theory, the tables and worked examples. Read it the first
time you use this skill in a session, and whenever a step below is not enough.

1. **Write a two-bar IDEA — a phrase, not a cell.** An active bar, then a bar that LANDS on a held note (or holds,
   then picks up into the next phrase). Choose its rhythm first — rhythm is what the ear remembers — and take it from
   how real songs phrase (`data/corpus_stats.json` → `top_two_bars`), not from a grid. Give it one contour (an arch, a
   rise, a fall, a leap then a fall) and say what it is. *batch-001: one-bar motifs, each bar moved on its own, were
   heard as "random"; people sing in phrases.*
2. **Choose a FORM for the role** (8 bars): a *period* (a question, then its answer), a *sentence* (idea, idea moved,
   fragments, cadence), *AABA*, a *hook loop* (two bars four times, the last lifted), *call and response*, a
   *climbing sequence*. The form decides where repetition and change fall.
3. **DEVELOP the idea: repeat it, then change it.** Bring it back EXACTLY where the chords repeat (bars 5–6 over a
   four-chord loop played twice — real pop returns a third of its two-bar units exactly). Elsewhere sequence it (move
   it a step or a third and BEND it to the new chords — a tonal sequence), vary its end, fragment it, invert it. The
   ear needs to recognise and it needs news.
4. **FIT it to the harmony.** Chord tones on most strong beats (beat 1, beat 3, any note a beat or longer — real pop:
   74 %, the root, 3rd and 5th about equally). A 3rd, 5th, 7th or 9th on a downbeat adds colour; the root is fine. Non-chord tones go on weak beats
   (passing and neighbour tones), or on a strong beat ONLY as a leaning note that resolves by step (an appoggiatura or
   a suspension) — that lean is where much of a melody's emotion lives.
5. **MOVE like a real melody** (880 professional pop melodies, `references/research.md`). About half the intervals are
   steps and a fifth are the same note again — a line DWELLS (a plateau pitch, Ewer) before it moves; never three of a
   note in a row. After a step it goes on or turns about equally; after a leap of a 4th or more it turns back (59 %).
   Lines tend to fall (descending steps 1.17× ascending). Every note between the strong ones heads for the next strong
   one. Check the line's **surprise** against real pop (2.94 bits an interval; 2.70–3.17): above it sounds random.
6. **SHAPE the contour.** ONE climax — the highest note, once — reached by a leap up (one or two upward leaps, after
   the midpoint) and left by steps down; a range of about 14 semitones in 8 bars (12–16); the second phrase higher than
   the first.
7. **CADENCE — the ending decides how the whole melody is remembered.** Phrases end on a long note. The first phrase
   asks (2, 5 or 7: a half cadence). The last ANSWERS with a real cadence: V7 → I under bars 7–8 whatever the loop does,
   the tonic ON bar 8's downbeat, held the bar, stepped into from 7 or 2 — no leaning note on the final note, and the
   home chord ringing after it ("the ending is not good", batch-003). A loop hook may end open so it can repeat.
8. **LEGATO, and RESTS where a singer breathes.** Inside a phrase the notes join — a gap under ¾ beat is articulation,
   not a rest (sixteenths with gaps sound choppy and cheap: "not luxury", batch-003). Rest after a held note, or on the
   last beat of a bar before a pickup (where real songs rest most). A rest in the middle of a phrase is "not in natural
   place" (the approver's words, batch-001). For lush styles, put colour (7ths, maj7, add9) in the chords under the
   held notes.
9. **EMOTION is placed, not sprinkled**: long notes at phrase ends and at the climax; a leaning note (appoggiatura) into
   one or two of them — not every one; the ♭6 → 5 sigh where the harmony is borrowed.
10. **Honour the tendency tones**: 7 → 1, 4 → 3, ♭6 → 5, 2 → 1 or 3. Where you break one, mean it.
11. **Check, then let the user hear it** — `melody.py analyze` gives the rubric (§3); the ear decides (§4).

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
| `corpus.py <POP909>/POP909` | learn from professionals: measures the 909 lead melodies of POP909 (MIT; clone it into a scratch folder) and writes `data/corpus_stats.json` — interval habits, what follows a leap, chord tones on strong beats, density, range, exact returns, where rests fall, the common two-bar rhythms, an interval bigram and its surprise. Aggregates only, never a melody. `melody.py` reads it |
| `songbook.py [--out <dir>]` | the songs written by hand (references/songwriting.md): each a verse, a pre-chorus and a chorus with an original lyric, every note its syllable (`*` stressed, `~` a held vowel) and vowel; prints each song's craft (`song_metrics`: range, surprise, stresses on the beat, misstresses, rests, each section's register, the climax, the hook's return, the cadence); `--out` writes a lab folder's batch.json (`songs-NNN`) |
| `song.py <dir> [--measure]` | each song ARRANGED by its sections (the verse thin, the pre-chorus building to a breath, the chorus full and pumping, the last chord held), the lead **sung** (a mono synth through the vowel filter, its vowel automated per syllable) and on a **piano** — two MP3s a song. `--measure` renders the stems and prints every part's loudness per section (the lead must lead) |
| `taste.py [--dry]` | what the verdicts teach: ratings by style, form, rhythm, motif kind, voice; the metrics where liked and disliked melodies differ; writes `data/weights.json` |

The rubric (`melody.py score`) — each part 0…1, each a point of §1, its target from the corpus where it has one:
chord fit (~90 %) and unresolved strong non-chord tones; steps (~50 %); repeated notes (~18 %); leaps recovered;
range (~14); pitch variety; colour on the downbeats; one climax (~60 %) and the rise to it; tendency tones; the
cadence's length; rhythmic variety; syncopation for the style; direction changes; EXPECTEDNESS (surprise ≈ 2.94 bits);
and the phrase — the idea's exact return, its rhythm kept, rests in place, phrase ends held, the climax entered by a
leap and left by a step. **It ranks candidates; it does not judge music — the user does.**

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

**A song's topline is written, not generated** — `references/songwriting.md`, `scripts/songbook.py`. Write it as a
songwriter does: the lyric's stresses first, then lines in mirrored pairs, the title on the chorus's first beat, the
climax once in the chorus's second half, the cadence home. Check it (`songbook.py`), arrange it (`song.py`), and put it
in the lab as `songs-NNN` for the approver. To carry an 8-bar line (`melody.py`) into a song:

- Take the notes from `batch.json` (`t` beats, `d` length, `p` MIDI, `name`) into the song's score — a list of
  (beat, length, note name) as `demo/storytrack/make_script.py` keeps its `THEME`.
- Put the melody on **its own strip** (a melody inside chords cannot be pitch-read) and make it **lead the mix** —
  measure each strip's loudness per section; a plucked or struck voice reads far quieter than a held saw.
- In the song's `measure.py`, check the melody **as written** (the devices: motif, sequence, climax, cadence) and
  **as heard** (YIN pitch on its stem, within 50 cents).

## 6. Failure modes already met

- **Bar-by-bar building** (batch-001): a one-bar motif, each bar transposed on its own, with no phrase — heard as
  "random", "no emotion". Build two-bar phrases and repeat them exactly where the harmony repeats.
- **A line that never dwells**: no repeated notes at all is restless — real melodies repeat a note a quarter of the time.
- **Rests in the middle of a phrase** ("not in natural place") — rest after a held note, or before a pickup.
- **Choppy articulation** ("not luxury"): rhythms copied from MIDI keep sung syllables short — join the notes.
- **A weak ending** ("the ending is not good"): the last note late, short, leaned into, over a chord that isn't home.
- **Surprise as a mere preference**: a rubric that only prefers expectedness will still pick a 3.7-bit line that wins
  elsewhere — it is a gate (3.25 bits).
- A melody with no motif — a well-behaved random walk — sounds aimless however well it scores.
- The motif pasted at one pitch over changing chords: bend it (tonal sequence).
- The tonic forced as the last note over a V chord.
- Hovering: five or fewer pitches in eight bars, or a note repeated four times — boring unless it is the rhythm hook.
- The climax struck twice — it stops being a climax.
- A pass/fail rubric: every candidate scored 1.000 and a monotonous line passed. Preferences must be graded.
- The lead buried under the pad (the piano voice, by 4.5 LU, the first time) — measure, don't assume.
- **An exercise, not a song** ("still too far to be a song", after batch-004): 8 bars over a loop, no words, no
  sections, no title, so nothing to say, nothing building, nothing to come home to. Write a song as a song
  (`references/songwriting.md`).
- **The roll not drawn**: an `<svg>` made with `createElement` is an HTML element, so its `innerHTML` never becomes
  SVG and the lab showed the chord names as plain text. Make it with `createElementNS` and screenshot the page.

## References

- `references/songwriting.md` — a melody into a SONG: prosody, the verse / pre-chorus / chorus jobs, melodic math,
  the title as the hook, the money note and the ending, the arrangement by section; its sources; the songs written.
- `references/research.md` — what professionals, research and 880 professional melodies say, with sources, and what
  the engine took from each; read it with theory.md.
- `references/theory.md` — the theory in full: harmony and the non-chord tones, scale degrees and their pull, motif
  development, phrase forms, contour, rhythm, cadences, genre notes, the rubric's reasons, a worked example.
- `taste.md` — the approver's verdicts and the lessons; read first.
- `data/judgments.jsonl` (every verdict with its melody), `data/weights.json` (what `taste.py` learned).
