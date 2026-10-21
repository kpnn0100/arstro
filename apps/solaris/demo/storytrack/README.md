# Porch Light — an original future bass track that tells a story, measured (R-SVC-17)

In the style of San Holo and Illenium — nobody's song: the story, the progressions, her theme and every part were written
for this repo. 150 bpm half time, 96 bars, 2:34; E♭ major, then G. Made with `solaris-cc` alone, every sound the DSP
library's — its guitar built for this track (R-INST-12). The user: *"make me another future bass banger, that sound good,
apply style from san holo, illenium and use advance music theory for better melody, translate a real emotional, story
into a music."*

## The story

After her mother died she drove back to the house to pack it up. The porch light was on — her mother always left it on
for her. Inside were the rooms, a voicemail she had never deleted, the grief, a photograph. When she left, she left the
light on.

| scene | bars | what tells it |
|---|---|---|
| **the key in the door** | 1–8 | the guitar alone, picking her chords; a pad comes in |
| **her rooms** | 9–24 | HER THEME — the leitmotif — stated on the guitar, twice, over the piano |
| **the voicemail** | 25–32 | a DOMINANT PEDAL (B♭ held under E♭, Cm, A♭, B♭); her voice — a synth through the vowel filter — calling the motif's first notes, each call a third higher |
| **grief** | 33–48 | the theme on a wide lead over chords BORROWED from the minor — A♭m6 (iv) and C♭maj7 (♭VI): the loss is in their C♭ and G♭; the chords pump and SAG two semitones at the end of each phrase (San Holo's wonk); a growl TALKS at each phrase's end |
| **the photograph** | 49–64 | the theme AUGMENTED, every note twice as long, on a bare piano; the guitar answers with it INVERTED; the dominant seventh resolves — to ♭VI, a DECEPTIVE cadence: she is not there |
| **the porch light** | 65–72 | that ♭VI (C♭ = B) becomes the dominant of E minor and the door to G; the motif CLIMBS, a step in each chord, to D6 |
| **carrying her** | 73–88 | a MAJOR THIRD up: her theme on the guitar in G, its borrowed chords now diatonic (ii and IV), its sigh gone, its climax a third higher; a glassy sparkle; no growl |
| **leaving the light on** | 89–96 | the guitar plays the motif once more over a minor PLAGAL close — G, C, **Cm** (her shadow, borrowed), G — and ends on the major third |

## Her theme — the melody, and why it moves as it does

Eight bars over E♭ – B♭/D – Cm7 – A♭maj7 – **A♭m6** – E♭/G – **C♭maj7** – B♭sus4→B♭ (the bass falling by step):

- **the motif** (bar 1): G A♭ B♭ E♭ — degrees 3-4-5-8, a dotted rhythm — a reach upward;
- **a sequence** (bar 3): the same two steps begun a third lower, an octave up (E♭ F G), turned down at its end — going
  up would pass the climax, which belongs to bar 7;
- **an appoggiatura** (bar 4): B♭ leaning on the downbeat over A♭maj7, resolving up a step to C;
- **the sigh** (bar 5): C♭ — the ♭6, outside the key, the borrowed iv's — falling a semitone to B♭: the lament's figure;
- **the climax** (bar 7): B♭5, once, the highest note, over the borrowed ♭VI — its major seventh;
- **a 4–3 suspension** (bar 8): E♭ held over B♭sus4 falls to D as the chord resolves — and the theme ends there, on the
  leading tone over V: a half cadence, a question the song answers only in its last bars.

Its transformations carry the story: **fragmented** in her voice (the voicemail), **augmented** in the photograph,
**inverted** — every step mirrored about B♭ in the scale — in her daughter's answer, **transposed** a major third up when
she carries it, its sigh and its borrowed chords gone.

| file | what it is |
|---|---|
| `make_script.py` | the story in code: the progressions, the theme and its transformations (`augment`, `invert` — diatonic — and the transposition) computed from one score, every part, and `song.txt` |
| `song.txt` | **the song** — ~370 `solaris-cc` lines, committed; ends in `project save` and the render with stems |
| `measure.py` | the master and the story, judged — 40 checks (below) |

```bash
cd <a scratch folder>; cp <repo>/apps/solaris/demo/storytrack/song.txt .
SOLARIS_SETTINGS=$PWD/s.txt SOLARIS_RECENTS=$PWD/r solaris-cc --script song.txt
python3 <repo>/apps/solaris/demo/storytrack/measure.py .   # numpy + scipy
ctest --test-dir build -L long                             # story_track_measured: all of it
```

**Measured (2026-10-21):** −9.4 LUFS integrated, −1.22 dBTP. THE ARC: the scenes at −14.0 · −10.8 · −8.6 · −7.7 · −14.6
· −10.5 · −7.4 · −17.3 LUFS (the drops 4 LU over the photograph and 3 over her rooms); each build rising 3.0 / 5.3 LU, 21–22 dB
brighter, landing; the gap 17.9 / 15.8 dB; the second drop's air 1.3 dB over the first's — light after grief; the snare
on the third beat of all 32 drop bars. THE THEME AS WRITTEN: all nine devices checked from the score. AS HEARD (YIN):
the guitar in her rooms 53 of 54 notes within 50 cents, the lead in the grief 54 of 54, the piano augmented 14 of 14,
the guitar inverted 14 of 14, the guitar in G 54 of 54. THE HARMONY: the borrowed C♭ and G♭ 35 % and 53 % of their
bars' peaks and absent from the other 12; the second drop under 5 % outside G major in every bar (only the saws' own
overtones); the keys E♭ major (88 %) and G major (99 %), a major third apart; the deceptive root C♭ at 61.8 Hz where E♭
would be 77.8; the outro's E♭ 31 % under the borrowed iv, faded to 0.7 % under the final G, its B 34 %. THE WONK: −2.00
semitones at all eight sags. THE GROWL: its formants cycling at 5.03 Hz, a syllable a sixteenth, swinging 18–20 dB. The
low end mono, the highs wide.

**What making it found:** a melody inside chords cannot be read — the photograph's right hand got a piano of its own; a
chroma of every FFT bin hears a detuned unison's skirts as wrong notes (peaks only), and a saw's overtones — F♯'s third,
A's fifth — are not out of key; a growl's vowels must alternate open and shut to talk; the first balance buried the
guitar 17 LU under the chords — a plucked string reads far quieter than a held saw — and the pad covered the photograph.
Mutants on the music, each seen red: no borrowed chords, no deceptive cadence, the wonk flat, the growl silent, no
inversion, the second drop a whole tone up instead of a third.
