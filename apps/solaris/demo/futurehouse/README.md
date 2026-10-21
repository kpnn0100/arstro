# Starward — an original electro / future house track in the NCS style, measured (R-SVC-16)

The style, nobody's song: written for this repo — the progression (vi–IV–I–V in G major, a bar a chord), the hook, the
bass line, the drums and the form — at 128 bpm, 80 bars, 2:30. Made with `solaris-cc` alone, every sound the DSP
library's. The user: *"make me a song in trap nation style and a song in NCS style, feel free to develope more vst3,
instrument, effect to complete the goal."* Its pump is the shaper built for this request (R-FX-18).

| file | what it is |
|---|---|
| `make_script.py` | spells every part and writes `song.txt`: a kick on every beat (a kick roll through each build); a clap with a snare under it on two and four; open hats on the off-beats, closed sixteenths around them; an FM BASS (algorithm 5: a sine and its octave-down sine, each struck bright by a fast modulator) on the off-beats between the kicks, jumping octaves — the bounce; chords (two five-voice saws) and a pad PUMPING on the shaper's pump shape a beat long, on the song's grid — the sidechain sound with no compressor and no key; the HOOK on a bright lead through the drops and on a pluck in the intro, the breakdown and the second build (an octave over the lead in the second drop); a piano for the verse and the breakdown; a riser, a snare roll, an impact, a crash; a reverb, a dotted-eighth delay; a true-peak limiter |
| `song.txt` | **the song** — ~260 `solaris-cc` lines, committed; ends in `project save` and the render with stems |
| `measure.py` | the master and the style, judged: BS.1770 loudness and true peak; the drops over the breakdown and the verse; the builds rising, brightening and landing; the gap; the FLOOR — the kick on every drop beat, the clap on two and four; the BOUNCE — the bass's loudness folded on the beat; the PUMP — the chords' loudness folded on the song's beat against the shaper's own curve; the HOOK as written and as heard; the field; the key; the SHINE |

```bash
cd <a scratch folder>; cp <repo>/apps/solaris/demo/futurehouse/song.txt .
SOLARIS_SETTINGS=$PWD/s.txt SOLARIS_RECENTS=$PWD/r solaris-cc --script song.txt
python3 <repo>/apps/solaris/demo/futurehouse/measure.py .   # numpy + scipy
ctest --test-dir build -L long                              # futurehouse_track_measured: all of it
```

**The hook** is eight bars in ONE rhythm — four eighths, two dotted eighths, an eighth — over Em–C–G–D. Its two-bar
motif (B4 B4 D5 B4 · E5 D5 B4, then up a third over C) returns at bars 5–6; bar 4 ASKS, climbing to A5 over D, and bar 8
ANSWERS, falling to B4, the fifth of the Em it loops back to.

**The form:** intro (bars 1–8: the pad pumping; the kick, the hats and the hook on a filtered pluck from bar 5), verse
(9–16: the chords pumping, the bass on the off-beats, a clap, the piano), build (17–24: the chords swelling and opening,
the kick from quarters to a sixteenth roll, the snare roll, the riser — and a beat of near-silence), drop (25–40: the
hook on the lead, the bass bouncing, the floor), breakdown (41–48: the piano, the pad, the hook on the pluck), build
(49–56: the pluck opening), drop (57–72: the pluck an octave over the lead, two more sixteenths in the bass), outro
(73–80).

**Measured (2026-10-21):** −8.7 LUFS integrated, −1.17 dBTP; the drops −7.2 / −7.0 LUFS over a −11.2 breakdown and a
−10.4 verse; each build rising 3.6–5.3 LU and 17–23 dB brighter, landing under its drop; the gap 18.8 / 16.9 dB; the kick
on all 128 drop beats within 0.12 ms; the clap on two and four of all 32 drop bars, never on one or three; the bass,
folded on the beat, loudest three quarters of the way through it and 38.5 dB quieter where the kick is; the chords,
folded on the song's beat, the shaper's pump at r = 1.00 in both drops, swinging 11 dB — with no compressor; the hook in
one rhythm in all 8 bars, its motif returning; the lead on all 224 of the drops' hook notes within 50 cents (median 3);
the low end mono, the highs wide (side −3.0 dB); 97 % of the drops' pitched peaks in G major; the air (10–16 kHz) 12 dB
under the loudest octave.

**What making it found:** the first take's builds ended louder than the drops they led to (a kick roll and the chords at
full) and started too loud to rise; the verse sat within a decibel and a half of the drop. The builds now start lower
and lose their hats, and the verse's kick, bass and chords sit back. The first balance had the kick setting every peak
(−14.7 LUFS, 0 dBFS before the master) and the bass 7 LU under the chords — rebalanced per strip, the limiter's release
halved. The clap check first counted a perfect 0.0 ms onset as a miss (a zero is false in Python).
