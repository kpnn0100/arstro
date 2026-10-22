# What professionals and research say about melody — and what this skill took from it

Asked for by the approver after batch-001 (2026-10-22): *"you need to learn on the internet how to make a good melody,
learn and analyze deeply from professional. find skill that can make melody and learn from that"*. Four kinds of
source: the craft (composers and songwriting teachers), the psychology (how listeners expect a melody to go), DATA
(880 professional pop melodies, measured), and other Claude skills that compose.

## 1. The craft

**Ableton, *Making Music* — "Creating Melodies 1: Contour"** (https://makingmusic.ableton.com/creating-melodies-1-contour)
- Balance rise and fall, step and leap: a climb is answered by a fall of about the same size.
- After several steps, a leap the other way works; after a leap, steps the other way (except arpeggiating a chord).
- One peak — the highest note, once — and it tends to sit on a strong beat.
- The opening gesture returns later. Rules are tendencies: good melodies break each now and then.

**Gary Ewer, *The Essential Secrets of Songwriting*** (https://www.secretsofsongwriting.com/?p=1896,
https://www.secretsofsongwriting.com/?p=2115, https://www.secretsofsongwriting.com/?p=9422)
- "Melodies without contour are doomed to be failures." A **plateau pitch** per section — the note the line lives
  around: the 3rd in a verse (relaxed), the 5th in a chorus (energy); higher again in a bridge.
- Start low and build; put the higher notes and **one or two upward leaps after the midpoint** — too many leaps undo it.
- Dangers: a line that is mostly arpeggio (predictable); one note repeated with no high point.
- Repetition — a short idea repeated, exactly or slightly varied, over different chords — is what hits rely on.
- Keep the range within about an octave and a half; simplify the chorus rhythm (longer notes carry emotion); put the
  emotional word on the high note (the octave leap of "Some-*where*").

**Native Instruments, "How to write a melody"** (https://blog.native-instruments.com/how-to-write-a-melody/)
- Melody is the control of tension and release: what the listener expects, then a twist; too familiar is a cliché, too
  new is hard to follow. The tonic, 3rd and 5th anchor a line; the pentatonic is a safe start. Repeat at every level.

**Lost Stories Academy, "The science behind catchy EDM hooks"**
(https://loststoriesacademy.com/blogs-and-tutorials/the-science-behind-catchy-edm-hooks)
- Simple ideas are processed and remembered; resist adding notes. Rhythm gives a melody its personality; pauses are
  as important as notes. Repeat with a change (the rhythm, a harmony, the final note). Decide the emotion before the
  notes. One focal point.

**Production Music Live, "12 tips for writing future bass chords and melodies"**
(https://www.productionmusiclive.com/blogs/news/12-tips-for-writing-future-bass-chords-and-melodies)
- Emotion first; 7th and 9th chords; try one melody over several progressions; audition it on several sounds; hum
  over the progression until a hook comes (Flume's method).

## 2. The psychology — why "random" is heard as random

**Narmour's implication-realization model, Krumhansl's quantification, Schellenberg's two factors**
(https://www.mtosmt.org/issues/mto.95.1.6/mto.95.1.6.royal.php; ISMIR 2004 https://archives.ismir.net/ismir2004/paper/000166.pdf;
Schellenberg 1997, *Music Perception* 14:3)
- Two notes imply a third. Small intervals imply continuing the same way; large ones imply turning back; the line is
  expected to return toward where it came from; intervals are expected to be small (≤ 5 semitones). Schellenberg's
  simplification keeps two factors — **pitch proximity** and **pitch reversal** after a jump — and predicts listeners
  as well.

**Pearce & Wiggins (2006), "Expectation in melody: the influence of context and learning"**
(https://research.gold.ac.uk/id/eprint/1001)
- These expectations are LEARNED statistically from the music one hears; a model trained on melodies (n-grams) predicts
  listeners as well as or better than the rules. → So a melody's naturalness can be measured against real melodies.

**Jakubowski et al. (2017), "Dissecting an earworm"** (https://research.gold.ac.uk/19405)
- Tunes that stick have **common global contours** but **less common interval patterns** between turning points, and
  faster tempi: familiar in shape, surprising in detail.

## 3. The data — 880 professional pop melodies, measured (`scripts/corpus.py` → `data/corpus_stats.json`)

POP909 (Wang et al., ISMIR 2020, MIT — https://github.com/music-x-lab/POP909-Dataset): 909 professionally written pop
songs with the lead melody on its own track, beats and chords annotated. 880 are in 4/4 and measured. Only aggregate
numbers are kept — never a melody.

| measure | professional pop | what batch-001's engine did |
|---|---|---|
| repeated notes | 24 % of intervals | none at all — restless |
| steps / 3rds / 4ths–5ths / larger | 44 / 18 / 11 / 4 % | ~60 % steps |
| descending steps : ascending | 1.17 : 1 (lines tend to fall) | — |
| after a leap (≥ a 4th) | turns back 59 %, repeats 26 %, goes on 16 % | turned back |
| after a step | goes on 43 %, turns 39 %, repeats 17 % | — |
| strong beats on chord tones | 74 % (root 26, 3rd 24, 5th 22 — the root is common) | ~100 %, the root avoided |
| notes a bar | median 5 (4–7) — vocals | 4–5 |
| range in 8 bars | median 14 semitones (12–16) | ~12 |
| two-bar units that return exactly | 32 % | 0–25 % |
| rests begin | most at the 4th beat of a bar (the breath before the next bar) | anywhere — "not in natural place" |
| **surprise** (bits an interval, by the corpus bigram) | **2.94** (middle half 2.70–3.17) | **3.38** — 13 of 16 above the upper quartile |

The last line is the finding: the melodies the approver called "random" were measurably more surprising than real
pop melodies — 0.44 bits an interval more. Within batch-001 the ratings did not follow the bits (all were too high to
tell apart), so expectedness is necessary, not sufficient: it keeps a line from sounding random; the phrase and the
user's ear decide whether it is good.

## 4. Other skills that compose

- **music-composer** (paoloquaranta, Smithery — https://smithery.ai/skills/paoloquaranta/music-composer): a toolkit
  (mido MIDI, scale formulas); its melody advice is "use repetition with variation" and "think call and response"; its
  generator's code is not public (404).
- **music-generation** and **code-to-music** (aiskillstore / cam10001110101 —
  https://skills.sh/aiskillstore/marketplace/music-generation): music21 toolkits that tell the model to "write custom
  code that composes algorithmically"; no melodic craft, no checks, no listener in the loop.
- Audio-model skills (Lyria, MiniMax, ElevenLabs music): render audio from prompts; nothing to learn about melody.

None of them encodes how a melody should move, or learns from a listener. What this skill took: nothing from their
code; the confirmation that **craft + data + the approver's ear** is the part nobody else has.

## 5. What changed in the engine because of this (engine 3)

1. **Phrases, not bars** (batch-001's "random", Ableton's repetition, Ewer's "short idea repeated"): a two-bar idea,
   returning exactly where the chords repeat.
2. **Real phrase rhythms**: the idea's rhythm is one of the 300 most common two-bar rhythms of the corpus that can
   carry an idea (it starts early, lands on a held note, has no hole mid-phrase) — rests where real songs breathe.
3. **Real motion**: the notes between the strong ones move as the corpus moves (after a step: on, turn or stay; after
   a leap: mostly back), pulled toward the next strong note; a pair of repeated notes allowed, never three.
4. **Expectedness in the score**: the corpus bigram's surprise, aimed at real pop's 2.94 bits.
5. **Targets recalibrated** to the corpus: range ~14, ~50 % steps, ~18 % repeats, strong-beat chord tones ~90 % (the
   corpus's 74 % includes its unlabelled extensions), the root allowed on downbeats, the climax around 60 % (a 4-bar
   phrase's high point is as often early as late in real pop).
6. Kept from the craft: one climax reached by a leap and left by steps; leaning notes into long notes (at most two);
   a question then an answer; Ewer's "one or two upward leaps after the midpoint".
