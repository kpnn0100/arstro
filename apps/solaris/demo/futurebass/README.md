# Halation — an original melodic trap / future bass track in the Trap Nation style, measured (R-SVC-15)

The style, nobody's song: written for this repo — the progression (VI–III–VII–i in C♯ minor, a bar a chord), the
vocal-chop melody and its syllables, the 808 line, the drums and the form — at 140 bpm in half time, 80 bars, 2:17. Made
with `solaris-cc` alone, every sound the DSP library's. The user: *"make me a song in trap nation style and a song in NCS
style, feel free to develope more vst3, instrument, effect to complete the goal."* Two effects were built for it in the
DSP library first: the shaper (R-FX-18) and the vowel filter (R-FX-19).

| file | what it is |
|---|---|
| `make_script.py` | spells every part and writes `song.txt`: future bass CHORDS (two five-voice saws, wide) held a bar each from the builds on, chopped on the song's grid by six shapers — a gate a quarter, an eighth, a sixteenth, a thirty-second through each build, a triplet-eighth wobble in the drops (a choice cannot be automated, so each rate is its own shaper whose depth is) — swelling through the builds and diving an octave on the pitch wheel out of the first drop; the CHOPS — one bright legato voice through the vowel filter, the melody, each note's syllable ("ah", "oh", "ee", "oo") a held automation placed under every clip of it; an 808 that glides; a half-time snare and clap; hats rolling in triplets; a piano for the intro, the verse and the breakdown; a pad; a pluck in the second drop; a riser, a roll, an impact, a crash; the pump, a long reverb, a dotted-eighth delay; a true-peak limiter |
| `song.txt` | **the song** — ~410 `solaris-cc` lines, committed; ends in `project save`, the render with stems, and a piece of the first drop rendered from beat 97.5 |
| `measure.py` | the master and the style, judged: BS.1770 loudness and true peak; the drops over the breakdown and the verse; the builds rising, brightening and landing; the gap; half time; the 808's roots and glides; the hats' rolls; the CHOP — the chords' loudness moving at each section's rate and, folded on the song's grid, matching the shaper's own curve, also in the render that starts mid-drop; the VOICE — the chops saying their written vowels and singing their written notes; the field; the pump; the key; the range |

```bash
cd <a scratch folder>; cp <repo>/apps/solaris/demo/futurebass/song.txt .
SOLARIS_SETTINGS=$PWD/s.txt SOLARIS_RECENTS=$PWD/r solaris-cc --script song.txt
python3 <repo>/apps/solaris/demo/futurebass/measure.py .   # numpy + scipy
ctest --test-dir build -L long                             # futurebass_track_measured: all of it
```

**The melody** is eight bars of chops in two phrases over A–E–B–C♯m. The first ASKS — it leans on the sixth and lands
on the tonic held as "oo"; the second climbs to G♯5, the major seventh over A, and answers home on C♯5, "ah". Sixteenth
stutters on one pitch are the chops. The syllables change as a cut (an 8 ms glide), as a chopped sample does.

**The form:** intro (bars 1–8: the piano opening through a filter, the pad), verse (9–16: the chops soft, a clap, light
hats, the 808 on the roots), build (17–24: the chords swelling in, their gate speeding up, the riser, the roll, the
kick climbing — and a beat of near-silence), drop (25–40: the chords wobbling, the chops full, the 808 gliding, the
rolling hats; the chords dive out of it), breakdown (41–48: the piano, the pad, the chops), build (49–56), drop (57–72: a
pluck over it, the wobble a sixteenth in its second half, a snare fill, the 808 diving at the last turn), outro (73–80).

**Measured (2026-10-21):** −9.6 LUFS integrated, −1.21 dBTP; the drops −7.7 LUFS over a −12.4 breakdown and a −11.2
verse; each build rising from −13.8 to −9.1 LUFS and 12 dB brighter, landing under its drop; the gap before each drop
18.6 / 19.4 dB under its first bar; the snare on the third beat of all 32 drop bars within 0.1 ms and never on the
backbeat; the 808 on every bar's root within 1 cent and gliding through all 28 slides; the hats with 91 triplet and 26
thirty-second gaps among 182 eighths; the chords' chop at its rate in all 12 stretches (2.33, 4.67, 9.33, 18.67 Hz
through each build; 7.00 and 9.33 in the drops), and folded on the song's grid matching the shaper's own curve at
r = 0.99–1.00 (a render from beat 97.5: r = 1.00 on the song's grid, −0.91 counted from its own first sample); the
chops' "ah" notes' first-formant band 9.6 dB over their second's and the "ee" notes' 2.6 under — 104 of 108 drop notes
on their written side, and at each pitch sung both ways the "ah" 6.3–29 dB over the "ee"; 76 of the 78 notes they hold
within 50 cents (median 0); the low end mono, the highs wide (side −7.1 dB); the chords pumping 14 dB; 95 % of the drops'
pitched peaks in C♯ minor.

**What making it found:** a shaper's `rate` is a choice, and a choice cannot be automated — so the build's speeding gate
is four shapers, each switched on by its depth. The first vowel reading put a third of the notes on the wrong side: a
two-voice unison beat its harmonics in and out of the narrow formants (one voice, as a sung chop is), and the vowel's
line runs A E I O U, so a 25 ms glide from "oh" to "ah" said "ee" on the way (an 8 ms cut). A fixed-band verdict still
mixes the source with the filter on a high note's sparse harmonics, so the measure also compares notes of the same
pitch. The first balance had the 808 carrying the drop, the chords 12 LU and the chops 20 LU under it — measured per
strip and rebalanced; the chords' release and the riser's reverb were then ringing into the gap.
