# Afterglow — an original progressive house track, its melody measured (R-SVC-14)

Written for this repo — the progression (IV–I–V–vi in D major, two bars a chord), the melody, the bass line, the drums
and the form; no one else's song — at 126 bpm, 104 bars, 3:18. Made with `solaris-cc` alone, every sound the DSP
library's. The user: *"make me a progressive house song that have really emotional melody."*

| file | what it is |
|---|---|
| `make_script.py` | spells every part and writes `song.txt`: a kick on every beat; claps, sixteenth hats and open off-beats (held back in the verse); a rolling sixteenth bass around the kick and a sine sub in the drops, under the pump; a sixteenth pluck arpeggio through a dotted-eighth delay, opening over the intro; a warm pad; wide supersaw chords; the MELODY on a legato lead (a five-voice saw and an octave, a touch of vibrato) that swells and opens through each build; the physical piano (R-INST-5) — chords, and the melody on a track of its own; a riser and a snare roll; a crash, a ride; a reverb, a delay, a true-peak limiter |
| `song.txt` | **the song** — ~300 `solaris-cc` lines, committed; ends in `project save` and the render with stems |
| `measure.py` | the master and the melody, judged: BS.1770 loudness and true peak; the drops the peak — over the breakdown and the verse; the builds rising, brightening and landing; the kick on every drop beat; the pump; the field; the melody as written (its range, its steps, its climax, its home) and as HEARD — the lead's and the piano's pitch on every written note (YIN), the last drop a whole tone up; the keys |

```bash
cd <a scratch folder>; cp <repo>/apps/solaris/demo/progressive/song.txt .
SOLARIS_SETTINGS=$PWD/s.txt SOLARIS_RECENTS=$PWD/r solaris-cc --script song.txt
python3 <repo>/apps/solaris/demo/progressive/measure.py .   # numpy + scipy
ctest --test-dir build -L long                              # progressive_track_measured: all of it
```

**The melody** is sixteen bars in two phrases over G–D–A–Bm: it rises by step from the third of G, reaches for F♯5
over D and its climax, A5, over A — the second half of each phrase — and the first phrase ASKS (it hangs on E and C♯
over Bm) where the second ANSWERS, home on D. Its range is fifteen semitones, two thirds of its moves a tone or less.

**The form:** intro (bars 1–16: the kick and the pluck opening; the bass and the claps from bar 9), verse (17–32: the
groove held back), breakdown (33–40: the drums gone — the piano's chords and the melody, bare, over the pad), build
(41–48: the lead swelling and opening on the melody's second phrase, the riser, the roll, the kick climbing back), drop
(49–64: everything, the melody on the lead), break (65–72: the piano again), build (73–80), drop (81–96: a whole tone up,
in E major, the melody doubled an octave higher, a ride), outro (97–104).

**Measured (2026-10-21):** −10.7 LUFS integrated, −1.21 dBTP; the drops −8.9 / −8.5 LUFS over a −15.6 breakdown and a
−11.2 verse; each build rising from −16 to −10 LUFS and brightening 21 dB, landing under its drop; the kick on all 128
drop beats within 0.2 ms; the chords pumping 13 dB; the lead on all 54 written notes of the first drop within 50 cents (median 3),
the piano on all 28 of the breakdown's, and the last drop's 54 a whole tone higher; the first drop in D major (97 % of
its pitched peaks), the last in E major (97 %); the sub to the air.

**What making it found:** the first take was flat — the verse within a decibel of the drop, the builds starting as loud
as they ended. The sub moved to the drops, the verse got a softer groove, the pluck sits out the first half of each
build, and the lead swells through the build instead of entering at full. The key check counted a C natural that was the
bass saw's seventh harmonic; it reads the peaks over 250 Hz.
