# Magnetar — an original trap / EDM track, its drop measured (R-SVC-13)

Written for this repo — the progression (i–VI–iv–V in F minor, the major V pulling home), the hook, the 808 line, the
drums and the form; no one else's song — at 150 bpm in half time, 80 bars, 2:08. Made with `solaris-cc` alone, every
sound the DSP library's. The user: *"make me a track that is trap/edm with really dope drop."*

| file | what it is |
|---|---|
| `make_script.py` | spells every part and writes `song.txt`: a short trap kick; an 808 (R-INST-11) re-attacking on the kicks and GLIDING where a note is held into the next, driven through a saturator; a snare and a clap together on the third beat of every bar (half time); hi-hats in eighths with thirty-second and triplet rolls; rims; a crash and a noise impact on each drop; brass-like stabs on the hook (a wavetable's sync table, saturated); a scream an octave up through the second drop's second half; a supersaw under the drops; a dark pad; a glassy pluck arpeggio; a riser; the sidechain, a reverb and a dotted-eighth delay; a true-peak limiter |
| `song.txt` | **the song** — ~270 `solaris-cc` lines, committed; ends in `project save` and the render with stems |
| `measure.py` | the drop, judged: BS.1770 loudness and true peak; the drops over the breakdown; each build rising, brightening and landing; the beat of near-silence before each drop; the snare's half-time grid; the 808 on its roots and gliding through the pitches between its notes; the hats' rolls; the low end mono and the highs wide; the pump; the key; the range |

```bash
cd <a scratch folder>; cp <repo>/apps/solaris/demo/trap/song.txt .
SOLARIS_SETTINGS=$PWD/s.txt SOLARIS_RECENTS=$PWD/r solaris-cc --script song.txt
python3 <repo>/apps/solaris/demo/trap/measure.py .      # numpy + scipy
ctest --test-dir build -L long                          # trap_track_measured: all of it
```

The form: intro (bars 1–8: the pad opening, the pluck from bar 3, light hats from bar 5), build (9–16: one kick a bar
then every beat, the roll from quarters to triplets, the riser, light hats — all cut for the last beat), drop (17–32:
the 808 and the kick, the half-time snare and clap, the rolling hats, the stabs, the supersaw, an impact and a crash),
breakdown (33–48: the pad and the pluck; then the supersaw, a soft clap and the hook on the pluck), build, drop (57–72:
rims, more rolls, an 808 that dives at the turn, the scream from bar 65, a snare fill), outro (73–80).

**Measured (2026-10-21):** −9.7 LUFS integrated, −1.20 dBTP; the drops −7.4 LUFS short-term over a −12.0 breakdown;
each build rising +3.7 / +5.1 LU and 13–14 dB brighter at 5–14 kHz, landing under its drop; the gap before each drop
27.5 / 30.3 dB under the drop's first bar; the snare on the third beat of all 32 drop bars within 0.12 ms and never
on the backbeat; the 808 on every bar's root within 2 cents and gliding through all 28 slides; the hats with 92
thirty-second and 43 triplet gaps among 164 eighths; L/R correlation 1.000 under 120 Hz, side/mid −2.4 dB over 1 kHz;
the supersaw pumping 16 dB; 88 % of the pitched peaks in F minor; the 808's octave the loudest, the air −24 dB.

**What making it found:** the audit called a wavetable with its built-in tables "silent — has no sample" (D-26: it
wants a sound only where an oscillator that sounds reads the `sample` table). The first balance had the 808 carrying
nearly all of the drop's loudness, the snare 21 LU and the hats 31 LU under the mix — measured per stem and rebalanced;
louder transients then needed the limiter's true-peak mode.
