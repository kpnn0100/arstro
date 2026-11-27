# What the synth and the genre were learned from

## The analog synth

| source | what it gave |
|---|---|
| Adam Szabo, *How to Emulate the Super Saw* (KTH, 2010), its numbers as published in code at https://gist.github.com/audionerd/fe50790b7601cba65ddd855caffb05ad | the JP-8000's seven voices (offsets ±0.0195, ±0.0629, ±0.110 of the fundamental), the eleventh-order detune curve, the centre/side mix law, the high-pass at the fundamental, free-running random phases |
| Vadim Zavalishin, *The Art of VA Filter Design* 2.1.0 (https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_2.1.0.pdf) | the trapezoidal one-pole, the ladder with its feedback solved inside the sample (zero-delay feedback), the multimode outputs |
| KVR and synth-diy discussions of band-limited steps (https://www.kvraudio.com/forum/viewtopic.php?p=5269992, https://synth-diy.org/pipermail/synth-diy/2021-February/167019.html) | polynomial band-limited steps are the cheap, good anti-aliasing for saw and pulse, and 2× oversampling on top cleans what is left. Measured here: a two-point step left aliases 57 dB down; the four-point one 86 dB |
| Xfer Serum and NI Massive, as their users describe them (https://www.productionmusiclive.com/blogs/news/10-supersaw-tips-for-xfer-serum-get-a-big-lush-sound-without-layering, https://www.productionmusiclive.com/blogs/news/how-to-make-a-future-bass-synth-in-serum-beginners) | what a modern synth offers a sound designer: unison with detune and blend per oscillator, one oscillator an octave up, a noise source, a filter that can be off, envelopes dragged onto pitch and cutoff, an LFO at a note value. Serum's and Massive's wavetables, FX racks and modulation matrices are NOT copied: this is an analog synth |

## Future bass

| source | what it gave |
|---|---|
| Native Instruments, "How to make future bass" (https://blog.native-instruments.com/how-to-make-future-bass/) | 130–160 bpm half time; 7th and 9th chords; the lead as layers (a pluck, a sustained saw with vibrato) |
| EDMProd, "How to make future bass like Flume" (https://www.edmprod.com/how-to-make-future-bass-like-flume/) | kick on 1, snare on 3; snare and clap layered in one room; a mid bass over a sub, high-passed; the build in two halves ending in silence and one clap |
| Loopmasters, "How to make a future bass drop" (https://www.loopmasters.com/articles/4372-How-To-Make-A-Future-Bass-Drop-A-Future-Bass-Production-Tutorial) | two detuned saw stacks an octave apart with a quick pitch envelope up at each note; chords and bass ducked by kick AND snare; fills after the snare; riser, cut, impact |
| musicproductionwiki, "How to recreate future bass chords" (https://musicproductionwiki.com/articles/how-to-recreate-future-bass-chords — its numbers marked unverified there) | a supersaw of 7–12 voices at moderate detune; chords high-passed at 150–200 Hz; a cut around 150 Hz; the pump timed to the beat |

## The lead

| source | what it gave |
|---|---|
| Production Music Live, a Flume-style lead rebuilt in Sylenth1 (https://www.productionmusiclive.com — "Tennis Court"-style lead walkthrough) | the lead is a STACK: a detuned saw (8 voices), a pulse (3 voices), a pulse an octave up (4 voices, low), up to 5 % noise; each pair through a 24 dB low-pass; an LFO at 1/32 gently on the cutoff; an envelope on the cutoff |
| synthctrl, a Flume-style wobbling lead | a saw an octave down (3 voices) + a saw (4 voices) + a square sub; attack ~120 ms, decay ~690 ms, sustain −2.6 dB, release ~900 ms; an LFO at 1.1 Hz on tuning; portamento ~220 ms, always on — "the most important part" |
| Attack Magazine, a Don Diablo-style detuned lead | mono legato saws; a low-pass at 400 Hz with a 200 ms envelope; a pitch LFO near 6.5 Hz; a hard clip AFTER the filter to restore harmonics; a phaser, a quarter-note delay, a widener; lows cut, highs lifted |
| Sample Focus, future bass lead guide | glide 100–250 ms, legato; attack 20–50 ms soft or 1–10 ms punchy; reverb 2–4 s at 10–25 %; delay 1/8 or 1/16; cut under 100 Hz, lift 1–5 kHz; layer a saw with a sine or a pluck |

These are techniques, rebuilt on the analog synth as `drops.py` `LEADS`; no artist's patch, melody or sample is copied.
(The exact article URLs were read through search results and a fetch agent; Splice's page refused the fetch.)

## Melodic dubstep

| source | what it gave |
|---|---|
| Native Instruments, "How to make dubstep" (https://blog.native-instruments.com/how-to-make-dubstep/); Cymatics, "How to make dubstep" (https://cymatics.fm/blogs/production/how-to-make-dubstep) | half time: the kick on beat 1, the snare on beat 3; a quieter second snare for variation |
| Audiotool tutorials (https://audiotool.com/track/how_to_create_a_dubstep_drop, https://www.audiotool.com/track/melodic_dubstep_chords_tutorial_thingy) | call and response: the growl on the kick, the high lead answering on and past the snare, swapped on repeats; chord stacks from 2–3 saw synths |
| Bass Gorilla, Serum growl bass (https://bassgorilla.com/serum-tutorial-super-growl-bass-in-ableton-live/); MusicRadar, LFO wobble bass (https://www.musicradar.com/how-to/lfo-wobble-bass) and FM growl (https://www.musicradar.com/tuition/tech/how-to-make-a-growling-bass-sound-using-fm-synthesis-606919) | a growl is a low oscillator through a band-pass with some resonance and drive, an LFO at a note value on the cutoff, distortion after; keep the sub separate and clean |

Read as search summaries, not the full pages. No breakdown of a Seven Lions or Illenium drop was found; the 'yoi'
(a rising sweep into a vowel filter) is this skill's own construction on the genre's band-pass recipe.

**Honest limits.** No interview or session breakdown from Illenium, San Holo or Martin Garrix was found; their style
is described here as habits of the genre (a quiet verse on piano or guitar, a drop whose lead is its chords, long
soaring notes or short syncopated stabs), and no melody, progression or sound of theirs is reproduced. The Szabo
thesis itself was not fetched; its numbers are taken from code that cites it.
