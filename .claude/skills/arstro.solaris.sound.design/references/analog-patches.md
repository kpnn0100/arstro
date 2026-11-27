# The analog synth — a sound designer's guide, and the patches

The instrument: registry `analog` (`clip add --instrument analog`; every parameter in `apps/solaris/docs/API.md`).
Its math: `core/DigitalSignalProcessing/src/instrument/README.md` ## AnalogSynth. The patches below live in
`scripts/futurebass.py`; when a verdict changes a number, change both.

## The controls, and what each is for

| group | controls | what it does to the sound |
|---|---|---|
| `osc1`, `osc2` | `wave` saw · pulse · triangle · sine; `octave`, `semi`, `fine`; `level`; `width` | saw: bright, every harmonic — leads, chords, basses. Pulse: hollow at 0.5, nasal as `width` narrows. Triangle and sine: body with no edge — subs, soft layers |
| unison | `unison` 1–7, `detune` 0–1, `blend` 0–1, `spread` 0–1 | the supersaw. Voices start at random phases and run free. `detune` is the JP-8000's knob: gentle to ~0.5, wild above 0.8. `blend` 0 = the centre voice alone, 1 = mostly sides. `spread` puts the sides left and right. More voices never change the level |
| `sub` | `level`, `octave` −1/−2, `wave` sine/square, `noise` | weight under oscillator 1; white noise for air (0.03–0.05 under a supersaw) or as a riser's source |
| `filter` | `mode` lowpass 24 · lowpass 12 · bandpass 12 · highpass 12 · off; `cutoff`; `res`; `drive`; `env`; `keytrack`; `velocity` | the ladder. 24 dB: the classic dark-to-bright sweep, plucks, basses. 12 dB: gentler, pads. `res` from 0.9 it sings at the cutoff. `drive` (dB) pushes the mix into the ladder's tanh: warmth at 6, growl at 12+. `off` takes it out entirely |
| `fenv`, `amp` | attack, decay, sustain, release (ms; decay and release are the time to −60 dB) | exponential, like a capacitor: a 300 ms decay sounds shorter than a linear one because most of the fall is early |
| `penv` | `amount` (st), `decay` (ms) | the note starts that far from its pitch and comes home: −1 st in 60 ms is the "vocal" rise into a future bass stab; +24 st in 30 ms is a click for a bass or a kick |
| `lfo` | `shape`, `rate` or `sync` (4 bars … 1/32, dotted, triplet), `fade`, to `pitch` / `cutoff` / `amp` / `width` | `fade` 300 ms + `pitch` 0.15 = a singer's late vibrato; `sync` 1/8 sine to `cutoff` or `amp` = a sway on the beat; slow sine to `cutoff` = a reese that breathes; to `width` = PWM |
| `voice` | `mode` poly/mono/legato, `glide`, `bend`, `drift`, `phase` free/reset | `drift` 0.15–0.3 is what keeps chords alive; 0 for a sub. `phase=reset` for basses (every note the same attack); free for everything wide |
| output | `volume`, `velocity` | velocity sensitivity 0.3 for chords (accents without holes) |

## The patches (`futurebass.py`)

| patch | settings | why |
|---|---|---|
| **SUPER** — the drop's chords | osc1 saw, unison 7, detune 0.42, blend 0.75, spread 0.9 · osc2 saw +1 octave, unison 7, detune 0.5, blend 0.8, spread 1, level 0.45 · noise 0.04 · lowpass 24 at 16 kHz (400 Hz → 9 kHz through a build) · amp 2 / 700 / 0.85 / 180 · drift 0.2 · `penv` −1 st / 60 ms where the song asks | fourteen free-running saws: the body and, an octave up, the sheen; the noise is the air between stabs. Detune under 0.5 keeps the chord's pitch clear |
| **TOP** — the hook's top note | osc1 saw, unison 3, detune 0.22, narrow · osc2 a 35 % pulse an octave up at 0.2 · lowpass 24 at 11 kHz | the melody stays readable inside the chord |
| the supersaw bus | EQ: low cut 160 Hz, −2.5 dB at 350 Hz, +1.5 dB at 3.2 kHz, +2 dB shelf at 9 kHz · imager 150 %, mono under 200 Hz · room 1.8 s at 0.14 · ducked 10 dB by the kick (130 ms), 3 dB by the snare | the synth is bright by itself; the EQ only clears the low end and the mud |
| **SUB** | osc1 sine, filter off, mono, `phase=reset`, drift 0, attack 2 ms, release 60 ms | the floor; every note identical |
| **MID** — the bass's voice | osc1 saw unison 3 (detune 0.25), osc2 a 40 % pulse at 0.4 · lowpass 24 at 700 Hz, res 0.25, **drive 9 dB**, env +2.2 oct (decay 220 ms, sustain 0.3) · mono, `phase=reset` · then a 150 Hz high-pass | a plucked, driven ladder is what a "hard" bass is; the sub owns the floor under it |
| **REESE** — the verse's bed | osc1 saw + osc2 saw 18 cents apart, sub sine 0.7 · lowpass 24 at 480 Hz, res 0.15, drive 6 dB · LFO sine 0.15 Hz → cutoff ±0.5 oct · legato, glide 70 ms, drift 0.3 · imager mono under 140 Hz, a room on its top | the classic two-saw reese, deep (A♯1–A2) and breathing |
| **PAD** | osc1 saw unison 5 (detune 0.3, spread 1) + triangle 0.3 · lowpass 12 at 1.2 kHz · attack 500 ms, release 1.2 s · drift 0.3 | glue |
| **PLUCK** / **LEAD** — the verse's line | pluck: saws ×3 + a 30 % pulse an octave up, lowpass 24 at 650 Hz opened 3.5 oct by a 260 ms envelope, key-tracked · lead: saws ×3 + triangle, lowpass 24 at 1.9 kHz, attack 35 ms, legato, vibrato by the LFO (5.4 Hz, fade 350 ms, 0.18 st) | the attack and the sustain of one soft instrument |
| **CHOP** — the vocal chop | osc1 saw + pulse 0.3, noise 0.03, filter off, legato glide 35 ms, late vibrato → the `vowel` filter ('ah' on held notes, 'oo'/'eh' between) | the one sound the user rated good in every batch it appeared in |
| **ARP** (drop 2) | PLUCK at 2.2 kHz, shorter (220 ms), sixteenths up and down the chord, a 500 Hz high-pass, an echo | the lift |
| **NOISE** / pitch riser | noise through lowpass 24, its cutoff and volume automated (swell, riser, fall) · SUPER with `voice.bend` 24, bent two octaves over four bars | tension |
| not the analog synth | kick + click, snare + clap, percussion: the Drum Machine · the bell: FM, harmonic (ratio 4), in tune, E5–G6, few notes · keys: the piano or the guitar · impact: the 808 | what an analog synth is not for |

## The level targets (`TARGETS` — each element alone, LUFS, in its section)

Verse (second half): main synth −20.5 · keys −23.5 · reese −23.5 · bell −25.5 · pad −27.
Drop: kick −13 · supersaw −14.5 · impact −18 · snare + clap −19.5 · sub −20.5 · mid bass −21 · counter chop −21.5 ·
percussion −25.5 · kick top −28 · arp (drop 2) −27. The limiter adds 1.5 dB under a −1 dBTP ceiling.

Measured on the first song rendered this way: the drop 5.3 LU over the verse; the kick's hit 7.1 dB over the mix; the
kick ducks the sub 8.2 dB and lets it back within 200 ms.
