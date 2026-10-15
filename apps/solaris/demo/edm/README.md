# Perihelion — an original EDM track, measured as a master (R-SVC-12)

Written for this repo — the progression (i–VI–III–VII in A minor), the hook, the bass lines, the drums and
the form; no one else's song — at 126 bpm, 72 bars, 2:17. Made with `solaris-cc` alone, every sound the DSP
library's. The user: *"please make an edm song, edm require more advance sound, so it can be a test for you."*

| file | what it is |
|---|---|
| `make_script.py` | spells every part and writes `song.txt`: the kick (tuned near A), claps, house hats, crash and ride, a snare roll that speeds up; a sine sub and an off-beat saw bass; wide supersaw chords (two five-voice saws, REQ-synth2-6); the hook on a bright saw lead; a pad opening through the intro; the physical piano in the breakdown; a noise-and-saw riser climbing in pitch, filter and level through each build; the sidechain, a reverb and a dotted-eighth delay; a limiter |
| `song.txt` | **the song** — ~215 `solaris-cc` lines, committed; ends in `project save` and the render with stems |
| `measure.py` | the master, judged: BS.1770 loudness (integrated, per section), true peak, the drops over the breakdown, the builds rising and landing, low end mono and highs wide, the pump in dB, the kick on every beat, the key, the range |

```bash
cd <a scratch folder>; cp <repo>/apps/solaris/demo/edm/song.txt .
SOLARIS_SETTINGS=$PWD/s.txt SOLARIS_RECENTS=$PWD/r solaris-cc --script song.txt
python3 <repo>/apps/solaris/demo/edm/measure.py .       # numpy + scipy
ctest --test-dir build -L long                           # edm_track_measured: all of it
```

The form: intro (bars 1–8: kick, light hats, the pad opening), build (9–16: the kick one a bar then every beat
then out, the roll from quarters to thirty-seconds, the riser), drop (17–32: everything, a crash on each
phrase), breakdown (33–40: the piano's broken chords, then the hook in its right hand, over the pad), build,
drop (49–64, with a ride), outro (65–72).

**Measured (2026-10-15):** −10.3 LUFS integrated, −1.03 dBTP; drops −8.8 LUFS over a −13.6 breakdown; each build
+3.1 LU and +15 dB brighter at 5–14 kHz, landing under its drop; L/R correlation 1.000 under 120 Hz, side/mid
−1.7 dB over 1 kHz; the chords pumping 10.3 dB; the kick on all 128 drop beats within 0.19 ms; 98 % of the pitched
peaks in A minor; the sub to the air in the drop.

**What making it found, and the library gained:** the Basic Synth's unison was mono and its attack flammed
(stereo unison, REQ-synth2-6); the kit had no cymbal (crash and ride, REQ-drum-6); a synth's volume did not move
a sounding note, so the riser could not swell (D-8, REQ-synth2-4 amended).
