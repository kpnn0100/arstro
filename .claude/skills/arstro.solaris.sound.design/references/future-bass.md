# Future bass — the recipes, in Solaris's own parameters

The patches live in `scripts/futurebass.py` (`REESE`, `BELL`, `PLUCK`, `LEAD`, `SAW`, `SUB`, `MID`, `NOISE` and each
section's drum kits). This file says what each number does and why. When a verdict changes a number, change both.

Half time throughout: the kick on 1, the snare on 3. Since fb-002 every TRACK (`TRACKS` in futurebass.py) is its own
music: original melodies (verse lines, the build's, and the drop's call, response and last response), its key and
tempo, and its DESIGN, a set of overrides on the base recipes below. fb-001 used Paper Lanterns at 150 bpm (D major);
the user then asked for new material ("don't continue on current melody").

## The four designs (fb-002)

| track | feel | key · bpm | main synth | bell | supersaw (drop) | bass | percussion · kick (bar A / bar B) |
|---|---|---|---|---|---|---|---|
| 1 Afterlight | bright, emotional (Illenium) | F major · 150 | pluck (harmonics) + saw lead | FM 3.5 (the base) | low-pass 6.5 kHz, a 1-st rise into each stab (70 ms), 1/16 square tremolo 0.45 | the base; kick F1 43.7 Hz | full · 1, 2½, 3¾ / 1, 1¾, 3½, 4½ |
| 2 Glass Garden | dreamy, wavy (San Holo) | E major · 150 | a vocal chop: saw through the vowel filter, 'ah' on held notes, 'oo'/'eh' between | glassy: FM ratio 2, a longer echo | softer (4.2 kHz), each stab scooped up 2 st over 150 ms, 1/8 SINE sway 0.6 | a softer mid (900 Hz); kick B1 61.7 Hz | light: 8th hats, more snaps, the shaker forward · 1, 3½ / 1, 2½, 3¾ |
| 3 Embers | dark, heavy (Said The Sky) | A minor · 150 | pluck + saw lead | celesta: FM ratio 4, a gentle clang | darker (5 kHz), detune 36, driven 10 dB, 1/16 tremolo 0.35 | the reese ±15 ct driven 14 dB; the mid WAVEFOLDED; kick A1 55 Hz | trap: 16th hats throughout, rolls every two bars · 1, 1¾, 2½, 3¾ / 1, 2, 3½, 4¼, 4½ |
| 4 Neon Tide | quirky, punchy (Flume) | G major · 145 | a square pluck (pulse table) + saw lead | metallic: FM ratio 1.41 with feedback | gated: short stabs, a deep 1/16 square tremolo 0.75, each stab DROPPING 1.5 st into the note | a plucky mid (fast filter); the reese's phaser faster (0.4 Hz); kick G1 49 Hz | swung: off-beat hats 80 ms late, a cowbell, doubled claps, rim on the a's · 1, 2¾, 3½ / 1, 1¾, 3¼, 4½ |

The kick is tuned to the key's root or fifth, whichever lands nearer 52 Hz. Every track's levels are measured and set
to the same order (the last table).

**The build and the drop are ONE clip** since fb-002, so the landing is judged with the drop: one supersaw strip plays
the build's quickening filtered stabs and then the drop's melody. Its tremolo is 0 until the drop, and its filter opens
350 Hz → 7 kHz through the build.

## What the verdicts changed (fb-002 → fb-003; taste.md lessons 1–6)

| element | was | now | the user's words |
|---|---|---|---|
| reese | ±12 ct, low-pass 750 Hz, sine 0.55, D2 register, dry | ±8 ct, 620 Hz, sine 0.85, **an octave lower (A♯1–A2)**, chorus 0.25, a 3.8 s room (mix 0.25, above 300 Hz), 150 % wide on top | "not deep enough, it need more ambient" |
| bell | high-pass 600 Hz, room 4.5 s at 0.42, echo 0.28 | + a **high cut at 6.5 kHz**, room 6.5 s at 0.55, echo 0.32 (feedback 0.5) | "high cut a little bit and more depth" |
| main synth | lead low-pass 2.6 kHz, square under, attack 18 ms; pluck 3.8 kHz | lead 1.7 kHz, **triangle** under, attack 35 ms; pluck 2.6 kHz; square pluck 2 kHz | "too rough, need more soft synth" |
| supersaw | low-pass 6.5 kHz, +1.2 oct envelope, octave stack 0.3, room 0.18 | **14 kHz**, +0.6 oct; octave stack 0.55, noise 0.04; EQ −4 dB @ 300 Hz, +5 dB @ 3.5 kHz, +4 dB shelf @ 8 kHz; room 0.12 — centre 2.7–2.9 kHz (was 1.9–2.1) | "need to be more sharp" |
| kick | 300 ms, tone 0.7, transient +35 % | 260 ms, tone 0.85, transient **+70 %**, tube saturation 6 dB; **a click layer** (an octave up, 70 ms, tone 1.0) on every hit — hits 7–10 dB over the mix (were 3–6) | "kick is not punchy enough" |
| mid bass | low-pass 1.4 kHz, drive 10 dB (Glass: 900 Hz) | **2.6 kHz, drive 18 dB** (mix 0.7) for every track | "need some hard bass" |
| drop melody | continuous | **rests** in the last two beats of every second bar, where a **vocal chop counter-melody** answers (the CHOP patch through the vowel filter, 'ah'/'oh', an echo, a room) | "need some rest note and counter melody" |

## The kick and its bass

| what | value | why |
|---|---|---|
| kick tune | +2.36 st → 55 Hz (A1) | the kick settles on 48·2^(tune/12) Hz (DSP measured); A is D major's 5th — the kick sits IN the key |
| kick decay | 380 ms (verse, build) · 300 ms (drop) | a shorter tail in the drop lets the duck release sooner |
| kick tone | 0.6 · 0.7 in the drop | more click in the drop |
| drop kick rhythm | bar A: 1, and-of-2, a-of-3 · bar B: 1, a-of-1, and-of-3, and-of-4 | the bounce after the snare; the push into the next bar |
| transient (drop) | attack +35 %, sustain −10 % | punch, and less boom fighting the sub |
| sub | a sine, mono, low-pass 300 Hz, attack 2 ms, release 60 ms | the floor; no harmonics to fight the kick's click |
| sub notes | the chord's root, F♯1 … F2 (46–87 Hz), struck WITH every kick, held to the next | "kick and bassline sync" — one event |
| mid bass | 3 saws ±9 ct + a square, an octave above the sub, low-pass 1.4 kHz (+1 oct envelope, 180 ms), tube drive 10 dB (mix 0.6), **high-pass 160 Hz**, a light chorus | the bass's voice where the ear hears it, out of the sub's way |
| ducks | sub and mid bass: 14 dB under the kick, **release 70 ms** · supersaw: 12 dB under the kick (release 120 ms) + 5 dB under the snare (80 ms) · reese: 4 dB (120 ms) | `strip duck`; measured 9.4 dB on the sub, back within 200 ms |

## The supersaw (the drop's hook)

| what | value | why |
|---|---|---|
| oscillators | osc1: 5 saws, detune 32 ct (±16), width 45 ct · osc2: 5 saws an octave up, detune 24, width 40, level 0.3 | two stacks an octave apart (Loopmasters); Solaris's unison is 5 voices an oscillator |
| filter | low-pass 6.5 kHz, res 0.12, +1.2 oct envelope (decay 260 ms, sustain 0.55) | bright at the stab's start, settling |
| amp | attack 2 ms, decay 700 ms, sustain 0.8, release 220 ms | a stab that rings |
| pitch rise | the bend lane: −1 semitone → 0 in 70 ms at every stab | the quick pitch envelope "up at the start" — the vocal-like attack |
| tremolo | shaper 1/16 square, depth 0.45, smooth 6 ms | the future bass "wub" on held chords (NI: volume stepped at 1/16) |
| voicing | the melody note on top, the chord's three nearest tones under it (within an octave) | the drop's lead IS the chords |
| colour | saturator soft 5 dB (mix 0.5) · imager 160 %, mono under 180 Hz · its own room (2.2 s, mix 0.18) | wide on top, centred below |

## The reese (the verse's bass)

| what | value | why |
|---|---|---|
| oscillators | osc1: 2 saws, detune 24 ct (±12) · osc2: a sine an octave down, level 0.55 | the detuned pair beats (the reese); the sine is its sub |
| filter | low-pass 750 Hz, res 0.2, +0.6 oct envelope (attack 60, decay 900); automated 650 → 1300 Hz through the verse | dark, then opening as the verse goes on |
| voice | legato, glide 70 ms | a bass that slides |
| movement | phaser 0.12 Hz, centre 600 Hz, depth 1.5 oct, 4 stages, feedback 0.4, mix 0.4 | the slow sweep the ear calls a reese |
| colour | saturator soft 8 dB (mix 0.5) · imager mono under 140 Hz | grit on top, a centred bottom |
| notes | the chord's root held 6 beats, then the 5th (or the octave) for 1.5, leading into the next | movement without busyness |

## The airy bell

| what | value | why |
|---|---|---|
| algorithm | 5 — op2 → op1, op4 → op3; carriers op1 and op3 | two pairs: the bell, and its shimmer |
| op1 / op2 | ratio 1, decay 2.6 s / ratio **3.5**, level 0.42, decay 650 ms | a non-integer modulator: the inharmonic clang (Splice), fading fast as a struck bell's does |
| op3 / op4 | ratio 2, +7 ct, level 0.5, decay 1.9 s / ratio 5, level 0.18, decay 380 ms | an octave a few cents sharp: the shimmer |
| air | high-pass 600 Hz · echo: a dotted eighth (300 ms), feedback 0.42, mix 0.28 · a room of 4.5 s, mix 0.42 | "airy": no low end, a long tail, echoes |
| notes | arpeggiating the chord in D6–D7 at 0, 1½, 3 (+ 4½, 6 in the second half) of each chord | sparkle above everything, denser as the verse grows |

## The main synth (the verse's melody)

| layer | value | why |
|---|---|---|
| pluck | Wavetable, "harmonics" table, position 0.55 swept down (posenv −0.3), 2 voices ±5 ct, low-pass 3.8 kHz +1.2 oct, amp decay 520 ms, sustain 0.15 | the attack — the plucked, acoustic-style lead (NI) |
| lead | Basic Synth: 3 saws ±7 ct (width 20) + a square, low-pass 2.6 kHz, attack 18 ms, legato glide 40 ms, a late vibrato (the mod lane: steady 0.3 s, then ±0.35 st × 0.8) | the sustain — the saw lead with vibrato (NI) |
| bus "Main synth" | chorus 0.6 Hz (mix 0.3) · echo dotted 8th (mix 0.16) · a room 2.4 s (mix 0.25) · gain +6 dB | one instrument made of two; it leads the verse |
| notes | the melody an octave up (D5–B5) | above the reese, under the bell |

## Drums and percussion

| element | value | why |
|---|---|---|
| snare + clap (drop) | snare +1 st, 240 ms, tone 0.62 · clap 360 ms · both on 3, one room (1.8 s, mix 0.24) | EDMProd: the snare a semitone up, one space for both |
| hats | 16ths accented on the 8ths (74 / 46) in the first phrase; 8ths with open hats between and a ride in the second | motion that grows |
| rolls | a 32nd roll into each 4-bar phrase, a triplet roll every 8 | the lift into the phrase |
| snaps | rim +6 st, 60 ms, on the e of 2 and 4, right of centre | filling between the kick and the snare |
| toms | high, mid, mid, low on the last beat of every second bar, panned L → R | a fill after the snare, never over it (Loopmasters) |
| shaker | a hat +9 st, 35 ms, the off-16ths, right | texture under everything |
| crash, reverse cymbal | a crash on each 8-bar phrase · noise with a 1.2 s attack, high-passed 2.5 kHz, into bar 9 | the phrase marked; the backwards swell |
| impact | an 808 boom (D, punch 24 st, 2.5 s, drive 0.45) + a crash on the drop's downbeat | the landing |

## The build-up

| layer | bars 1–4 | bars 5–8 |
|---|---|---|
| main synth, bell | the pre-chorus melody; the bell answering | the melody goes on to its end |
| reese | the roots | bars 5–6, then gone |
| chords (the supersaw, filtered) | halves, then quarters; low-pass from 350 Hz | eighths, then sixteenths; low-pass to 7 kHz |
| kick | on 1 | quarters, eighths, a 16th roll |
| snare | on 3, then quarters | eighths, 16ths, 32nds; velocity 70 → 120; tuned up an octave (the `snare.tune` automation) |
| risers | — | noise 300 Hz → 12 kHz (−40 → −8 dB); a saw bent up 24 st |
| the end | — | everything stops for a beat and a half; one big clap; the drop's downbeat (boom, crash, kick, the first chord) |

## The measured levels (fb-002, each element alone, integrated LUFS — the build → drop clips over both sections)

| clip | the levels |
|---|---|
| verses 1–4 | main synth −14.6 … −15.8 · reese −18.4 … −19.3 · bell −20.8 … −22.8 · kick −21.1 … −22.0 · snap −24.4 · hats −30.5 |
| build → drops 1–4 | supersaw −13.2 … −14.7 · main synth −13.9 … −14.8 · kick −15.9 … −16.3 · snare + clap −17.7 … −18.5 · sub −19.2 … −20.4 · mid bass −19.3 … −20.7 · percussion −23.4 … −26.6 (light, swung: lower by design) · shaker −28.6 … −31.7 · the kick ducks the sub 9.0 … 11.1 dB |

## The measured levels (fb-001)

| section | the levels (integrated LUFS, each element alone) |
|---|---|
| verse | main synth −15.4 · reese −18.9 · kick −21.2 · bell −21.4 · snap −24.4 · hats −30.5 |
| build-up | main synth −15.6 · reese −17.1 · chords −17.9 · snare roll −19.0 · kick −20.0 · bell −21.8 · pitch riser −24.3 · hats −30.3 (the noise riser −14.2 at its peak) |
| drop | supersaw −14.2 · kick −15.9 · snare + clap −17.8 · sub −19.5 · mid bass −19.9 · percussion −22.9 · shaker −31.5 — the kick ducks the sub 9.4 dB |
