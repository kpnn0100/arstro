# From a melody to a song — the songwriter's method

> Read this when the melody must be a SONG, not an 8-bar line over a loop: a verse, a pre-chorus and a chorus with
> words. It is how `scripts/songbook.py` was written; `scripts/song.py` makes it heard.

**Why this exists.** After four batches the 8-bar engine reached "okay to good" (mean 3.31, one love). The approver's
verdict after batch-004 was: *"these melody quality are still too far to be a song"*. The engine writes instrumental
exercises over a loop: there are no words, so no speech rhythm; no sections, so nothing builds and arrives; no title, so
no hook to come home to. A song melody is made the other way round, starting from the words and the form.

## 1. The words first: prosody

A sung line is speech made musical, so its rhythm is the rhythm of the words (Pat Pattison, *Writing Better Lyrics*;
the whole tradition of "prosody").

- **Mark the stresses** — `*kept the *light on in the *hall`. A stressed syllable belongs on a strong place: a beat,
  or an **anticipation** (an offbeat held across the next beat — pop's own stress). An unstressed syllable never sits
  on beat 1 or 3 held a beat or more. `song_metrics` counts both (Paper Lanterns: 94 % stressed-on-the-beat, 0
  misstressed).
- **Syllables are eighths and dotted pairs** (`¾ + ¼`, "*ev-ry"): speech rate at 75–126 bpm. Sixteenth runs are for
  instruments.
- **The last stressed word of a line is held**, and usually **anticipated** — it arrives half a beat early and holds
  over the bar line (`hall` at beat 4½ for 2½ beats). That push is the difference between a sung line and a typed one.
- **A pickup starts the line** (beat 4½ or 4 of the bar before): lines begin before the bar and land on it.
- **Breathe at line ends** — a rest after the held word, never inside a line (standing lesson 2).
- **Vowels carry the long notes**: put open vowels (*ah*, *oh*, *ay*) on held notes and the climax. `home`, `way`,
  `go`, `day`, not `with` or `it`.

## 2. The sections: each one does a job

| section | job | register | rhythm | harmony | ends |
|---|---|---|---|---|---|
| **verse** | tells: speech-like, intimate | LOW: a plateau around the 3rd–5th of the key, a range of ~6–8 semitones | the busiest: syllabic eighths, one long note a line | the loop (vi–IV–I–V and the like) | on 2 or 5 over V, a half cadence (it must ask) |
| **pre-chorus** | builds: tension, anticipation | higher than the verse, rising line by line | quicker, more repetition (a short phrase twice, a step higher the second time) | moves off the tonic — IV, V, vi, V | **hanging on V**, on 2 or 5, then a breath (a beat or two of silence before the chorus) |
| **chorus** | arrives: the title, the feeling | higher again: the hook around 5–1 of the upper octave, the **climax here, once** | **longer notes** than the verse, fewer syllables | IV–V–iii–vi (the "royal road"), IV–I–V–vi | the authentic cadence: V(7) → I, the tonic on the downbeat, held |

The sources agree on these jobs: Gary Ewer on verse and chorus melodies (same key, different notes —
https://www.secretsofsongwriting.com/2012/10/19/verse-and-chorus-melodies-same-key-different-notes/) and on the
pre-chorus building anticipation (https://www.secretsofsongwriting.com/2010/09/22/using-a-pre-chorus-to-build-anticipation/);
Tony Conniff, "the art of the pre-chorus" (https://tonyconniff.com/lorde-adele-mj-the-art-of-the-pre-chorus/); Lost
Stories Academy, "why pre-choruses exist" (https://loststoriesacademy.com/blogs-and-tutorials/why-pre-choruses-exist).
Hit Songs Deconstructed found that hit choruses arrive early (~0:40), come back about three times, repeat their title,
and are entered through a lead-in or a pause (https://reports.hitsongsdeconstructed.com/articles/hit-song-choruses/).

**Checked against real songs, honestly.** In POP909, I compared the 4-bar phrases that return exactly with those that
don't. I hoped to find the chorus higher there. The two did not differ in register, note length, density or range
(−0.01 against −0.18 semitones, 0.63 against 0.64 beats a note, 5.3 notes a bar each, 11.6 against 11.5). An exact
return does not pick out a chorus in that data. So "the chorus sits higher, with longer notes" stays a craft claim
from the sources above, not a corpus fact. The approver's ear will judge it.

## 3. Melodic math: lines in pairs, repeated with a change

The Max Martin school (top40theory, "Max Martin's melodic math" —
https://www.top40theory.com/blog/max-martins-melodic-math-repetition-tweaks-in-taylor-swifts-shake-it-off; Music
Business Worldwide — https://musicbusinessworldwide.com/how-max-martins-songwriting-techniques-are-used-to-write-hit-after-hit-after-hit):

- **Lines mirror.** Line 3 has line 1's syllable count and rhythm and nearly its melody; line 4 answers line 2. The
  ear learns a line once and then enjoys it coming back.
- **The change goes at the end.** The mirrored line keeps its start and changes its last note or two, usually higher:
  the verse of Paper Lanterns ends line 1 on F♯ and line 3 on A.
- **The hook returns exactly**: chorus line 3 = chorus line 1 (`hook_returns`). Its ANSWER is what climbs: "car-ry
  me HOME" (A B C♯ E) becomes "light up the WAY" (B C♯ D F♯), the same shape a step higher, which reaches the climax.
- **Repetition inside a section is a feature.** In the pre-chorus, "I can't let go, I can't let go" (the second a step
  higher), "don't look back, don't look back now". The 8-bar engine avoided this; a song needs it.

## 4. The title is the hook

- Put the **title at the start of the chorus** (and again at line 3), on the chorus's most memorable rhythm:
  "PA-per LAN-terns", "slow SUN-day", "RUN with me, RUN with me".
- **A one-bar title** can be sequenced at once ("run with me" 6-5-3, then 5-3-1). A two-bar title wants its answer.
- The title's notes are **chord tones of the chorus's first chord**, or a leaning note onto one. The title must sound
  inevitable, not clever.

## 5. The money note and the ending

- **One climax, in the chorus's second half**: the highest note of the song, **once**, on an open vowel, entered by a
  leap (D5 → F♯5 "way", C♯5 → A5 "run") and left by step or by a turn back (standing lesson 3).
- **After the climax, walk home**: a scale line down (F♯ E D C♯ B A in Paper Lanterns), then **7 → 1 or 2 → 1 onto
  the downbeat of the last bar over I**, held the whole bar (standing lesson 8). The bar before is V or V7.
- The pre-chorus's last note hangs on 2 over V. The chorus's first note resolves it: E5 at the end of the pre-chorus,
  D5 on "PA-". The resolution IS the arrival.

## 6. Harmony that makes the colour

- Lush styles put **7ths, maj7s and 9ths under the held words** (lesson 7): a verse's anticipated last word lands on the
  maj7 of IV or the 9th of I; lo-fi puts the 7th or 9th on the downbeat.
- The **4–3 lean** (a held 4 over vi or V resolving to 3), and the **6 → 5 lean** over V, are where the ache lives. Use
  one or two in a song, not one a bar.
- Change chords at the bar line and keep the cadence's chords plain: V7 → I.

## 7. The whole song, not one pass

songs-001 was one pass: a verse, a pre-chorus and a chorus, 70 seconds long, which is a demo and not a song.
songs-002 strings the parts into the form a record has:

| section | what it is made of | its job |
|---|---|---|
| intro (4) | the chorus's chords, its first four bars played by the hook (chops through an opening filter; lo-fi: keys) | the hook is heard before it is sung |
| verse 1 · pre · chorus | as written | tell, build, arrive |
| drop (8, EDM) | the chorus melody as vocal chops over the full band | the instrumental hook; in future bass and progressive house the drop is the song's second chorus |
| verse 2 | the verse's melody with NEW words (`words=`, the same syllables and stresses) | the story moves on; the ear already knows the tune |
| pre · chorus 2 | as before; a harmony joins | the return, bigger |
| bridge (8) | new chords and a new melody: low and bare at first, climbing, hanging on 2 or 5 over V, a breath | the turn of the story (Paper Lanterns: letting go); the contrast that makes the last chorus land |
| last chorus | the chorus, its last chord changed to vi: a deceptive cadence | the song refuses to end yet |
| tag (4) | the title line once more, over IV–V–I, held | the real ending, 7 → 1 on the downbeat |

A held note running into the next line is shortened to leave a beat to breathe (`flatten`), because a singer needs one.

## 8. A voice that sings

The voice (a synth through the vowel filter) is not a singer, but each of these moves it closer. Each is measured:
- **vibrato that arrives late**: the synth's vibrato sounds only while the mod wheel is up (R-MIDI-3, (Y9) of the DSP
  synth). songs-001 set its depth and never moved the wheel, so it had NO vibrato. Now the wheel is drawn per held note:
  0 for the first ~0.3 s, then rising to 0.55–0.8 of the full ±0.6 semitone. Pitch-tracked, it is steady at first, then
  grows to about ±25–35 cents at 5.3 Hz, a pop singer's vibrato;
- **a scoop into each phrase**: the pitch wheel starts about a semitone low and settles within ~90 ms, on every phrase's
  first note and every stressed reach upward (measured: −96 → 0 cents in 60 ms);
- **consonants**: noise through a high-pass that follows the key, a burst just before each s / sh / f / t / k / p / d /
  b / g / h onset (sibilants ~70 ms and 50 ms early, plosives ~25 ms). It gives the ear the word's edges;
- **doubles and a harmony**: in every chorus two more voices, a few cents apart, left and right; from chorus 2 on, a
  harmony under the line (the nearest chord tone a third to a sixth below on strong notes, the diatonic third below on
  the others) — a backing singer;
- **in tune with itself**: the voice's two oscillators must be in tune. The synth's default 7-cent detune made the
  vowel filter's formants ring 5.5 LU louder and clip (measured).

## 9. The arrangement follows the sections (`song.py`)

The section form must also be HEARD:
- **verse**: thin. Keys and a held bass, a kick and a clap at most, so the words are heard.
- **pre-chorus**: building. The pad comes in with its filter opening, the drums go to quarters, a snare roll rises,
  and then everything stops for a beat or two: the breath.
- **chorus**: full. Wide saw chords pumping on the beat (a shaper), the style's bass rhythm, the full drums, a crash.
- **the lead leads**: `song.py --measure` renders the stems and prints each one's loudness per section. The lead sits
  3–6 LU over the loudest other part in every section.
- **sung**: a mono synth (each syllable sounds again, a 45 ms slide between notes, a little vibrato and breath)
  through the vowel filter, its vowel automated per syllable (AH EH EE OH OO, held). It is not a singer; it is enough to
  hear a line as SUNG, with its words beside it on the page.

## 10. Check, then the ear

`python3 scripts/songbook.py` prints each song's craft:

- range and surprise (bits a note, real pop 2.94);
- strong chord tones and leans;
- stresses on the beat, and misstresses;
- rests out of place;
- each section's mean pitch (the chorus above the verse);
- the climax (once? in the chorus? entered by a leap?);
- the hook's return;
- the cadence (tonic, downbeat, held).

These checks catch mistakes. They do not make a song good; the approver decides that in the lab (`songs-NNN`, judged
like a batch).

## The songs so far (`songbook.py`)

| id | title | style | key / bpm | the idea |
|---|---|---|---|---|
| song-01 | Paper Lanterns | future bass (Illenium / San Holo) | D major, 75 | waiting for someone who isn't coming home; the hook falls 1-7-6-5, the answer climbs to the climax F♯5 "way" |
| song-02 | Slow Sunday | lo-fi pop | F major, 82 | a morning with nowhere to be; 7ths on the downbeats, the title a lazy rising third, the climax G5 "song" over Am7 |
| song-03 | Run With Me | progressive house / NCS | A major, 126 | leaving the city at night; a one-bar title hook sequenced at once, a minor-sixth leap to A5 |

Each runs the whole form (§7) since songs-002 — Paper Lanterns 3:50, Slow Sunday 3:07, Run With Me 2:32 — with verse-2
words, a bridge and a tag of its own:
- **Paper Lanterns**: the bridge lets the lanterns rise and the person go;
- **Slow Sunday**: the bridge asks for the day to last ("stay with me, it's getting late");
- **Run With Me**: the bridge drives into the sunrise ("so run with me and chase the day").
